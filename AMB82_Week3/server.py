#!/usr/bin/env python3
"""AMB82 quiz local bridge: serial -> stable gesture -> browser SSE. Python 3.9+."""
import argparse
import json
import queue
import re
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent / 'web'
PATTERN = re.compile(r'\[QUIZ\]\s*fingers\s*=\s*([0-5])\s*,\s*letter\s*=\s*([ABCD-])', re.I)
MAPPING = 'ABCD'

class State:
    """Emit each stable gesture once; rearm on a different stable gesture or hand absence."""
    def __init__(self, stable=2, gap=0.0, release=1.5):
        self.lock = threading.RLock()
        self.listeners = set()
        self.history = deque(maxlen=stable)
        self.stable = stable
        self.release = release
        self.last_seen = 0.0
        self.last_emitted = None
        self.connection = 'waiting'
        self.event_id = 0

    def publish(self, kind, **data):
        with self.lock:
            self.event_id += 1
            payload = {'type': kind, 'id': self.event_id, 'time': round(time.time(), 3), **data}
            for listener in list(self.listeners):
                try: listener.put_nowait(payload)
                except queue.Full:
                    try: listener.get_nowait()
                    except queue.Empty: pass
                    try: listener.put_nowait(payload)
                    except queue.Full: pass

    def set_connection(self, value):
        with self.lock:
            if self.connection != value:
                self.connection = value
                self.publish('connection', status=value)

    def release_if_idle(self):
        with self.lock:
            if self.last_seen and time.monotonic() - self.last_seen >= self.release:
                if self.last_emitted is not None or self.history:
                    self.last_emitted = None
                    self.history.clear()
                    self.publish('released')

    def ingest(self, fingers, source='serial'):
        now = time.monotonic()
        with self.lock:
            if self.last_seen and now - self.last_seen >= self.release:
                self.last_emitted = None
                self.history.clear()
            self.last_seen = now
            self.history.append(fingers)
            self.publish('preview', fingers=fingers, samples=len(self.history), required=self.stable, source=source)
            if len(self.history) < self.stable or len(set(self.history)) != 1:
                return
            if self.last_emitted == fingers:
                return
            self.last_emitted = fingers
            self.publish('gesture', fingers=fingers, source=source)


def serial_worker(state, port, baud):
    try:
        import serial
    except ImportError:
        state.set_connection('pyserial_missing')
        print('[ERROR] Install pyserial: python -m pip install pyserial', flush=True)
        return
    while True:
        try:
            print(f'[SERIAL] Opening {port} at {baud} ...', flush=True)
            with serial.Serial(port, baud, timeout=0.5) as ser:
                state.set_connection('connected')
                while True:
                    raw = ser.readline().decode('utf-8', errors='replace')
                    match = PATTERN.search(raw)
                    if match:
                        finger = int(match.group(1))
                        letter = match.group(2).upper()
                        expected = MAPPING[finger-1] if 1 <= finger <= 4 else '-'
                        if expected == letter:
                            state.ingest(finger)
                    state.release_if_idle()
        except (OSError, serial.SerialException) as exc:
            state.set_connection('disconnected')
            print(f'[SERIAL] {exc}; retrying in 3 seconds. Close Arduino Serial Monitor.', flush=True)
            time.sleep(3)


def create_handler(state, demo):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            if not self.path.startswith('/events'):
                super().log_message(fmt, *args)

        def send_json(self, data, code=200):
            body = json.dumps(data, ensure_ascii=False).encode('utf-8')
            self.send_response(code)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urlsplit(self.path).path
            if path == '/api/status':
                with state.lock:
                    return self.send_json({'connection': state.connection, 'demo': demo, 'stable': state.stable, 'last_emitted': state.last_emitted})
            if path == '/events':
                listener = queue.Queue(maxsize=50)
                with state.lock:
                    state.listeners.add(listener)
                    status = state.connection
                try:
                    self.send_response(200)
                    self.send_header('Content-Type', 'text/event-stream; charset=utf-8')
                    self.send_header('Cache-Control', 'no-cache, no-transform')
                    self.send_header('Connection', 'keep-alive')
                    self.end_headers()
                    self.wfile.write(('data: '+json.dumps({'type':'connection','status':status})+'\n\n').encode())
                    self.wfile.flush()
                    while True:
                        try:
                            event = listener.get(timeout=10)
                            message = 'data: '+json.dumps(event, ensure_ascii=False)+'\n\n'
                        except queue.Empty:
                            message = ': heartbeat\n\n'
                        self.wfile.write(message.encode('utf-8'))
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError, OSError):
                    pass
                finally:
                    with state.lock: state.listeners.discard(listener)
                return
            if path == '/': path = '/index.html'
            if path not in ('/index.html', '/style.css', '/app.js'):
                return self.send_error(404)
            target = ROOT / path.lstrip('/')
            body = target.read_bytes()
            mime = {'.html':'text/html', '.css':'text/css', '.js':'application/javascript'}[target.suffix]
            self.send_response(200)
            self.send_header('Content-Type', mime+'; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            path = urlsplit(self.path).path
            if path != '/api/demo' or not demo:
                return self.send_json({'error':'demo disabled'}, 403)
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if length > 1024: return self.send_json({'error':'too large'}, 413)
                data = json.loads(self.rfile.read(length))
                fingers = data.get('fingers')
                if type(fingers) is not int or fingers not in range(6): return self.send_json({'error':'invalid fingers'}, 400)
                # Emulate a sustained gesture, then release it for next demo action.
                # Demo button represents a new deliberate gesture event.
                with state.lock:
                    state.history.clear()
                    state.last_emitted = None
                for _ in range(state.stable): state.ingest(fingers, 'demo')
                return self.send_json({'ok':True})
            except (ValueError, TypeError, json.JSONDecodeError):
                return self.send_json({'error':'invalid JSON'}, 400)
    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='COM3', help='Serial port (Windows: COM3)')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--http-port', type=int, default=8000)
    parser.add_argument('--stable', type=int, default=2, help='Consecutive matching samples (board emits about 1/sec)')
    parser.add_argument('--demo', action='store_true', help='Use demo buttons without board')
    args = parser.parse_args()
    if args.stable < 1: parser.error('--stable must be >=1')
    state = State(stable=args.stable)
    if args.demo:
        state.set_connection('demo')
    else:
        threading.Thread(target=serial_worker, args=(state,args.port,args.baud), daemon=True).start()
    threading.Thread(target=lambda: idle_loop(state), daemon=True).start()
    server = ThreadingHTTPServer(('127.0.0.1', args.http_port), create_handler(state, args.demo))
    server.daemon_threads = True
    print(f'[WEB] Open http://127.0.0.1:{args.http_port} (demo={args.demo})', flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close()

def idle_loop(state):
    while True:
        state.release_if_idle()
        time.sleep(0.15)

if __name__ == '__main__': main()
