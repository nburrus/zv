#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

from __future__ import annotations

import argparse
import os
import shlex
import signal
import socket
import subprocess
import threading
import time
from dataclasses import dataclass


BUFFER_SIZE = 64 * 1024
CONNECT_TIMEOUT_SEC = 5.0
SOCKET_POLL_TIMEOUT_SEC = 0.5
PROCESS_POLL_PERIOD_SEC = 0.2


def pick_available_port(host: str) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((host, 0))
        return int(sock.getsockname()[1])


def connect_with_retry(host: str, port: int, timeout_sec: float) -> socket.socket:
    deadline = time.monotonic() + timeout_sec
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((host, port), timeout=0.5)
            sock.settimeout(SOCKET_POLL_TIMEOUT_SEC)
            return sock
        except OSError as exc:
            last_error = exc
            time.sleep(0.05)
    raise RuntimeError(f"Could not connect to zv at {host}:{port}") from last_error


def close_socket(sock: socket.socket | None) -> None:
    if sock is None:
        return
    try:
        sock.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    try:
        sock.close()
    except OSError:
        pass


def forward_loop(src: socket.socket, dst: socket.socket, stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            data = src.recv(BUFFER_SIZE)
        except socket.timeout:
            continue
        except OSError:
            break

        if not data:
            break

        try:
            dst.sendall(data)
        except OSError:
            break

    stop.set()
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


def terminate_process(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return

    proc.terminate()
    try:
        proc.wait(timeout=2.0)
        return
    except subprocess.TimeoutExpired:
        pass

    proc.kill()
    try:
        proc.wait(timeout=1.0)
    except subprocess.TimeoutExpired:
        pass


@dataclass
class SessionConfig:
    zv_command: list[str]
    zv_host: str


def handle_client(client_sock: socket.socket, client_addr: tuple[str, int], cfg: SessionConfig) -> None:
    client_sock.settimeout(SOCKET_POLL_TIMEOUT_SEC)
    upstream_sock: socket.socket | None = None
    proc: subprocess.Popen[bytes] | None = None
    stop = threading.Event()

    try:
        upstream_port = pick_available_port(cfg.zv_host)
        cmd = [*cfg.zv_command, "-p", str(upstream_port), "--require-server"]
        print(f"[{client_addr[0]}:{client_addr[1]}] starting: {shlex.join(cmd)}", flush=True)
        proc = subprocess.Popen(cmd)

        upstream_sock = connect_with_retry(cfg.zv_host, upstream_port, CONNECT_TIMEOUT_SEC)
        print(
            f"[{client_addr[0]}:{client_addr[1]}] connected to zv {cfg.zv_host}:{upstream_port}",
            flush=True,
        )

        t1 = threading.Thread(target=forward_loop, args=(client_sock, upstream_sock, stop), daemon=True)
        t2 = threading.Thread(target=forward_loop, args=(upstream_sock, client_sock, stop), daemon=True)
        t1.start()
        t2.start()

        while not stop.is_set():
            if proc.poll() is not None:
                print(
                    f"[{client_addr[0]}:{client_addr[1]}] zv exited ({proc.returncode}), closing session",
                    flush=True,
                )
                stop.set()
                break
            time.sleep(PROCESS_POLL_PERIOD_SEC)

        close_socket(client_sock)
        close_socket(upstream_sock)
        t1.join(timeout=1.0)
        t2.join(timeout=1.0)
    except Exception as exc:
        print(f"[{client_addr[0]}:{client_addr[1]}] session error: {exc}", flush=True)
    finally:
        close_socket(client_sock)
        close_socket(upstream_sock)
        if proc is not None:
            terminate_process(proc)
        print(f"[{client_addr[0]}:{client_addr[1]}] session closed", flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="zv-proxy",
        description="Spawn one dedicated zv process per client and proxy traffic to it.",
    )
    parser.add_argument("--listen-host", default="127.0.0.1", help="Host interface to listen on.")
    parser.add_argument("--listen-port", type=int, default=4207, help="Port to listen on.")
    parser.add_argument(
        "--zv-host",
        default="127.0.0.1",
        help="Host used to connect to spawned zv server instances.",
    )
    parser.add_argument(
        "--zv-cmd",
        default=os.environ.get("ZV_CMD", "zv"),
        help="Command used to launch zv (default: $ZV_CMD or 'zv').",
    )
    return parser.parse_args()


def install_signal_handlers(shutdown_event: threading.Event) -> None:
    def _handler(signum: int, _frame: object) -> None:
        print(f"signal {signum} received, shutting down", flush=True)
        shutdown_event.set()

    signal.signal(signal.SIGINT, _handler)
    signal.signal(signal.SIGTERM, _handler)


def main() -> int:
    args = parse_args()
    cfg = SessionConfig(zv_command=shlex.split(args.zv_cmd), zv_host=args.zv_host)

    shutdown_event = threading.Event()
    install_signal_handlers(shutdown_event)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_sock:
        server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_sock.bind((args.listen_host, args.listen_port))
        server_sock.listen()
        server_sock.settimeout(SOCKET_POLL_TIMEOUT_SEC)

        print(f"zv-proxy listening on {args.listen_host}:{args.listen_port}", flush=True)
        print(f"spawning zv via: {shlex.join(cfg.zv_command)}", flush=True)

        threads: list[threading.Thread] = []
        while not shutdown_event.is_set():
            try:
                client_sock, client_addr = server_sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break

            print(f"[{client_addr[0]}:{client_addr[1]}] accepted", flush=True)
            thread = threading.Thread(
                target=handle_client,
                args=(client_sock, client_addr, cfg),
                daemon=True,
            )
            thread.start()
            threads.append(thread)

        for thread in threads:
            thread.join(timeout=1.0)

    print("zv-proxy stopped", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
