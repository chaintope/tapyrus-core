#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Dummy Tor control port for testing.

Answers PROTOCOLINFO, AUTHENTICATE and ADD_ONION as Tor does with NULL
authentication, and records the ADD_ONION commands, so a test can check
where a node asks Tor to forward its onion service.
"""

import queue
import re
import socket
import threading

# A syntactically valid v3 service ID (56 base32 characters) and private key.
SERVICE_ID = "a" * 56
PRIVATE_KEY = "ED25519-V3:" + "A" * 86 + "=="


class TorControlConnection():
    def __init__(self, serv, conn):
        self.serv = serv
        self.conn = conn

    def handle(self):
        with self.conn:
            buffer = b""
            while True:
                data = self.conn.recv(4096)
                if not data:
                    return
                buffer += data
                while b"\r\n" in buffer:
                    line, buffer = buffer.split(b"\r\n", 1)
                    self.conn.sendall(self.reply(line.decode("ascii")).encode("ascii"))

    def reply(self, command):
        if command.startswith("PROTOCOLINFO"):
            return '250-PROTOCOLINFO 1\r\n250-AUTH METHODS=NULL\r\n250-VERSION Tor="0.4.8.0"\r\n250 OK\r\n'
        if command == "AUTHENTICATE":
            return "250 OK\r\n"
        if command.startswith("ADD_ONION "):
            self.serv.add_onion_commands.put(command)
            return "250-ServiceID={}\r\n250-PrivateKey={}\r\n250 OK\r\n".format(SERVICE_ID, PRIVATE_KEY)
        return "510 Unrecognized command\r\n"


class TorControlServer():
    def __init__(self, addr):
        self.addr = addr
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(addr)
        self.s.listen(5)
        self.running = False
        self.thread = None
        self.add_onion_commands = queue.Queue()

    def run(self):
        while self.running:
            (sockconn, _) = self.s.accept()
            if self.running:
                conn = TorControlConnection(self, sockconn)
                thread = threading.Thread(None, conn.handle)
                thread.daemon = True
                thread.start()
            else:
                sockconn.close()

    def start(self):
        assert not self.running
        self.running = True
        self.thread = threading.Thread(None, self.run)
        self.thread.daemon = True
        self.thread.start()

    def stop(self):
        self.running = False
        # Connect to self to end the run loop.
        with socket.create_connection(self.addr):
            pass
        self.thread.join()
        self.s.close()

    def clear(self):
        """Forget ADD_ONION commands received so far."""
        while not self.add_onion_commands.empty():
            self.add_onion_commands.get_nowait()

    def wait_for_onion_target(self, timeout=60):
        """Return the target of the next ADD_ONION command, e.g. '127.0.0.1:2358'."""
        command = self.add_onion_commands.get(timeout=timeout)
        match = re.search(r" Port=\d+,(\S+)$", command)
        assert match, "Unexpected ADD_ONION command: " + command
        return match.group(1)
