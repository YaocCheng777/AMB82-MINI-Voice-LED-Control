#include <WiFi.h>

// 請填入你的 Wi-Fi 設定
char WIFI_SSID[] = "YOUR_WIFI_SSID";
char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

WiFiServer server(80);
int wifiStatus = WL_IDLE_STATUS;

// AMB82-MINI core variant.h 定義：LED_B=AMB_D23、LED_G=AMB_D24
const int BLUE_LED_PIN = LED_B;
const int GREEN_LED_PIN = LED_G;

// 若你的 LED 連接方式是共陽極，則 HIGH = 亮、LOW = 滅
// 若你的 LED 連接方式是共陰極，則請將以下兩個常數互換
const int LED_ON = HIGH;
const int LED_OFF = LOW;

bool blueLedState = false;
bool greenLedState = false;

void setBlueLed(bool on)
{
  blueLedState = on;
  digitalWrite(BLUE_LED_PIN, on ? LED_ON : LED_OFF);
}

void setGreenLed(bool on)
{
  greenLedState = on;
  digitalWrite(GREEN_LED_PIN, on ? LED_ON : LED_OFF);
}

void blinkBothLedsThreeTimes()
{
  for (int blink = 0; blink < 3; blink++)
  {
    setBlueLed(true);
    setGreenLed(true);
    delay(500);
    setBlueLed(false);
    setGreenLed(false);
    delay(500);
  }
}

void printStatus()
{
  Serial.print("STATUS: BLUE=");
  Serial.print(blueLedState ? "ON" : "OFF");
  Serial.print(", GREEN=");
  Serial.println(greenLedState ? "ON" : "OFF");
}

String ledStateJson()
{
  String body = "{\"blue\":\"";
  body += blueLedState ? "ON" : "OFF";
  body += "\",\"green\":\"";
  body += greenLedState ? "ON" : "OFF";
  body += "\"}";
  return body;
}

void sendHttpResponse(WiFiClient &client, int statusCode, const char *statusText, const String &body)
{
  client.print("HTTP/1.1 ");
  client.print(statusCode);
  client.print(" ");
  client.println(statusText);
  client.println("Content-Type: application/json; charset=utf-8");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.println(body);
}

void sendStatusResponse(WiFiClient &client)
{
  sendHttpResponse(client, 200, "OK", ledStateJson());
}

String requestPath(const String &requestLine)
{
  int methodEnd = requestLine.indexOf(' ');
  if (methodEnd < 0 || requestLine.substring(0, methodEnd) != "GET")
  {
    return "";
  }

  int pathEnd = requestLine.indexOf(' ', methodEnd + 1);
  if (pathEnd < 0)
  {
    return "";
  }
  return requestLine.substring(methodEnd + 1, pathEnd);
}

void handleHttpClient(WiFiClient &client)
{
  String requestLine = client.readStringUntil('\n');
  requestLine.trim();
  String path = requestPath(requestLine);

  while (client.connected() && client.available())
  {
    String header = client.readStringUntil('\n');
    header.trim();
    if (header.length() == 0)
    {
      break;
    }
  }

  if (path == "/blue/on")
  {
    setBlueLed(true);
    Serial.println("HTTP: /blue/on");
    sendStatusResponse(client);
  }
  else if (path == "/blue/off")
  {
    setBlueLed(false);
    Serial.println("HTTP: /blue/off");
    sendStatusResponse(client);
  }
  else if (path == "/green/on")
  {
    setGreenLed(true);
    Serial.println("HTTP: /green/on");
    sendStatusResponse(client);
  }
  else if (path == "/green/off")
  {
    setGreenLed(false);
    Serial.println("HTTP: /green/off");
    sendStatusResponse(client);
  }
  else if (path == "/blink")
  {
    Serial.println("HTTP: /blink (both LEDs, three times)");
    blinkBothLedsThreeTimes();
    sendStatusResponse(client);
  }
  else if (path == "/status")
  {
    Serial.println("HTTP: /status");
    sendStatusResponse(client);
  }
  else if (path.length() == 0)
  {
    Serial.println("HTTP: invalid method or request");
    sendHttpResponse(client, 400, "Bad Request", "{\"error\":\"Only GET requests are supported\"}");
  }
  else
  {
    Serial.print("HTTP: unknown URL: ");
    Serial.println(path);
    sendHttpResponse(client, 404, "Not Found", "{\"error\":\"Unknown URL\"}");
  }
  client.stop();
}

void printWifiStatus()
{
  Serial.print("Wi-Fi SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("Ameba IP: http://");
  Serial.println(WiFi.localIP());
  Serial.println("HTTP server port: 80");
}

void processCommand(String command)
{
  command.trim();
  command.toUpperCase();

  if (command == "BLUE_ON")
  {
    setBlueLed(true);
    Serial.println("藍燈已開啟");
  }
  else if (command == "BLUE_OFF")
  {
    setBlueLed(false);
    Serial.println("藍燈已關閉");
  }
  else if (command == "GREEN_ON")
  {
    setGreenLed(true);
    Serial.println("綠燈已開啟");
  }
  else if (command == "GREEN_OFF")
  {
    setGreenLed(false);
    Serial.println("綠燈已關閉");
  }
  else if (command == "STATUS")
  {
    printStatus();
  }
  else
  {
    Serial.println("未知指令，請輸入：BLUE_ON / BLUE_OFF / GREEN_ON / GREEN_OFF / STATUS");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(100);

  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  // 開機時兩顆 LED 都關閉
  setBlueLed(false);
  setGreenLed(false);

  Serial.println("Ameba Mini Wi-Fi LED HTTP Server 啟動");
  Serial.println("Serial 指令：BLUE_ON, BLUE_OFF, GREEN_ON, GREEN_OFF, STATUS");
  printStatus();

  while (wifiStatus != WL_CONNECTED)
  {
    Serial.print("正在連線到 Wi-Fi: ");
    Serial.println(WIFI_SSID);
    wifiStatus = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    if (wifiStatus != WL_CONNECTED)
    {
      Serial.println("Wi-Fi 連線失敗，5 秒後重試");
      delay(5000);
    }
  }

  server.begin();
  printWifiStatus();
}

void loop()
{
  WiFiClient client = server.available();
  if (client)
  {
    Serial.println("HTTP client connected");
    handleHttpClient(client);
    Serial.println("HTTP client disconnected");
  }

  if (Serial.available() > 0)
  {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.replace("\r", "");

    if (input.length() > 0)
    {
      processCommand(input);
    }
  }
}
