#!/usr/bin/env python3
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Minimal RFB 3.8 test server (Raw, or ZRLE when asked) for manual/visual testing of Marconi without a real VNC server.
#
# Serves an animated test pattern using security type None and Raw encoding (plus CopyRect when the client
# asks for it), and logs every client message (pointer, key, clipboard) so input handling can be checked.
#
#   python Tools/fake_vnc_server.py [--port 5999] [--size 1024x768]
#   then connect Marconi to 127.0.0.1::5999
#
# Type "resize WxH" or "cut <text>" or "bell" on the server's stdin to exercise those messages, or pass
# --cut-on-connect "text" to send clipboard text as soon as a client connects.

import argparse
import socket
import struct
import sys
import threading
import time
import zlib

ENCODING_NAMES = {0: "Raw", 1: "CopyRect", 2: "RRE", 5: "Hextile", 7: "Tight", 16: "ZRLE", -223: "DesktopSize",
                  -224: "LastRect", -239: "Cursor", -308: "ExtendedDesktopSize"}


class Session:
    def __init__(self, conn, width, height):
        self.conn = conn
        self.width = width
        self.height = height
        self.lock = threading.Lock()
        self.update_requested = threading.Event()
        self.full_update = True
        self.encodings = []
        self.frame = 0
        self.pending_resize = None
        self.alive = True

    def recv_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.conn.recv(n - len(data))
            if not chunk:
                raise ConnectionError("client closed")
            data += chunk
        return data

    def send(self, data):
        with self.lock:
            self.conn.sendall(data)

    def handshake(self):
        self.conn.sendall(b"RFB 003.008\n")
        print("client version:", self.recv_exact(12))
        self.conn.sendall(bytes([1, 1]))  # one security type: None
        print("security choice:", self.recv_exact(1)[0])
        self.conn.sendall(struct.pack(">I", 0))
        print("shared:", self.recv_exact(1)[0])
        name = b"Marconi fake server"
        pixel_format = struct.pack(">BBBBHHHBBBxxx", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
        self.conn.sendall(struct.pack(">HH", self.width, self.height) + pixel_format + struct.pack(">I", len(name)) + name)

    def pixel(self, x, y):
        # Gradient background, checkerboard band, and a moving white square.
        r = (x * 255) // max(self.width - 1, 1)
        g = (y * 255) // max(self.height - 1, 1)
        b = 128
        if 40 <= y < 80:
            r = g = b = 255 if ((x // 20) + (y // 20)) % 2 == 0 else 0
        square = (self.frame * 8) % max(self.width - 64, 1)
        if square <= x < square + 64 and self.height // 2 <= y < self.height // 2 + 64:
            r = g = b = 255
        return r, g, b

    def rect(self, x0, y0, w, h):
        # ZRLE when the client asked for it (like macOS), otherwise Raw.
        if 16 in getattr(self, "encodings", []):
            return self.zrle_rect(x0, y0, w, h)
        return self.raw_rect(x0, y0, w, h)

    def zrle_rect(self, x0, y0, w, h):
        # Every 64x64 tile as a raw tile of 3-byte CPIXELs (B, G, R), through one zlib stream per connection.
        if not hasattr(self, "zlib_stream"):
            self.zlib_stream = zlib.compressobj()
        tiles = bytearray()
        for ty in range(y0, y0 + h, 64):
            for tx in range(x0, x0 + w, 64):
                tiles.append(0)
                for y in range(ty, min(ty + 64, y0 + h)):
                    for x in range(tx, min(tx + 64, x0 + w)):
                        r, g, b = self.pixel(x, y)
                        tiles += bytes((b, g, r))
        data = self.zlib_stream.compress(bytes(tiles)) + self.zlib_stream.flush(zlib.Z_SYNC_FLUSH)
        return struct.pack(">HHHHiI", x0, y0, w, h, 16, len(data)) + data

    def raw_rect(self, x0, y0, w, h):
        out = bytearray(struct.pack(">HHHHi", x0, y0, w, h, 0))
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                r, g, b = self.pixel(x, y)
                out += bytes((b, g, r, 0))  # little-endian 0x00RRGGBB, as the client asked
        return bytes(out)

    def updater(self):
        while self.alive:
            if not self.update_requested.wait(0.1):
                continue
            time.sleep(0.05)  # ~20 fps
            self.update_requested.clear()
            rects = []
            if self.pending_resize is not None:
                self.width, self.height = self.pending_resize
                self.pending_resize = None
                rects.append(struct.pack(">HHHHi", 0, 0, self.width, self.height, -223))
                self.full_update = True
            self.frame += 1
            if self.full_update:
                rects.append(self.rect(0, 0, self.width, self.height))
                self.full_update = False
            else:
                # Only the band the square moves in.
                y = self.height // 2
                rects.append(self.rect(0, y, self.width, min(64, self.height - y)))
            try:
                self.send(struct.pack(">BxH", 0, len(rects)) + b"".join(rects))
            except OSError:
                return

    def message_loop(self):
        while True:
            message_type = self.recv_exact(1)[0]
            if message_type == 0:
                self.recv_exact(3)
                fmt = struct.unpack(">BBBBHHHBBBxxx", self.recv_exact(16))
                print("SetPixelFormat", fmt)
            elif message_type == 2:
                self.recv_exact(1)
                (count,) = struct.unpack(">H", self.recv_exact(2))
                self.encodings = list(struct.unpack(">%di" % count, self.recv_exact(4 * count)))
                print("SetEncodings", [ENCODING_NAMES.get(e, e) for e in self.encodings])
            elif message_type == 3:
                incremental, x, y, w, h = struct.unpack(">BHHHH", self.recv_exact(9))
                if not incremental:
                    self.full_update = True
                self.update_requested.set()
            elif message_type == 4:
                down, keysym = struct.unpack(">Bxxi", self.recv_exact(7))
                print("Key %s keysym=0x%04x %r" % ("down" if down else "up  ", keysym,
                                                   chr(keysym) if 32 <= keysym < 127 else ""))
            elif message_type == 5:
                buttons, x, y = struct.unpack(">BHH", self.recv_exact(5))
                print("Pointer buttons=%s x=%d y=%d" % (format(buttons, "08b"), x, y))
            elif message_type == 6:
                self.recv_exact(3)
                (length,) = struct.unpack(">i", self.recv_exact(4))
                text = self.recv_exact(abs(length))
                print("ClientCutText", text.decode("latin-1"))
            else:
                print("Unknown client message", message_type)
                return


def console(session):
    for line in sys.stdin:
        parts = line.strip().split(" ", 1)
        if parts[0] == "resize" and len(parts) == 2:
            w, h = (int(v) for v in parts[1].split("x"))
            session.pending_resize = (w, h)
            session.update_requested.set()
        elif parts[0] == "cut" and len(parts) == 2:
            text = parts[1].encode("latin-1", "replace")
            session.send(struct.pack(">Bxxxi", 3, len(text)) + text)
        elif parts[0] == "bell":
            session.send(bytes([2]))


def flipper(session, sizes, seconds):
    index = 0
    while session.alive:
        time.sleep(seconds)
        index = 1 - index
        print("resizing to %dx%d" % sizes[index], flush=True)
        session.pending_resize = sizes[index]
        session.update_requested.set()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5999)
    parser.add_argument("--size", default="1024x768")
    parser.add_argument("--cut-on-connect", default=None, help="send this text as ServerCutText after the handshake")
    parser.add_argument("--flip", default=None, metavar="WxH",
                        help="switch between --size and this size every --flip-every seconds (like a Mac locking)")
    parser.add_argument("--flip-every", type=float, default=3.0)
    args = parser.parse_args()
    width, height = (int(v) for v in args.size.split("x"))

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", args.port))
    listener.listen(1)
    print("listening on 127.0.0.1:%d (%dx%d)" % (args.port, width, height), flush=True)
    while True:
        conn, peer = listener.accept()
        print("client connected from", peer, flush=True)
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        session = Session(conn, width, height)
        try:
            session.handshake()
            if args.cut_on_connect:
                text = args.cut_on_connect.encode("latin-1", "replace")
                session.send(struct.pack(">Bxxxi", 3, len(text)) + text)
            threading.Thread(target=session.updater, daemon=True).start()
            threading.Thread(target=console, args=(session,), daemon=True).start()
            if args.flip:
                sizes = [(width, height), tuple(int(v) for v in args.flip.split("x"))]
                threading.Thread(target=flipper, args=(session, sizes, args.flip_every), daemon=True).start()
            session.message_loop()
        except (ConnectionError, OSError) as error:
            print("session ended:", error, flush=True)
        finally:
            session.alive = False
            conn.close()


if __name__ == "__main__":
    main()
