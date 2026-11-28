#!/usr/bin/env python3
"""
Serial Monitor + Dual-Mode (WebSocket & Raw TCP) Betaflight Configurator Proxy.

Features:
  - Creates a unique timestamped log file on every run by default
    (e.g., com9_proxy_20261128_112627.txt).
  - Keeps listening for Betaflight Configurator even if the serial port (e.g. COM9)
    is absent at startup or temporarily unplugged, and auto-connects when present.
  - Supports easy port switching via CLI (`python com9_betaflight_proxy.py COM5` or
    `--port COM5`) and live in-console commands (`:ports`, `:port COM5`, `:baud 115200`).
  - Accepts BOTH:
      * Betaflight Web App PWA (master.app.betaflight.com / app.betaflight.com,
        Manual connection mode):  ws://127.0.0.1:5761
      * Desktop Betaflight Configurator (Manual port):
                                  tcp://127.0.0.1:5761
"""

import argparse
import base64
import hashlib
import os
import re
import select
import socket
import struct
import sys
import threading
import time

try:
    import serial
    from serial import SerialException
    from serial.tools import list_ports
except ImportError:
    serial = None
    SerialException = OSError
    list_ports = None

try:
    import msvcrt
except ImportError:
    msvcrt = None


WS_MAGIC_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
DEFAULT_SERIAL_PORT = "COM9"
DEFAULT_BAUD = 115200
DEFAULT_LISTEN_HOST = "127.0.0.1"
DEFAULT_LISTEN_PORT = 5761


def now_ts():
    """Return local wall-clock timestamp with millisecond precision."""
    t = time.time()
    lt = time.localtime(t)
    ms = int((t - int(t)) * 1000)
    return time.strftime("%H:%M:%S", lt) + f".{ms:03d}"


def run_stamp(epoch_sec=None):
    """Return filename-safe YYYYMMDD_HHMMSS timestamp for the current run."""
    lt = time.localtime(epoch_sec if epoch_sec is not None else time.time())
    return time.strftime("%Y%m%d_%H%M%S", lt)


def sanitize_port_for_filename(port_name):
    """Normalize a serial port string into a safe lowercase filename prefix."""
    base = os.path.basename(port_name.strip()) if port_name else "serial"
    cleaned = re.sub(r"[^A-Za-z0-9_-]+", "_", base).strip("_").lower()
    return cleaned or "serial"


def make_default_log_path(port_name, base_dir=None, epoch_sec=None):
    """Build a unique per-run log filename containing port and start time."""
    target_dir = base_dir or os.path.dirname(os.path.abspath(__file__))
    prefix = sanitize_port_for_filename(port_name)
    return os.path.join(target_dir, f"{prefix}_proxy_{run_stamp(epoch_sec)}.txt")


def enumerate_serial_ports():
    """Return list of (device, description) for currently attached serial ports."""
    if list_ports is None:
        return []
    try:
        return [(p.device, p.description or "") for p in sorted(list_ports.comports(), key=lambda x: x.device)]
    except Exception:
        return []


def format_available_ports():
    """Format attached COM/tty serial ports for human-readable status messages."""
    ports = enumerate_serial_ports()
    if not ports:
        return "none detected"
    return ", ".join(f"{dev} ({desc})" if desc else dev for dev, desc in ports)


def describe_msp_packets(data):
    """Return brief MSP v1/v2 frame tags found in data for debugging."""
    tags = []
    i = 0
    n = len(data)
    while i + 5 < n:
        if data[i] == 0x24 and data[i + 1] == 0x4D and data[i + 2] in (0x3C, 0x3E, 0x21):
            direction = chr(data[i + 2])
            size = data[i + 3]
            cmd = data[i + 4]
            tags.append(f"MSP1({direction} cmd={cmd} len={size})")
            i += max(6 + size, 1)
            continue
        if data[i] == 0x24 and data[i + 1] == 0x58 and data[i + 2] in (0x3C, 0x3E, 0x21) and i + 8 < n:
            direction = chr(data[i + 2])
            cmd = data[i + 4] | (data[i + 5] << 8)
            size = data[i + 6] | (data[i + 7] << 8)
            tags.append(f"MSP2({direction} cmd={cmd} len={size})")
            i += max(9 + size, 1)
            continue
        i += 1
    return " ".join(tags)


def format_ascii_preview(data):
    """Render printable ASCII and escaped control chars cleanly."""
    out = []
    for b in data:
        if b == 0x0D:
            out.append("\\r")
        elif b == 0x0A:
            out.append("\\n")
        elif b == 0x09:
            out.append("\\t")
        elif 0x20 <= b <= 0x7E:
            out.append(chr(b))
        else:
            out.append(f"\\x{b:02X}")
    return "".join(out)


def is_mostly_printable_text(data):
    """Heuristic: true when chunk is boot/CLI ASCII text with no MSP binary framing."""
    if not data:
        return False
    if b"$M<" in data or b"$M>" in data or b"$X<" in data or b"$X>" in data:
        return False
    for b in data:
        if b in (0x09, 0x0A, 0x0D):
            continue
        if 0x20 <= b <= 0x7E:
            continue
        return False
    return True


def build_ws_frame(opcode, payload):
    """Encode an unmasked server-to-client RFC 6455 WebSocket frame."""
    header = bytearray([0x80 | (opcode & 0x0F)])
    length = len(payload)
    if length < 126:
        header.append(length)
    elif length <= 0xFFFF:
        header.append(126)
        header.extend(struct.pack("!H", length))
    else:
        header.append(127)
        header.extend(struct.pack("!Q", length))
    return bytes(header) + payload


def recv_exact(sock, count):
    """Read exact byte count from sock or return None on EOF."""
    buf = bytearray()
    while len(buf) < count:
        chunk = sock.recv(count - len(buf))
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


def read_ws_frame(sock):
    """Decode a single RFC 6455 WebSocket frame; returns (opcode, payload) or (None, None)."""
    head = recv_exact(sock, 2)
    if head is None:
        return None, None
    b0, b1 = head[0], head[1]
    opcode = b0 & 0x0F
    masked = (b1 & 0x80) != 0
    length = b1 & 0x7F
    if length == 126:
        ext = recv_exact(sock, 2)
        if ext is None:
            return None, None
        length = struct.unpack("!H", ext)[0]
    elif length == 127:
        ext = recv_exact(sock, 8)
        if ext is None:
            return None, None
        length = struct.unpack("!Q", ext)[0]
    mask_key = b""
    if masked:
        mask_key = recv_exact(sock, 4)
        if mask_key is None:
            return None, None
    payload = recv_exact(sock, length) if length > 0 else b""
    if payload is None:
        return None, None
    if masked and length > 0:
        unmasked = bytearray(length)
        for idx in range(length):
            unmasked[idx] = payload[idx] ^ mask_key[idx & 3]
        payload = bytes(unmasked)
    return opcode, payload


def try_ws_handshake(sock, request_bytes):
    """Validate HTTP WebSocket upgrade headers and send 101 Switching Protocols."""
    text = request_bytes.decode("latin1", errors="replace")
    lines = text.split("\r\n")
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()

    key = headers.get("sec-websocket-key")
    upgrade = headers.get("upgrade", "").lower()
    if not key or "websocket" not in upgrade:
        sock.sendall(
            b"HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            b"Expected WebSocket upgrade or raw TCP Betaflight MSP stream.\r\n"
        )
        return False, "invalid-upgrade"

    accept_raw = (key + WS_MAGIC_GUID).encode("ascii")
    accept_b64 = base64.b64encode(hashlib.sha1(accept_raw).digest()).decode("ascii")

    proto_line = ""
    chosen_proto = None
    req_protos = headers.get("sec-websocket-protocol", "")
    if req_protos:
        tokens = [p.strip() for p in req_protos.split(",") if p.strip()]
        if "binary" in tokens:
            chosen_proto = "binary"
        elif tokens:
            chosen_proto = tokens[0]
        if chosen_proto:
            proto_line = f"Sec-WebSocket-Protocol: {chosen_proto}\r\n"

    resp = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {accept_b64}\r\n"
        f"{proto_line}"
        "\r\n"
    ).encode("ascii")
    sock.sendall(resp)
    origin = headers.get("origin", "-")
    return True, f"origin={origin} proto={chosen_proto or '-'}"


def read_http_header_block(sock, stop_event=None):
    """Wait for client's first byte and distinguish HTTP WebSocket Upgrade from raw TCP."""
    while True:
        if stop_event is not None and stop_event.is_set():
            return "eof", b""
        ready, _, _ = select.select([sock], [], [], 0.2)
        if ready:
            break

    peeked = sock.recv(16, socket.MSG_PEEK)
    if not peeked:
        return "eof", b""

    upper = peeked.upper()
    if upper.startswith((b"GET ", b"HEAD ", b"POST ", b"OPTIONS ")):
        buf = bytearray()
        sock.settimeout(2.0)
        while b"\r\n\r\n" not in buf and len(buf) < 16384:
            chunk = sock.recv(1024)
            if not chunk:
                return "eof", bytes(buf)
            buf.extend(chunk)
        sock.settimeout(None)
        return "ws", bytes(buf)

    return "tcp", b""


def default_serial_opener(port, baud):
    """Open physical serial port via pyserial with short timeout for clean switching."""
    if serial is None:
        raise RuntimeError("pyserial is not installed")
    return serial.Serial(port, baud, timeout=0.05, write_timeout=1.0)


class ClientSession:
    """Represents a connected Betaflight client (raw TCP or WebSocket)."""

    def __init__(self, sock, addr, mode):
        self.sock = sock
        self.addr = addr
        self.mode = mode  # "tcp" or "ws"
        self.send_lock = threading.Lock()
        self.closed = False

    def send_data(self, data):
        if self.closed or not data:
            return False
        try:
            with self.send_lock:
                if self.mode == "ws":
                    self.sock.sendall(build_ws_frame(0x2, data))
                else:
                    self.sock.sendall(data)
            return True
        except OSError:
            self.closed = True
            return False

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            if self.mode == "ws":
                with self.send_lock:
                    self.sock.sendall(build_ws_frame(0x8, b""))
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


class SerialBetaflightProxy:
    """Bridges a hot-pluggable/switchable serial port to a log file and TCP/WS clients."""

    def __init__(
        self,
        target_port,
        target_baud,
        log_file,
        raw_file=None,
        echo_stdout=True,
        serial_opener=None,
        port_lister=None,
        retry_interval=0.5,
    ):
        self.target_port = target_port
        self.target_baud = int(target_baud)
        self.log_file = log_file
        self.raw_file = raw_file
        self.echo_stdout = echo_stdout
        self.serial_opener = serial_opener or default_serial_opener
        self.port_lister = port_lister or format_available_ports
        self.retry_interval = retry_interval

        self.stop_event = threading.Event()
        self.log_lock = threading.Lock()
        self.ser_state_lock = threading.Lock()
        self.ser_write_lock = threading.Lock()
        self.client_lock = threading.Lock()

        self.ser = None
        self.active_port = None
        self.active_baud = None
        self.reconnect_requested = True
        self.active_client = None

    def get_active_serial(self):
        with self.ser_state_lock:
            return self.ser, self.active_port, self.active_baud

    def get_target(self):
        with self.ser_state_lock:
            return self.target_port, self.target_baud

    def switch_port(self, new_port, new_baud=None):
        """Request immediate switch to another serial port (and optional baud rate)."""
        new_port = new_port.strip()
        if not new_port:
            return
        with self.ser_state_lock:
            self.target_port = new_port
            if new_baud is not None:
                self.target_baud = int(new_baud)
            baud = self.target_baud
            self.reconnect_requested = True
        self.log_event(f"[PORT SWITCH REQUESTED] Target={new_port} @ {baud}")

    def set_baud(self, new_baud):
        """Request baud rate change on the current target serial port."""
        with self.ser_state_lock:
            self.target_baud = int(new_baud)
            port = self.target_port
            self.reconnect_requested = True
        self.log_event(f"[BAUD CHANGE REQUESTED] Target={port} @ {int(new_baud)}")

    def log_event(self, text):
        line = f"[{now_ts()}] {text}\n"
        with self.log_lock:
            self.log_file.write(line.encode("utf-8", errors="replace"))
            self.log_file.flush()
            if self.echo_stdout:
                sys.stdout.write(line)
                sys.stdout.flush()

    def log_traffic(self, direction, data):
        if not data:
            return
        msp_info = describe_msp_packets(data)
        with self.log_lock:
            if direction == "RX" and self.raw_file is not None:
                self.raw_file.write(data)
                self.raw_file.flush()

            if direction == "RX" and is_mostly_printable_text(data):
                text = data.decode("utf-8", errors="replace")
                entry = f"[{now_ts()}] [RX <- {self.active_port or 'SERIAL'} {len(data)}B TEXT]\n{text}"
                if not text.endswith("\n"):
                    entry += "\n"
                self.log_file.write(entry.encode("utf-8", errors="replace"))
                self.log_file.flush()
                if self.echo_stdout:
                    sys.stdout.write(text)
                    sys.stdout.flush()
                return

            hex_str = " ".join(f"{b:02X}" for b in data)
            ascii_str = format_ascii_preview(data)
            tag = f" {msp_info}" if msp_info else ""
            entry = (
                f"[{now_ts()}] [{direction} {len(data)}B{tag}]\n"
                f"  HEX  : {hex_str}\n"
                f"  ASCII: {ascii_str}\n"
            )
            self.log_file.write(entry.encode("utf-8", errors="replace"))
            self.log_file.flush()
            if self.echo_stdout:
                sys.stdout.write(entry)
                sys.stdout.flush()

    def write_serial(self, data, source_label="PROXY"):
        if not data:
            return False
        ser, port, _ = self.get_active_serial()
        if ser is None:
            target_port, _ = self.get_target()
            self.log_traffic(f"TX DROPPED (NO SERIAL {target_port}) <- {source_label}", data)
            return False

        self.log_traffic(f"TX({port}) <- {source_label}", data)
        try:
            with self.ser_write_lock:
                ser.write(data)
                ser.flush()
            return True
        except Exception as exc:
            self.log_event(f"[SERIAL WRITE ERROR] {port}: {exc}")
            with self.ser_state_lock:
                self.reconnect_requested = True
            return False

    def set_client(self, session):
        with self.client_lock:
            old = self.active_client
            self.active_client = session
        if old is not None:
            old.close()

    def clear_client(self, session):
        with self.client_lock:
            if self.active_client is session:
                self.active_client = None

    def _close_current_serial(self):
        with self.ser_state_lock:
            ser = self.ser
            self.ser = None
            self.active_port = None
            self.active_baud = None
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass

    def serial_reader_loop(self):
        last_wait_msg = None
        while not self.stop_event.is_set():
            with self.ser_state_lock:
                need_reconnect = self.reconnect_requested or (self.ser is None)
                target_port = self.target_port
                target_baud = self.target_baud

            if need_reconnect:
                if self.ser is not None:
                    old_port = self.active_port
                    self._close_current_serial()
                    self.log_event(f"[SERIAL CLOSED] Closed {old_port}")

                with self.ser_state_lock:
                    self.reconnect_requested = False

                try:
                    new_ser = self.serial_opener(target_port, target_baud)
                    with self.ser_state_lock:
                        if self.target_port != target_port or self.target_baud != target_baud:
                            # Target changed mid-open; discard and loop immediately
                            self.reconnect_requested = True
                            new_ser.close()
                            continue
                        self.ser = new_ser
                        self.active_port = target_port
                        self.active_baud = target_baud
                    last_wait_msg = None
                    self.log_event(f"[SERIAL CONNECTED] {target_port} @ {target_baud}")
                except Exception as exc:
                    avail = self.port_lister()
                    wait_sig = (target_port, target_baud, avail)
                    if last_wait_msg != wait_sig:
                        self.log_event(
                            f"[WAIT SERIAL] {target_port} @ {target_baud} unavailable ({exc}). "
                            f"Proxy still listening | Attached ports: {avail}"
                        )
                        last_wait_msg = wait_sig
                    self.stop_event.wait(self.retry_interval)
                    continue

            ser = self.ser
            if ser is None:
                continue

            try:
                pending = ser.in_waiting
                chunk = ser.read(pending if pending > 0 else 1)
                if not chunk:
                    continue

                buf = bytearray(chunk)
                deadline = time.monotonic() + 0.012
                while time.monotonic() < deadline and len(buf) < 4096 and not self.reconnect_requested:
                    more = ser.in_waiting
                    if more > 0:
                        buf.extend(ser.read(min(more, 4096 - len(buf))))
                        deadline = time.monotonic() + 0.004
                    else:
                        time.sleep(0.001)

                data = bytes(buf)
                self.log_traffic("RX", data)

                with self.client_lock:
                    client = self.active_client
                if client is not None:
                    if not client.send_data(data):
                        self.clear_client(client)
                        client.close()
            except Exception as exc:
                if not self.stop_event.is_set():
                    self.log_event(f"[SERIAL DISCONNECTED] {target_port} lost ({exc}); waiting to reconnect...")
                    self._close_current_serial()
                    last_wait_msg = None
                    self.stop_event.wait(self.retry_interval)

        self._close_current_serial()

    def handle_local_command(self, raw_cmd):
        """Process interactive local proxy commands (:ports, :port COM5, :baud 115200, :status, :help, :quit)."""
        cmd_line = raw_cmd.strip()
        if cmd_line.startswith(":"):
            cmd_line = cmd_line[1:].strip()
        if not cmd_line:
            return

        parts = cmd_line.split()
        verb = parts[0].lower()

        if verb in ("ports", "list", "ls"):
            self.log_event(f"[PORTS] {self.port_lister()}")
            return

        if verb in ("status", "info"):
            ser, act_port, act_baud = self.get_active_serial()
            tgt_port, tgt_baud = self.get_target()
            state = f"CONNECTED ({act_port} @ {act_baud})" if ser else f"WAITING ({tgt_port} @ {tgt_baud})"
            with self.client_lock:
                cli = f"{self.active_client.mode.upper()} {self.active_client.addr}" if self.active_client else "none"
            self.log_event(f"[STATUS] Serial={state} | Client={cli}")
            return

        if verb in ("port", "switch", "use"):
            if len(parts) < 2:
                self.log_event("[USAGE] :port <PORT> [BAUD]   (example: :port COM5)")
                return
            baud = None
            if len(parts) >= 3:
                try:
                    baud = int(parts[2])
                except ValueError:
                    self.log_event(f"[ERROR] Invalid baud rate: {parts[2]}")
                    return
            self.switch_port(parts[1], baud)
            return

        if verb == "baud":
            if len(parts) < 2:
                self.log_event("[USAGE] :baud <RATE>   (example: :baud 115200)")
                return
            try:
                self.set_baud(int(parts[1]))
            except ValueError:
                self.log_event(f"[ERROR] Invalid baud rate: {parts[1]}")
            return

        if verb == "reconnect":
            with self.ser_state_lock:
                self.reconnect_requested = True
            self.log_event("[RECONNECT] Forcing serial reconnect...")
            return

        if verb in ("quit", "exit"):
            self.log_event("[STOPPING] Exit requested from console.")
            self.stop_event.set()
            return

        if verb in ("help", "?"):
            self.log_event(
                "[COMMANDS] :ports | :port <COMx> [baud] | :baud <rate> | :reconnect | :status | :quit"
            )
            return

        # Shorthand when someone types just "COM5" or "/dev/ttyUSB0"
        if re.match(r"^(COM\d+|/dev/tty\S+)$", parts[0], re.IGNORECASE):
            baud = None
            if len(parts) >= 2:
                try:
                    baud = int(parts[1])
                except ValueError:
                    self.log_event(f"[ERROR] Invalid baud rate: {parts[1]}")
                    return
            self.switch_port(parts[0], baud)
            return

        self.log_event(f"[UNKNOWN COMMAND] '{cmd_line}'. Type :help for proxy commands.")

    def keyboard_loop(self):
        if not sys.stdin or not sys.stdin.isatty():
            return

        if msvcrt is None:
            # POSIX fallback: line-oriented stdin for local proxy commands or serial CLI lines
            while not self.stop_event.is_set():
                try:
                    ready, _, _ = select.select([sys.stdin], [], [], 0.1)
                    if not ready:
                        continue
                    line = sys.stdin.readline()
                    if not line:
                        return
                    stripped = line.rstrip("\r\n")
                    ser, _, _ = self.get_active_serial()
                    if stripped.startswith(":") or ser is None:
                        self.handle_local_command(stripped)
                    else:
                        self.write_serial((stripped + "\r\n").encode("utf-8"), source_label="KBD")
                except Exception:
                    time.sleep(0.05)
            return

        local_mode = False
        local_buf = bytearray()

        while not self.stop_event.is_set():
            try:
                if not msvcrt.kbhit():
                    time.sleep(0.02)
                    continue

                ch = msvcrt.getch()
                if ch == b"\x03":  # Ctrl+C
                    self.stop_event.set()
                    return
                if ch in (b"\x00", b"\xe0"):
                    msvcrt.getch()
                    continue

                ser, _, _ = self.get_active_serial()

                # Enter local command mode via ':' (or anytime serial isn't connected yet)
                if not local_mode and (ch == b":" or ser is None):
                    local_mode = True
                    local_buf.clear()
                    sys.stdout.write("\r\nproxy> ")
                    if ch != b":":
                        if ch in (b"\r", b"\n"):
                            sys.stdout.write("\n")
                            sys.stdout.flush()
                            local_mode = False
                            continue
                        local_buf.extend(ch)
                        sys.stdout.write(ch.decode("latin1", errors="replace"))
                    else:
                        sys.stdout.write(":")
                    sys.stdout.flush()
                    continue

                if local_mode:
                    if ch in (b"\r", b"\n"):
                        sys.stdout.write("\n")
                        sys.stdout.flush()
                        cmd_str = local_buf.decode("utf-8", errors="replace")
                        local_buf.clear()
                        local_mode = False
                        self.handle_local_command(cmd_str)
                    elif ch in (b"\x08", b"\x7f"):  # Backspace
                        if local_buf:
                            local_buf.pop()
                            sys.stdout.write("\b \b")
                            sys.stdout.flush()
                    elif ch == b"\x1b":  # Esc cancels local command entry
                        local_buf.clear()
                        local_mode = False
                        sys.stdout.write(" [cancelled]\n")
                        sys.stdout.flush()
                    else:
                        local_buf.extend(ch)
                        sys.stdout.write(ch.decode("latin1", errors="replace"))
                        sys.stdout.flush()
                    continue

                # Connected & not in local command mode: raw CLI keystroke passthrough
                self.write_serial(ch, source_label="KBD")
            except Exception:
                time.sleep(0.05)

    def handle_client(self, conn, addr):
        try:
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass

        try:
            mode, header_bytes = read_http_header_block(conn, self.stop_event)
        except OSError:
            conn.close()
            return

        if mode == "eof":
            conn.close()
            return

        ws_note = ""
        if mode == "ws":
            ok, ws_info = try_ws_handshake(conn, header_bytes)
            if not ok:
                self.log_event(f"[WS HANDSHAKE REJECTED] {addr[0]}:{addr[1]} ({ws_info})")
                conn.close()
                return
            ws_note = f" {ws_info}"

        session = ClientSession(conn, addr, mode)
        self.set_client(session)
        ser, act_port, act_baud = self.get_active_serial()
        ser_note = f"{act_port} @ {act_baud}" if ser else "SERIAL NOT CONNECTED YET"
        self.log_event(f"[CLIENT CONNECTED] {addr[0]}:{addr[1]} via {mode.upper()}{ws_note} ({ser_note})")

        close_reason = ""
        try:
            while not self.stop_event.is_set() and not session.closed:
                if mode == "ws":
                    opcode, payload = read_ws_frame(conn)
                    if opcode is None:
                        close_reason = " (EOF)"
                        break
                    if opcode == 0x8:  # Close
                        if len(payload) >= 2:
                            code = struct.unpack("!H", payload[:2])[0]
                            reason = payload[2:].decode("utf-8", errors="replace")
                            close_reason = f" (WS close code={code} {reason!r})"
                        else:
                            close_reason = " (WS close)"
                        break
                    if opcode == 0x9:  # Ping -> Pong
                        with session.send_lock:
                            conn.sendall(build_ws_frame(0xA, payload))
                        continue
                    if opcode == 0xA:  # Pong
                        continue
                    if opcode in (0x0, 0x1, 0x2) and payload:
                        self.write_serial(payload, source_label="WS")
                else:
                    ready, _, _ = select.select([conn], [], [], 0.2)
                    if not ready:
                        continue
                    data = conn.recv(4096)
                    if not data:
                        close_reason = " (EOF)"
                        break
                    self.write_serial(data, source_label="TCP")
        except OSError as exc:
            if not self.stop_event.is_set():
                self.log_event(f"[CLIENT ERROR] {addr[0]}:{addr[1]}: {exc}")
        finally:
            self.clear_client(session)
            session.close()
            self.log_event(f"[CLIENT DISCONNECTED] {addr[0]}:{addr[1]}{close_reason}")


def parse_args():
    p = argparse.ArgumentParser(
        description="Serial Monitor + Dual-Mode (WS & TCP) Proxy for Betaflight Configurator."
    )
    p.add_argument(
        "positional_port",
        nargs="?",
        default=None,
        help="Optional serial port shorthand (e.g. COM5), overrides --port",
    )
    p.add_argument(
        "-p",
        "--port",
        default=DEFAULT_SERIAL_PORT,
        help=f"Initial serial port (default: {DEFAULT_SERIAL_PORT})",
    )
    p.add_argument(
        "-b",
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help=f"Baud rate (default: {DEFAULT_BAUD})",
    )
    p.add_argument(
        "--listen-host",
        default=DEFAULT_LISTEN_HOST,
        help=f"Proxy bind address (default: {DEFAULT_LISTEN_HOST})",
    )
    p.add_argument(
        "-l",
        "--listen-port",
        type=int,
        default=DEFAULT_LISTEN_PORT,
        help=f"Proxy TCP/WebSocket port (default: {DEFAULT_LISTEN_PORT})",
    )
    p.add_argument(
        "-o",
        "--out",
        default=None,
        help="Output log file path (default: <port>_proxy_<YYYYMMDD_HHMMSS>.txt next to script)",
    )
    p.add_argument(
        "--raw-out",
        default=None,
        help="Optional file path to also dump raw unmodified serial RX bytes",
    )
    p.add_argument(
        "--list",
        action="store_true",
        help="List currently available serial ports and exit",
    )
    return p.parse_args()


def main():
    args = parse_args()
    if args.list:
        print(f"Available serial ports: {format_available_ports()}")
        return

    if serial is None:
        print("ERROR: pyserial is required. Install with: pip install pyserial", file=sys.stderr)
        sys.exit(1)

    initial_port = args.positional_port or args.port
    log_path = args.out or make_default_log_path(initial_port)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.listen_host, args.listen_port))
    srv.listen(4)
    srv.settimeout(0.5)

    out_dir = os.path.dirname(os.path.abspath(log_path))
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    raw_f = open(args.raw_out, "wb") if args.raw_out else None
    with open(log_path, "wb") as log_f:
        proxy = SerialBetaflightProxy(
            initial_port,
            args.baud,
            log_f,
            raw_file=raw_f,
            echo_stdout=True,
        )
        proxy.log_event(
            f"[PROXY READY] Target={initial_port} @ {args.baud} | Log={log_path}"
        )
        proxy.log_event(
            f"[CONNECT URL] Betaflight Web App (Manual Mode): ws://{args.listen_host}:{args.listen_port}"
        )
        proxy.log_event(
            f"[CONNECT URL] Betaflight Desktop (Manual):      tcp://{args.listen_host}:{args.listen_port}"
        )
        proxy.log_event(
            "[SWITCH PORTS] Type :ports or :port COM5 (no ':' needed while waiting) | Ctrl+C to quit"
        )

        rx_thread = threading.Thread(target=proxy.serial_reader_loop, daemon=True)
        kbd_thread = threading.Thread(target=proxy.keyboard_loop, daemon=True)
        rx_thread.start()
        kbd_thread.start()

        try:
            while not proxy.stop_event.is_set():
                try:
                    conn, addr = srv.accept()
                except socket.timeout:
                    continue
                t = threading.Thread(target=proxy.handle_client, args=(conn, addr), daemon=True)
                t.start()
        except KeyboardInterrupt:
            proxy.log_event("[STOPPING] Ctrl+C received, closing proxy...")
        finally:
            proxy.stop_event.set()
            proxy.set_client(None)
            try:
                srv.close()
            except OSError:
                pass
            rx_thread.join(timeout=1.5)
            if raw_f is not None:
                raw_f.close()
            print(f"\nSaved log to {log_path}")


if __name__ == "__main__":
    main()
