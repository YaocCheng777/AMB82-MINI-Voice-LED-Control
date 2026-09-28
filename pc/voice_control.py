from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any


COMMAND_PATHS = {
    "左邊開燈": "/blue/on",
    "左邊關燈": "/blue/off",
    "右邊開燈": "/green/on",
    "右邊關燈": "/green/off",
    "閃爍三次": "/blink",
    "兩個燈閃爍三次": "/blink",
    "兩顆燈閃爍三次": "/blink",
    "turn on the left light": "/blue/on",
    "turn off the left light": "/blue/off",
    "turn on the right light": "/green/on",
    "turn off the right light": "/green/off",
    "left light on": "/blue/on",
    "left light off": "/blue/off",
    "right light on": "/green/on",
    "right light off": "/green/off",
    "blink three times": "/blink",
}
DEFAULT_AMEBA_URL = "http://192.168.1.109"


@dataclass(frozen=True)
class LedState:
    blue: bool | None
    green: bool | None


def command_to_path(text: str) -> str | None:
    normalized = " ".join(text.strip().lower().split()).rstrip(".,!?;:")
    return COMMAND_PATHS.get(normalized)


def _to_led_value(value: Any) -> bool | None:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        normalized = value.strip().upper()
        if normalized in {"ON", "1", "TRUE", "開", "開啟"}:
            return True
        if normalized in {"OFF", "0", "FALSE", "關", "關閉"}:
            return False
    if isinstance(value, (int, float)) and value in {0, 1}:
        return bool(value)
    return None


def parse_led_state(response_text: str) -> LedState | None:
    payload: Any
    try:
        payload = json.loads(response_text)
    except json.JSONDecodeError:
        payload = None

    if isinstance(payload, dict):
        status = payload.get("status", payload)
        if isinstance(status, dict):
            blue = _to_led_value(status.get("blue"))
            green = _to_led_value(status.get("green"))
            if blue is not None or green is not None:
                return LedState(blue=blue, green=green)

    matches = re.search(
        r"BLUE\s*[:=]\s*(ON|OFF).*?GREEN\s*[:=]\s*(ON|OFF)",
        response_text,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if matches:
        return LedState(
            blue=matches.group(1).upper() == "ON",
            green=matches.group(2).upper() == "ON",
        )
    return None


def send_command(base_url: str, path: str, timeout: float) -> tuple[LedState | None, str | None]:
    url = base_url.rstrip("/") + path
    request = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as error:
        return None, f"HTTP 通訊失敗：{error.code} {error.reason}"
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        return None, f"通訊失敗：{error}"

    state = parse_led_state(body)
    if state is None:
        return None, "Ameba 回應未包含可確認的 LED 實際狀態"
    return state, None


def print_led_state(state: LedState) -> None:
    blue = "開" if state.blue is True else "關" if state.blue is False else "未知"
    green = "開" if state.green is True else "關" if state.green is False else "未知"
    print(f"Ameba 實際狀態：左邊藍燈={blue}，右邊綠燈={green}")


def process_recognized_text(text: str, base_url: str, timeout: float) -> None:
    print(f"語音辨識文字：{text}")
    path = command_to_path(text)
    if path is None:
        print("非控制指令")
        return

    print(f"判斷出的控制指令：GET {path}")
    state, error = send_command(base_url, path, timeout)
    if error is not None:
        print(f"HTTP 傳送結果：失敗，{error}")
        print("不假設 LED 已成功改變。")
        return

    print("HTTP 傳送結果：成功")
    print_led_state(state)


def listen_once(recognizer: Any, microphone: Any, language: str) -> str | None:
    with microphone as source:
        print("請說指令...")
        audio = recognizer.listen(source)
    try:
        return recognizer.recognize_google(audio, language=language)
    except Exception:
        print("無法辨識語音")
        return None


def run_text_mode(args: argparse.Namespace) -> int:
    print(f"已啟動鍵盤文字測試模式，Ameba 位址：{args.url}")
    print("請輸入四個合法指令；輸入 exit 或 quit 結束。")
    while True:
        try:
            text = input("請輸入指令：").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n已結束。")
            return 0
        if text.lower() in {"exit", "quit"}:
            print("已結束。")
            return 0
        process_recognized_text(text, args.url, args.timeout)


def run(args: argparse.Namespace) -> int:
    if args.text:
        return run_text_mode(args)

    try:
        import speech_recognition as sr
    except ImportError:
        print("缺少套件 speech_recognition，請先執行：python -m pip install -r requirements.txt", file=sys.stderr)
        return 1

    recognizer = sr.Recognizer()
    try:
        microphone = sr.Microphone()
    except Exception as error:
        print(f"無法開啟電腦麥克風：{error}", file=sys.stderr)
        return 1

    print(f"已啟動語音控制，Ameba 位址：{args.url}")
    print("按 Ctrl+C 結束。")
    while True:
        try:
            recognized = listen_once(recognizer, microphone, args.language)
            if recognized is None:
                continue
            process_recognized_text(recognized, args.url, args.timeout)
        except KeyboardInterrupt:
            print("\n已結束。")
            return 0
        except Exception as error:
            print(f"語音輸入失敗：{error}", file=sys.stderr)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Ameba Mini 中文語音 LED 控制程式")
    parser.add_argument("--url", default=DEFAULT_AMEBA_URL, help=f"Ameba Mini HTTP 基底網址，預設為 {DEFAULT_AMEBA_URL}")
    parser.add_argument("--language", default="zh-TW", help="Google 語音辨識語言，預設為 zh-TW")
    parser.add_argument("--timeout", type=float, default=5.0, help="HTTP timeout 秒數，預設為 5")
    parser.add_argument("--text", action="store_true", help="使用鍵盤文字測試模式，不啟用麥克風")
    return parser


if __name__ == "__main__":
    raise SystemExit(run(build_parser().parse_args()))