from __future__ import annotations

import socket
import urllib.error
import urllib.request
from pathlib import Path
from urllib.parse import urlsplit

from flask import Flask, jsonify, request, send_from_directory

try:
    from .voice_control import DEFAULT_AMEBA_URL, command_to_path, parse_led_state
except ImportError:
    from voice_control import DEFAULT_AMEBA_URL, command_to_path, parse_led_state


WEB_DIRECTORY = Path(__file__).resolve().parent.parent / "web"
CONTROL_PATHS = {"/blue/on", "/blue/off", "/green/on", "/green/off", "/blink"}
COMMAND_LABELS = {
    "/blue/on": "左側 LED ON",
    "/blue/off": "左側 LED OFF",
    "/green/on": "右側 LED ON",
    "/green/off": "右側 LED OFF",
    "/blink": "兩燈同時閃爍三次",
}
DEVICE_TIMEOUT_SECONDS = 5


class ProxyError(Exception):
    def __init__(self, message: str, status_code: int = 502):
        super().__init__(message)
        self.status_code = status_code


def validate_device_url(value: str | None) -> str:
    if not value:
        raise ValueError("請輸入 AMB82-MINI 網址")
    value = value.strip()
    parsed = urlsplit(value)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise ValueError("網址格式無效，請使用 http:// 或 https:// 開頭")
    if parsed.username or parsed.password or parsed.path not in {"", "/"} or parsed.query or parsed.fragment:
        raise ValueError("請只輸入裝置基底網址，不要包含路徑、帳密或查詢參數")
    try:
        parsed.port
    except ValueError as error:
        raise ValueError("網址中的 Port 無效") from error
    return value.rstrip("/")


def request_device_state(device_url: str, path: str) -> dict[str, str]:
    url = device_url + path
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=DEVICE_TIMEOUT_SECONDS) as response:
            body = response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as error:
        raise ProxyError(f"AMB82 回應 HTTP {error.code}: {error.reason}") from error
    except (socket.timeout, TimeoutError) as error:
        raise ProxyError("連線 AMB82 逾時", 504) from error
    except urllib.error.URLError as error:
        if isinstance(error.reason, (socket.timeout, TimeoutError)):
            raise ProxyError("連線 AMB82 逾時", 504) from error
        raise ProxyError(f"無法連線 AMB82: {error.reason}") from error
    except OSError as error:
        raise ProxyError(f"無法連線 AMB82: {error}") from error

    state = parse_led_state(body)
    if state is None or state.blue is None or state.green is None:
        raise ProxyError("AMB82 回應未包含完整的 blue / green 狀態")
    return {
        "blue": "ON" if state.blue else "OFF",
        "green": "ON" if state.green else "OFF",
    }


def create_app() -> Flask:
    app = Flask(__name__)

    @app.get("/")
    def index():
        return send_from_directory(WEB_DIRECTORY, "index.html")

    @app.get("/style.css")
    def stylesheet():
        return send_from_directory(WEB_DIRECTORY, "style.css")

    @app.get("/app.js")
    def javascript():
        return send_from_directory(WEB_DIRECTORY, "app.js")

    @app.get("/api/parse")
    def parse_command():
        text = request.args.get("text", "")
        path = command_to_path(text)
        return jsonify(
            ok=True,
            text=text,
            path=path,
            label=COMMAND_LABELS.get(path) if path else None,
        )

    @app.get("/api/status")
    def get_status():
        try:
            device_url = validate_device_url(request.args.get("device_url", DEFAULT_AMEBA_URL))
            state = request_device_state(device_url, "/status")
        except ValueError as error:
            return jsonify(ok=False, error=str(error)), 400
        except ProxyError as error:
            return jsonify(ok=False, error=str(error)), error.status_code
        return jsonify(ok=True, state=state)

    @app.post("/api/control")
    def control_device():
        payload = request.get_json(silent=True) or {}
        path = payload.get("path")
        try:
            device_url = validate_device_url(payload.get("device_url", DEFAULT_AMEBA_URL))
        except ValueError as error:
            return jsonify(ok=False, error=str(error)), 400
        if path not in CONTROL_PATHS:
            return jsonify(ok=False, error="不允許的控制路徑"), 400
        try:
            state = request_device_state(device_url, path)
        except ProxyError as error:
            return jsonify(ok=False, error=str(error)), error.status_code
        return jsonify(ok=True, path=path, state=state)

    return app


app = create_app()


if __name__ == "__main__":
    print("Web UI: http://127.0.0.1:5000")
    app.run(host="127.0.0.1", port=5000, debug=False, threaded=True)