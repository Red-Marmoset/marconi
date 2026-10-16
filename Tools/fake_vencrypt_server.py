#!/usr/bin/env python3
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Minimal RFB 3.8 server that requires an encrypted connection (VeNCrypt over TLS), for checking Marconi's
# encryption end to end against a real TLS stack. It serves one grey frame and logs what the client sends.
#
#   openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 30 -subj "/CN=test"
#   python Tools/fake_vencrypt_server.py --port 5998 --cert cert.pem --key key.pem [--subtype SUBTYPE]
#   then connect Marconi to 127.0.0.1::5998. SUBTYPE is x509vnc (default), x509plain, x509none, tlsvnc, tlsplain or
#   tlsnone; the password is "secret" for the *vnc subtypes and any user name and password work for *plain. The *vnc
#   password is only checked when pycryptodome (pip install pycryptodome) is installed; otherwise any answer passes.
#
# Exit codes: 0 once a client has logged in, asked for the screen and sent a key event; 1 on any protocol error.

import argparse
import socket
import ssl
import struct
import sys

SUBTYPES = {"plain": 256, "tlsnone": 257, "tlsvnc": 258, "tlsplain": 259, "x509none": 260, "x509vnc": 261,
            "x509plain": 262}


def recv_exact(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            raise ConnectionError("client closed")
        data += chunk
    return data


def des_response(password, challenge):
    # VNC authentication: DES with the bit-reversed password as key, via Marconi's own test knowledge of the
    # algorithm; here we simply check the client's answer against what the reference DES gives.
    from Crypto.Cipher import DES  # pycryptodome; only needed for the *vnc subtypes
    key = bytes(int("{:08b}".format(b)[::-1], 2) for b in password.ljust(8, "\0")[:8].encode("latin-1"))
    return DES.new(key, DES.MODE_ECB).encrypt(challenge)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5998)
    parser.add_argument("--cert", default="cert.pem")
    parser.add_argument("--key", default="key.pem")
    parser.add_argument("--subtype", default="x509vnc", choices=sorted(SUBTYPES))
    parser.add_argument("--password", default="secret")
    parser.add_argument("--once", action="store_true", help="exit after one client")
    args = parser.parse_args()
    subtype = SUBTYPES[args.subtype]
    anonymous = args.subtype.startswith("tls")

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    if anonymous:
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.set_ciphers("aNULL:@SECLEVEL=0")
    else:
        context.load_cert_chain(args.cert, args.key)

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", args.port))
    listener.listen(1)
    print("listening on 127.0.0.1:%d, VeNCrypt subtype %s" % (args.port, args.subtype), flush=True)

    while True:
        conn, peer = listener.accept()
        print("client connected from", peer, flush=True)
        try:
            conn.sendall(b"RFB 003.008\n")
            print("client version:", recv_exact(conn, 12), flush=True)
            conn.sendall(bytes([1, 19]))  # the only security type: VeNCrypt
            chosen = recv_exact(conn, 1)[0]
            if chosen != 19:
                raise ValueError("client chose security type %d" % chosen)
            conn.sendall(bytes([0, 2]))
            version = recv_exact(conn, 2)
            print("client VeNCrypt version: %d.%d" % (version[0], version[1]), flush=True)
            if version != bytes([0, 2]):
                conn.sendall(bytes([1]))
                raise ValueError("unsupported VeNCrypt version")
            conn.sendall(bytes([0]))
            offered = [256, subtype]  # plain text alongside: the client must never pick it
            conn.sendall(bytes([len(offered)]) + b"".join(struct.pack(">I", s) for s in offered))
            (picked,) = struct.unpack(">I", recv_exact(conn, 4))
            print("client picked subtype", picked, flush=True)
            if picked != subtype:
                conn.sendall(bytes([0]))
                raise ValueError("client picked an unexpected subtype")
            conn.sendall(bytes([1]))

            tls = context.wrap_socket(conn, server_side=True)
            print("TLS established:", tls.version(), tls.cipher()[0], flush=True)

            if args.subtype.endswith("vnc"):
                challenge = bytes(range(16))
                tls.sendall(challenge)
                response = recv_exact(tls, 16)
                try:
                    expected = des_response(args.password, challenge)
                except ImportError:
                    expected = None
                    print("(pycryptodome not installed: not checking the password)", flush=True)
                if expected is not None and response != expected:
                    tls.sendall(struct.pack(">II", 1, 15) + b"Wrong password.")
                    raise ValueError("wrong password")
            elif args.subtype.endswith("plain"):
                ulen, plen = struct.unpack(">II", recv_exact(tls, 8))
                username = recv_exact(tls, ulen).decode("utf-8")
                password = recv_exact(tls, plen).decode("utf-8")
                print("plain login: user %r, password of %d characters" % (username, len(password)), flush=True)
            tls.sendall(struct.pack(">I", 0))  # SecurityResult OK

            print("shared:", recv_exact(tls, 1)[0], flush=True)
            name = b"Encrypted fake server"
            pixel_format = struct.pack(">BBBBHHHBBBxxx", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
            tls.sendall(struct.pack(">HH", 64, 48) + pixel_format + struct.pack(">I", len(name)) + name)

            saw_request = False
            saw_key = False
            while not (saw_request and saw_key):
                message_type = recv_exact(tls, 1)[0]
                if message_type == 0:
                    recv_exact(tls, 19)
                    print("SetPixelFormat", flush=True)
                elif message_type == 2:
                    recv_exact(tls, 1)
                    (count,) = struct.unpack(">H", recv_exact(tls, 2))
                    recv_exact(tls, 4 * count)
                    print("SetEncodings (%d)" % count, flush=True)
                elif message_type == 3:
                    recv_exact(tls, 9)
                    if not saw_request:
                        saw_request = True
                        rect = struct.pack(">HHHHi", 0, 0, 64, 48, 0) + bytes([0x80, 0x80, 0x80, 0]) * (64 * 48)
                        tls.sendall(struct.pack(">BxH", 0, 1) + rect)
                        print("sent one raw frame", flush=True)
                elif message_type == 4:
                    down, keysym = struct.unpack(">Bxxi", recv_exact(tls, 7))
                    print("Key %s keysym=0x%04x" % ("down" if down else "up", keysym), flush=True)
                    saw_key = True
                elif message_type == 5:
                    recv_exact(tls, 5)
                elif message_type == 6:
                    recv_exact(tls, 3)
                    (length,) = struct.unpack(">i", recv_exact(tls, 4))
                    recv_exact(tls, abs(length))
                else:
                    raise ValueError("unknown client message %d" % message_type)
            print("SUCCESS: encrypted session worked", flush=True)
            tls.close()
            if args.once:
                return 0
        except (ConnectionError, OSError, ValueError, ssl.SSLError) as error:
            print("session ended:", error, flush=True)
            if args.once:
                return 1
        finally:
            conn.close()


if __name__ == "__main__":
    sys.exit(main())
