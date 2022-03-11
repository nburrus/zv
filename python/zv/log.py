from distutils.debug import DEBUG
from distutils.log import ERROR, INFO
from zv import Viewer

import numpy as np

matplotlib_supported = False
warned_about_matplotlib = False
try:
    import matplotlib.pyplot as plt
    import matplotlib as mpl
    matplotlib_supported = True
except ImportError:
    # Not enabling matplotlib support.
    pass

import argparse
from contextlib import ContextDecorator
import multiprocessing as mp
from multiprocessing import connection
from multiprocessing.connection import Client, Listener
import queue
import time
import pickle
import sys

import atexit

from enum import IntEnum
from typing import Any

class RateLimit(ContextDecorator):
    def __init__(self, minDuration):
        self._minDuration = minDuration

    def __enter__(self):
        self._lastEnter = time.time()
        return self

    def __exit__(self, *exc_info: Any):
        elapsed = time.time() - self._lastEnter
        if (self._minDuration > elapsed):
            time.sleep (self._minDuration - elapsed)

class DebuggerElement(IntEnum):
    StopProcess=0
    StopWhenAllWindowsClosed=1
    Image=2
    Figure=3

class _ZVLogChild:
    def __init__(self, conn: connection.Connection):
        self._conn = conn
        self._shutdown = False
        self._stop_when_all_windows_closed = False
        self._figures_by_name = dict()
        self._zv_viewer_per_group = {}
        self._already_displayed_data = False

    def _process_image (self, data):
        img, name, group = data
        # Support for mask images.
        if img.dtype == np.bool:
            img = img.astype(np.uint8)*255
        
        viewer = self._zv_viewer_per_group[group]
        if viewer is None:
            viewer = Viewer()
            viewer.initialize ()
            self._zv_viewer_per_group[group] = viewer
            self._already_displayed_data = True
        try:
            viewer.addImage (name, img, -1, replace=True)
        except:
            print (f"Could not add image with shape {img.shape} and dtype {img.dtype}")
        # cv2.imshow(name, img)        

    def _process_input(self, e):
        kind, data = e
        if kind == DebuggerElement.StopProcess:
            self._shutdown = True
        elif kind == DebuggerElement.StopWhenAllWindowsClosed:
            self._stop_when_all_windows_closed = True
        elif kind == DebuggerElement.Image:
            self._process_image (data)
        elif kind == DebuggerElement.Figure:
            if matplotlib_supported:
                fig, name = data
                if name in self._figures_by_name:
                    plt.close (self._figures_by_name[name])
                self._figures_by_name[name] = fig
                fig.canvas.manager.set_window_title(name)
                fig.canvas.mpl_connect('close_event', lambda e: self._on_fig_close(name))
                fig.show ()
            elif not warned_about_matplotlib:
                print ("Received a matplotlib figure, but the package is not installed.")
                warned_about_matplotlib = True

    def _on_fig_close(self, name):
        del self._figures_by_name[name]

    def _shouldStop (self):
        # not dict returns True if empty
        if self._already_displayed_data and not self._zv_viewer_per_group and not self._figures_by_name and self._stop_when_all_windows_closed:
            return True
        return self._shutdown

    def run (self):        
        while not self._shouldStop():
            with RateLimit(1./30.0):
                viewers_to_remove = []
                for group, viewer in self._zv_viewer_per_group.items():
                    viewer.renderFrame (0)
                    if viewer.exitRequested():
                        viewers_to_remove.append(group)
                for group in viewers_to_remove:
                    del self._zv_viewer_per_group[group]

                if self._figures_by_name:
                    # This would always bring the window to front, which is not what I want.
                    # plt.pause(0.005)
                    manager = plt.get_current_fig_manager()
                    if manager is not None:
                        manager.canvas.figure.canvas.flush_events()
                        manager.canvas.figure.canvas.draw_idle()
                
                if self._conn.poll():
                    e = self._conn.recv()
                    self._process_input (e)    

class ZVLogServer:
    def __init__(self, interface = '127.0.0.1', port = 7007):
        print (f"Server listening on {interface}:{port}...")
        self.listener = Listener(('127.0.0.1', port), authkey=b'zvlog')
        zvlog.start ()

    def start (self):
        while True:
            with self.listener.accept() as conn:
                print('connection accepted from', self.listener.last_accepted)
                try:
                    while True:
                        e = conn.recv()
                        zvlog._send_raw(e)
                except Exception as e:
                    print (f"ERROR: got exception {type(e), str(e)}, closing the client")
                    conn.close()

class ZVLog:
    def __init__(self):
        # Start the subprocess right away, to make sure that we won't fork
        # the program too late where there is already a lot of stuff in memory.
        # In particular matplotlib stuff can be problematic if we share the
        # memory of the main process.
        # But keep it disabled by default until explicitly enabled.
        self._enabled = False
        self.child = None

    def start(self, address_and_port=None):
        """ Example of address and port ('127.0.0.1', 7007)
        """
        if not address_and_port:
            self._start_child ()
        else:
            self.parent_conn = None
            delay = 1
            while self.parent_conn is None:
                try:
                    self.parent_conn = Client(address_and_port, authkey=b'zvlog')
                except Exception as e:
                    print(f"ERROR: ZVLog: cannot connect to {address_and_port} ({repr(e)}), retrying in {delay} seconds...")
                    time.sleep (delay)
                    delay = min(delay*2, 4)
        self.enabled = True

    @property
    def enabled(self): return self._enabled

    @enabled.setter
    def enabled(self, value):
        self._enabled = value

    def waitUntilWindowsAreClosed(self):
        if self.child:
            self.parent_conn.send((DebuggerElement.StopWhenAllWindowsClosed, None))
            self.child.join()
            self.child = None

    def shutdown(self):
        if self.child:
            self.parent_conn.send((DebuggerElement.StopProcess, None))

    def image(self, name: str, img: np.ndarray, group: str = 'default'):
        if not self._enabled:
            return
        self._send((DebuggerElement.Image, (img, name, group)))


    def plot(self, name: str, fig: mpl.figure.Figure):
        """Show a matplotlib figure
        
        Sample code
            with plt.ioff():
                fig,ax = plt.subplots(1,1)
                ax.plot([1,2,3], [1,4,9])
                zvlog.plot(fig)
        """
        if not self._enabled:
            return
        self._send((DebuggerElement.Figure, (fig, name)))

    def _send(self, e):
        try:
            self.parent_conn.send(e)
        except Exception as e:
            print(f"ZVLog error: {repr(e)}. {e} not sent.")

    def _start_child (self):
        if matplotlib_supported:        
            if plt.get_fignums():
                # If you create figures before forking, then the shared memory will
                # make a mess and freeze the subprocess.
                # Unfortunately this still does not catch whether a figure was created
                # and already closed, which is also a problem.
                raise Exception("You need to call start before creating any matplotlib figure.")

        self.ctx = mp.get_context("fork")
        self.parent_conn, child_conn = mp.Pipe()
        self.child = self.ctx.Process(target=ZVLog._run_child, args=(child_conn,))
        self.child.start ()

        # Make sure that we'll kill the logger when shutting down the parent process.
        atexit.register(ZVLog._zvlog_shutdown, self)

    def _send_raw(self, e):
        self.parent_conn.send(e)

    def _zvlog_shutdown(this_zvlog):
        this_zvlog.waitUntilWindowsAreClosed()

    def _run_child(conn: connection.Connection):
        processor = _ZVLogChild(conn)
        processor.run ()

zvlog = ZVLog()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ZVLog Server')
    parser.add_argument('--test-client', action='store_true', help='Run as a test client')
    args = parser.parse_args()
    if args.test_client:
        zvlog.start (('127.0.0.1',7007))
        zvlog.enabled = True
        zvlog.image("random", np.random.default_rng().random(size=(256,256,3), dtype=np.float32))
        zvlog.waitUntilWindowsAreClosed()
    else:
        server = ZVLogServer()
        server.start ()
