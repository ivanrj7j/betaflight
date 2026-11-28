#!/usr/bin/env python3
"""
COM9 Serial Monitor + Dual-Mode (WebSocket & Raw TCP) Betaflight Configurator Proxy.

Owns the hardware serial port exclusively (no COM port conflict), logs all RX/TX traffic
to a file in real time, prints human-readable telemetry/MSP traces to stdout, and exposes a
loopback proxy that accepts BOTH:
  - Betaflight Web App PWA (Chrome/Edge at master.app.betaflight.com / app.betaflight.com):
      Enable "Manual connection mode" -> connect to:  ws://127.0.0.1:5761
  - Standalone Desktop Betaflight Configurator:
      Select manual port -> connect to:               tcp://127.0.0.1:5761
"""

import argparse
import base64
import hashlib
import os
import select
import socket
import struct
import sys
import threading
import time

try:
    import serial
except ImportError:
    serial = None

try:
    import msvcrt
except ImportError:
    msvcrt = None


WS_MAGIC_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
DEFAULT_SERIAL_PORT = "COM9"
DEFAULT_BAUD = 115200
DEFAULT_LISTEN_HOST = "127.0.0.1"
DEFAULT_LISTEN_PORT = 5761
DEFAULT_LOG_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "com9_proxy_log.txt"
)


def now_ts():
    """Return local wall-clock timestamp with millisecond precision."""
    t = time.time()
    lt = time.localtime(t)
    ms = int((t - int(t)) * 1000)
    return time.strftime("%H:%M:%S", lt) + f".{ms:03d}"


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
        return False

    accept_raw = (key + WS_MAGIC_GUID).encode("ascii")
    accept_b64 = base64.b64encode(hashlib.sha1(accept_raw).digest()).decode("ascii")
    resp = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {accept_b64}\r\n"
        "\r\n"
    ).encode("ascii")
    sock.sendall(resp)
    return True


def read_http_header_block(sock, initial_timeout=0.25):
    """Peek first bytes to distinguish HTTP WebSocket Upgrade from raw TCP stream."""
    ready, _, _ = select.select([sock], [], [], initial_timeout)
    if not ready:
        return "tcp", b""

    peeked = sock.recv(16, socket.MSG_PEEK)
    if not peeked:
        return "eof", b""

    if peeked.startswith(b"GET ") or peeked.startswith(b"get "):
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
    """Bridges one physical serial port to a log file and sequential TCP/WS clients."""

    def __init__(self, ser, log_file, raw_file=None, echo_stdout=True):
        self.ser = ser
        self.log_file = log_file
        self.raw_file = raw_file
        self.echo_stdout = echo_stdout

        self.stop_event = threading.Event()
        self.log_lock = threading.Lock()
        self.ser_write_lock = threading.Lock()
        self.client_lock = threading.Lock()
        self.active_client = None

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
                entry = f"[{now_ts()}] [RX <- SERIAL {len(data)}B TEXT]\n{text}"
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
            return
        self.log_traffic(f"TX <- {source_label}", data)
        with self.ser_write_lock:
            self.ser.write(data)
            self.ser.flush()

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

    def serial_reader_loop(self):
        while not self.stop_event.is_set():
            try:
                pending = self.ser.in_waiting
                chunk = self.ser.read(pending if pending > 0 else 1)
                if not chunk:
                    continue

                # Coalesce short burst (e.g., line of text or single MSP reply)
                buf = bytearray(chunk)
                deadline = time.monotonic() + 0.012
                while time.monotonic() < deadline and len(buf) < 4096:
                    more = self.ser.in_waiting
                    if more > 0:
                        buf.extend(self.ser.read(min(more, 4096 - len(buf))))
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
                    self.log_event(f"[ERROR] Serial read error: {exc}")
                    time.sleep(0.05)

    def keyboard_loop(self):
        if not msvcrt or not sys.stdin or not sys.stdin.isatty():
            return
        while not self.stop_event.is_set():
            try:
                if msvcrt.kbhit():
                    ch = msvcrt.getch()
                    if ch == b"\x03":
                        self.stop_event.set()
                        return
                    if ch in (b"\x00", b"\xe0"):
                        msvcrt.getch()
                        continue
                    self.write_serial(ch, source_label="KBD")
                else:
                    time.sleep(0.02)
            except Exception:
                time.sleep(0.05)

    def handle_client(self, conn, addr):
        try:
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass

        try:
            mode, header_bytes = read_http_header_block(conn)
        except OSError:
            conn.close()
            return

        if mode == "eof":
            conn.close()
            return

        if mode == "ws":
            if not try_ws_handshake(conn, header_bytes):
                conn.close()
                return

        session = ClientSession(conn, addr, mode)
        self.set_client(session)
        self.log_event(f"[CLIENT CONNECTED] {addr[0]}:{addr[1]} via {mode.upper()}")

        try:
            while not self.stop_event.is_set() and not session.closed:
                if mode == "ws":
                    opcode, payload = read_ws_frame(conn)
                    if opcode is None:
                        break
                    if opcode == 0x8:  # Close
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
                        break
                    self.write_serial(data, source_label="TCP")
        except OSError as exc:
            if not self.stop_event.is_set():
                self.log_event(f"[CLIENT ERROR] {addr[0]}:{addr[1]}: {exc}")
        finally:
            self.clear_client(session)
            session.close()
            self.log_event(f"[CLIENT DISCONNECTED] {addr[0]}:{addr[1]}")


def parse_args():
    p = argparse.ArgumentParser(
        description="COM9 Serial Monitor + Dual-Mode (WS & TCP) Proxy for Betaflight Configurator."
    )
    p.add_argument("--port", default=DEFAULT_SERIAL_PORT, help="Serial port (default: COM9)")
    p.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate (default: 115200)")
    p.add_argument(
        "--listen-host",
        default=DEFAULT_LISTEN_HOST,
        help="Proxy bind address (default: 127.0.0.1)",
    )
    p.add_argument(
        "--listen-port",
        type=int,
        default=DEFAULT_LISTEN_PORT,
        help="Proxy TCP/WebSocket port (default: 5761)",
    )
    p.add_argument(
        "--out",
        default=DEFAULT_LOG_PATH,
        help=f"Output diagnostic log path (default: {DEFAULT_LOG_PATH})",
    )
    p.add_argument(
        "--raw-out",
        default=None,
        help="Optional file path to also dump raw unmodified serial RX bytes",
    )
    return p.parse_args()


def main():
    args = parse_args()
    if serial is None:
        print("ERROR: pyserial is required. Install with: pip install pyserial", file=sys.stderr)
        sys.exit(1)

    ser = serial.Serial(args.port, args.baud, timeout=0.01, write_timeout=1.0)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.listen_host, args.listen_port))
    srv.listen(4)
    srv.settimeout(0.5)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    raw_f = open(args.raw_out, "wb") if args.raw_out else None
    with open(args.out, "wb") as log_f:
        proxy = SerialBetaflightProxy(ser, log_f, raw_file=raw_f, echo_stdout=True)
        proxy.log_event(
            f"[PROXY READY] Serial={args.port} @ {args.baud} | Log={args.out}"
        )
        proxy.log_event(
            f"[CONNECT URL] Betaflight Web App (Manual Mode): ws://{args.listen_host}:{args.listen_port}"
        )
        proxy.log_event(
            f"[CONNECT URL] Betaflight Desktop (Manual):      tcp://{args.listen_host}:{args.listen_port}"
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
            try:
                ser.close()
            except OSError:
                pass
            if raw_f is not None:
                raw_f.close()
            rx_thread.join(timeout=1.0)
            print(f"\nSaved log to {args.out}")


if __name__ == "__main__":
    main()
