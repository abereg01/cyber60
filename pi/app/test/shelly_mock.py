#!/usr/bin/env python3
"""A stand-in for the Shelly Cloud's Control API v2, for pi/tools/shelly-test.sh
mock: two Gen3-style lamps (cct:0), the key "test-key", and a log of every
request. On SIGTERM it checks what lights.c sent and exits 1 if anything is
wrong: requests closer than 1 s apart (Shelly's limit), a wrong key, or a set
body that isn't what the page asked for last.

    shelly_mock.py <port> <expected last brightness of lamp 0>
"""

import json
import signal
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

PORT = int(sys.argv[1])
EXPECT = int(sys.argv[2])
lamps = {"a0b1c2d3e4f5": {"output": False, "brightness": 50}, "0a1b2c3d4e5f": {"output": True, "brightness": 80}}
log = []  # (time, path, body)
bad = []


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, obj):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        u = urlparse(self.path)
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        log.append((time.monotonic(), u.path, body))
        if parse_qs(u.query).get("auth_key") != ["test-key"]:
            bad.append("wrong auth_key")
            return self.reply(401, {"error": "UNAUTHORIZED"})
        if u.path == "/v2/devices/api/get":
            out = [{"id": i, "type": "SNSN-0024X", "gen": 3, "online": 1, "status": {"cct:0": dict(lamps[i], id=0)}}
                   for i in body.get("ids", []) if i in lamps]
            return self.reply(200, out)
        if u.path == "/v2/devices/api/set/light":
            lamp = lamps.get(body.get("id"))
            if lamp is None:
                return self.reply(404, {"error": "DEVICE_NOT_FOUND"})
            if "on" in body:
                lamp["output"] = body["on"]
            if "brightness" in body:
                lamp["brightness"] = body["brightness"]
            return self.reply(200, {})
        self.reply(404, {"error": "NOT_FOUND"})


def finish(*_):
    sets = [b for _, p, b in log if p.endswith("/set/light")]
    gaps = [b[0] - a[0] for a, b in zip(log, log[1:])]
    print(f"mock: {len(log)} requests, {len(sets)} sets, closest gap {min(gaps, default=9):.2f} s")
    for t, p, b in log:
        print(f"mock:   {t - log[0][0]:6.2f}  {p.rsplit('/', 1)[-1]:5}  {json.dumps(b)}")
    if any(g < 1.0 for g in gaps):
        bad.append("two requests less than 1 s apart")
    if sets and sets[-1].get("brightness") != EXPECT:
        bad.append(f"last set brightness {sets[-1].get('brightness')}, wanted {EXPECT}")
    if EXPECT and len(sets) > 4:
        bad.append(f"{len(sets)} sets for one drag: the wishes didn't merge")
    print("mock: " + ("; ".join(bad) if bad else "all good"))
    sys.exit(1 if bad else 0)


signal.signal(signal.SIGTERM, finish)
HTTPServer(("127.0.0.1", PORT), H).serve_forever()
