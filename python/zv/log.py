from distutils.debug import DEBUG
from distutils.log import ERROR, INFO
import zv

import numpy as np

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

from enum import IntEnum, auto
from typing import Any

class ZVLog:
    def __init__(self):
        # Start the subprocess right away, to make sure that we won't fork
        # the program too late where there is already a lot of stuff in memory.
        # In particular matplotlib stuff can be problematic if we share the
        # memory of the main process.
        # But keep it disabled by default until explicitly enabled.
        self._enabled = False
        self.client = None

    def start(self, address: str = None, port: int = 4207):
        self.client = zv.Client()
        if address:
            if not self.client.connect (address, port):
                return False
        else:
            print("Starting a local server...")
            server_port = self._start_server ()
            if not self.client.connect ('127.0.0.1', server_port):
                return False
        self.enabled = True
        return True

    @property
    def enabled(self): return self._enabled and self.client

    @enabled.setter
    def enabled(self, value):
        self._enabled = value

    def waitUntilWindowsAreClosed(self):
        if self.client:
            self.client.waitUntilDisconnected ()

    def stop(self):
        if self.client:
            self.client.disconnect()

    def image(self, name: str, img: np.ndarray, group: str = 'default'):
        if not self.enabled:
            return
        self.client.addImage (name, img, group)

    def _start_server (self):
        self.ctx = mp.get_context("fork")
        self.parent_conn, child_conn = mp.Pipe()
        self.child = self.ctx.Process(target=ZVLog._run_server, args=(child_conn,))
        self.child.start ()
        
        # Make sure that we'll kill the logger when shutting down the parent process.
        atexit.register(ZVLog._zvlog_shutdown, self)

        # Block until we get a port on which the server could listen.
        server_port = self.parent_conn.recv()
        print (f"Started the server on port {server_port}")
        return server_port

    def _zvlog_shutdown(this_zvlog):
        this_zvlog.waitUntilWindowsAreClosed()

    def _run_server(conn: connection.Connection):
        app = zv.App()
        port = 42007
        while not app.initialize(["zvlog", "--port", str(port), "--interface", "127.0.0.1", "--require-server"]):
            print (f"Could not listen on port {port}, trying the next one...")
            port = port + 1
        conn.send (port)
        viewer = app.getViewer()
        while app.numViewers > 0:
            app.updateOnce(1.0 / 30.0)

zvlog = ZVLog()

if __name__ == "__main__":
    zvlog.start ()
    zvlog.image("random1", np.random.default_rng().random(size=(256,256,3), dtype=np.float32), group="default")
    zvlog.image("random2", np.random.default_rng().random(size=(256,256,3), dtype=np.float32), group="default")

    zvlog.image("random3", np.random.default_rng().random(size=(256,256,3), dtype=np.float32), group="SecondGroup")
    zvlog.image("random4", np.random.default_rng().random(size=(256,256,3), dtype=np.float32), group="SecondGroup")
    zvlog.waitUntilWindowsAreClosed()
