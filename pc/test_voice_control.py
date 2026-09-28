import unittest
import socket
from unittest.mock import patch
from urllib.error import URLError

from voice_control import DEFAULT_AMEBA_URL, LedState, command_to_path, parse_led_state
from web_server import create_app


class FakeResponse:
    def __init__(self, body):
        self.body = body

    def read(self):
        return self.body

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False


class VoiceControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = create_app().test_client()

    def test_supported_commands(self):
        self.assertEqual(command_to_path("左邊開燈"), "/blue/on")
        self.assertEqual(command_to_path("左邊關燈"), "/blue/off")
        self.assertEqual(command_to_path("右邊開燈"), "/green/on")
        self.assertEqual(command_to_path("右邊關燈"), "/green/off")

    def test_chinese_blink_commands(self):
        self.assertEqual(command_to_path("閃爍三次"), "/blink")
        self.assertEqual(command_to_path("兩個燈閃爍三次"), "/blink")
        self.assertEqual(command_to_path("兩顆燈閃爍三次"), "/blink")

    def test_english_commands(self):
        self.assertEqual(command_to_path("Turn on the left light"), "/blue/on")
        self.assertEqual(command_to_path("Turn off the left light"), "/blue/off")
        self.assertEqual(command_to_path("Turn on the right light"), "/green/on")
        self.assertEqual(command_to_path("Turn off the right light"), "/green/off")
        self.assertEqual(command_to_path("left light on"), "/blue/on")
        self.assertEqual(command_to_path("left light off"), "/blue/off")
        self.assertEqual(command_to_path("right light on"), "/green/on")
        self.assertEqual(command_to_path("right light off"), "/green/off")
        self.assertEqual(command_to_path("blink three times"), "/blink")
        self.assertEqual(command_to_path("  TURN ON THE LEFT LIGHT. "), "/blue/on")

    def test_unknown_command_is_not_sent(self):
        for text in (
            "全部開燈", "播放音樂", "blue", "green", "on", "off", "hello",
            "good morning", "left", "right", "turn on left", "turn on right",
            "turn on the blue light", "turn off the green light", "blue light on",
            "green light off",
        ):
            with self.subTest(text=text):
                self.assertIsNone(command_to_path(text))

    def test_json_state(self):
        self.assertEqual(
            parse_led_state('{"blue":"ON","green":"OFF"}'),
            LedState(blue=True, green=False),
        )

    def test_text_state(self):
        self.assertEqual(
            parse_led_state("STATUS: BLUE=OFF, GREEN=ON"),
            LedState(blue=False, green=True),
        )

    def test_missing_state_is_unknown(self):
        self.assertIsNone(parse_led_state('{"ok":true}'))

    def test_default_ameba_url(self):
        self.assertEqual(DEFAULT_AMEBA_URL, "http://192.168.1.109")

    def test_web_page_assets_are_served(self):
        for path in ("/", "/style.css", "/app.js"):
            with self.subTest(path=path):
                response = self.client.get(path)
                self.assertEqual(response.status_code, 200)
                response.close()

    def test_web_command_parser_supports_chinese_and_english(self):
        commands = {
            "左邊開燈": "/blue/on",
            "左邊關燈": "/blue/off",
            "右邊開燈": "/green/on",
            "右邊關燈": "/green/off",
            "閃爍三次": "/blink",
            "turn on the left light": "/blue/on",
            "turn off the left light": "/blue/off",
            "turn on the right light": "/green/on",
            "turn off the right light": "/green/off",
            "blink three times": "/blink",
        }
        for text, expected_path in commands.items():
            with self.subTest(text=text):
                response = self.client.get("/api/parse", query_string={"text": text})
                self.assertEqual(response.status_code, 200)
                self.assertEqual(response.json["path"], expected_path)

    def test_web_parser_rejects_non_commands(self):
        for text in ("left", "right", "on", "off", "turn on left", "hello", "good morning"):
            with self.subTest(text=text):
                response = self.client.get("/api/parse", query_string={"text": text})
                self.assertIsNone(response.json["path"])

    @patch("web_server.urllib.request.urlopen")
    def test_status_proxy_parses_real_device_state(self, mock_urlopen):
        mock_urlopen.return_value = FakeResponse(b'{"blue":"ON","green":"OFF"}')
        response = self.client.get("/api/status", query_string={"device_url": DEFAULT_AMEBA_URL})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json["state"], {"blue": "ON", "green": "OFF"})
        self.assertTrue(mock_urlopen.call_args.args[0].full_url.endswith("/status"))

    @patch("web_server.urllib.request.urlopen")
    def test_control_proxy_returns_device_state(self, mock_urlopen):
        mock_urlopen.return_value = FakeResponse(b'{"blue":"OFF","green":"ON"}')
        response = self.client.post(
            "/api/control",
            json={"device_url": DEFAULT_AMEBA_URL, "path": "/green/on"},
        )
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json["state"], {"blue": "OFF", "green": "ON"})
        self.assertTrue(mock_urlopen.call_args.args[0].full_url.endswith("/green/on"))

    @patch("web_server.urllib.request.urlopen", side_effect=URLError("connection refused"))
    def test_status_proxy_reports_amb82_connection_failure(self, mock_urlopen):
        response = self.client.get("/api/status", query_string={"device_url": DEFAULT_AMEBA_URL})
        self.assertEqual(response.status_code, 502)
        self.assertFalse(response.json["ok"])
        self.assertIn("無法連線 AMB82", response.json["error"])

    @patch("web_server.urllib.request.urlopen", side_effect=socket.timeout("timed out"))
    def test_status_proxy_reports_timeout(self, mock_urlopen):
        response = self.client.get("/api/status", query_string={"device_url": DEFAULT_AMEBA_URL})
        self.assertEqual(response.status_code, 504)
        self.assertFalse(response.json["ok"])
        self.assertIn("逾時", response.json["error"])

    @patch("web_server.urllib.request.urlopen")
    def test_control_proxy_rejects_unknown_path_without_request(self, mock_urlopen):
        response = self.client.post(
            "/api/control",
            json={"device_url": DEFAULT_AMEBA_URL, "path": "/status"},
        )
        self.assertEqual(response.status_code, 400)
        mock_urlopen.assert_not_called()


if __name__ == "__main__":
    unittest.main()