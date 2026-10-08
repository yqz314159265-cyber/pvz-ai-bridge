"""Serialized JSON transport for the bridge's Windows named pipe."""
from __future__ import annotations

import json
import os
import threading
import time
import uuid

MAX_MESSAGE = 1024 * 1024


class BridgeError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


def _write_all(write_chunk, payload: bytes, deadline: float, clock=time.monotonic,
               sleep=time.sleep) -> None:
    offset = 0
    while offset < len(payload):
        if clock() >= deadline:
            raise BridgeError("timeout", "Timed out writing the bridge request.")
        try:
            written = write_chunk(payload[offset:])
        except BlockingIOError:
            sleep(min(0.01, max(0.0, deadline - clock())))
            continue
        if written <= 0:
            raise BridgeError("write_failed", "Bridge pipe accepted no request bytes.")
        offset += written


def _read_line(read_chunk, deadline: float, clock=time.monotonic,
               sleep=time.sleep) -> bytes:
    buf = bytearray()
    while len(buf) <= MAX_MESSAGE:
        if clock() >= deadline:
            raise BridgeError("timeout", "Timed out waiting for the bridge reply.")
        try:
            part = read_chunk(4096)
        except BlockingIOError:
            sleep(min(0.01, max(0.0, deadline - clock())))
            continue
        if not part:
            break
        buf.extend(part)
        if b"\n" in buf:
            break
    if len(buf) > MAX_MESSAGE or b"\n" not in buf:
        raise BridgeError("invalid_reply", "Bridge reply is missing its newline or exceeds 1 MiB.")
    return bytes(buf).split(b"\n", 1)[0]


def _connect_pipe(wait_ready, open_pipe, deadline: float, clock=time.monotonic,
                  sleep=time.sleep):
    """Retry the named-pipe instance handoff without exceeding the request deadline."""
    retryable = {2, 121, 231, 233, 536}  # FILE_NOT_FOUND, SEM_TIMEOUT, PIPE_BUSY, NOT_CONNECTED, LISTENING
    last_error = 0
    while clock() < deadline:
        remaining_ms = max(1, min(100, int((deadline - clock()) * 1000)))
        wait_error = wait_ready(remaining_ms)
        if wait_error:
            last_error = wait_error
            if wait_error not in retryable:
                raise BridgeError("unavailable", f"Bridge pipe wait failed (Windows error {wait_error}).")
            sleep(min(0.01, max(0.0, deadline - clock())))
            continue
        handle, open_error = open_pipe()
        if handle is not None:
            return handle
        last_error = open_error
        if open_error not in retryable:
            raise BridgeError("connect_failed", f"Could not open bridge pipe (Windows error {open_error}).")
        sleep(min(0.01, max(0.0, deadline - clock())))
    raise BridgeError("unavailable", f"Bridge pipe did not become available before timeout (Windows error {last_error}).")


class PipeClient:
    """One request per pipe connection; calls are serialized per client."""
    def __init__(self, pid: int, timeout: float = 3.0):
        self.pid, self.timeout = pid, timeout
        self.path = rf"\\.\pipe\pvz-ai-{pid}"
        self._lock = threading.Lock()

    def request(self, op: str, args: dict | None = None) -> dict:
        if os.name != "nt":
            raise BridgeError("platform_unsupported", "The bridge uses Windows named pipes.")
        with self._lock:
            return self._request_windows(op, args or {})

    def _request_windows(self, op: str, args: dict) -> dict:
        import ctypes
        from ctypes import wintypes
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.WaitNamedPipeW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD)
        k32.WaitNamedPipeW.restype = wintypes.BOOL
        k32.CreateFileW.restype = wintypes.HANDLE
        k32.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                    wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
        k32.WriteFile.argtypes = (wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD,
                                  ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID)
        k32.ReadFile.argtypes = (wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                                 ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID)
        k32.SetNamedPipeHandleState.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD),
                                                ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD))
        k32.SetNamedPipeHandleState.restype = wintypes.BOOL
        k32.CloseHandle.argtypes = (wintypes.HANDLE,)
        invalid = ctypes.c_void_p(-1).value
        deadline = time.monotonic() + self.timeout
        def wait_ready(milliseconds):
            return 0 if k32.WaitNamedPipeW(self.path, milliseconds) else ctypes.get_last_error()
        def open_pipe():
            handle = k32.CreateFileW(self.path, 0xC0000000, 0, None, 3, 0, None)
            if handle == invalid:
                return None, ctypes.get_last_error()
            return handle, 0
        h = _connect_pipe(wait_ready, open_pipe, deadline)
        try:
            mode = wintypes.DWORD(0x1)  # PIPE_NOWAIT; ReadFile/WriteFile cannot hang indefinitely.
            if not k32.SetNamedPipeHandleState(h, ctypes.byref(mode), None, None):
                raise BridgeError("pipe_mode_failed", f"Could not enable bounded pipe I/O (Windows error {ctypes.get_last_error()}).")
            request_id = uuid.uuid4().hex
            payload = json.dumps({"id": request_id, "op": op, "args": args}, separators=(",", ":")).encode("utf-8") + b"\n"
            if len(payload) > MAX_MESSAGE:
                raise BridgeError("request_too_large", "Request exceeds the 1 MiB protocol limit.")
            def write_chunk(data: bytes) -> int:
                written = wintypes.DWORD()
                buffer = ctypes.create_string_buffer(data)
                if not k32.WriteFile(h, buffer, len(data), ctypes.byref(written), None):
                    error = ctypes.get_last_error()
                    if error in (232, 233, 536):  # NO_DATA / PIPE_NOT_CONNECTED / PIPE_LISTENING
                        raise BlockingIOError()
                    raise BridgeError("write_failed", f"Bridge request write failed (Windows error {error}).")
                return written.value

            def read_chunk(size: int) -> bytes:
                chunk = ctypes.create_string_buffer(size)
                read = wintypes.DWORD()
                if not k32.ReadFile(h, chunk, size, ctypes.byref(read), None):
                    error = ctypes.get_last_error()
                    if error in (232, 233, 536):
                        raise BlockingIOError()
                    if error == 109:  # ERROR_BROKEN_PIPE
                        return b""
                    raise BridgeError("read_failed", f"Bridge reply read failed (Windows error {error}).")
                return chunk.raw[:read.value]

            _write_all(write_chunk, payload, deadline)
            reply_line = _read_line(read_chunk, deadline)
            try:
                reply = json.loads(reply_line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as e:
                raise BridgeError("invalid_reply", "Bridge returned invalid UTF-8 JSON.") from e
            if not isinstance(reply, dict) or reply.get("id") != request_id or not isinstance(reply.get("ok"), bool):
                raise BridgeError("invalid_reply", "Bridge reply does not match the request contract.")
            if not reply["ok"]:
                err = reply.get("error") or {}
                raise BridgeError(str(err.get("code", "bridge_error")), str(err.get("message", "Bridge rejected request.")))
            return reply.get("result", {})
        finally:
            k32.CloseHandle(h)
