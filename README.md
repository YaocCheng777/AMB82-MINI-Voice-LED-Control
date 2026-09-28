# AMB82-MINI 中英文語音 LED 控制系統
# AMB82-MINI Bilingual Voice LED Control

## 一、專案介紹

本專案是使用 VS Code + Codex 進行 Vibe Coding 開發的 AMB82-MINI 中英文語音 LED 控制系統。使用者可透過單頁 Web UI 手動啟動或停止語音辨識，以繁體中文或英文控制 AMB82-MINI 左右 LED，也可使用網頁上的手動按鈕。

## 二、主要功能

- Web 操作介面，可修改裝置 IP 並測試連線
- 手動開始/停止語音辨識，不會在頁面載入時開啟麥克風
- 繁體中文與英文語音控制
- 左側 LED ON/OFF、右側 LED ON/OFF
- 兩顆 LED 同時閃爍三次
- 網頁手動 LED 控制
- 顯示語音辨識文字、解析指令及執行結果
- 顯示 AMB82 實際回傳的 blue / green LED 狀態
- 非控制指令不會送出 LED 控制要求
- 通訊失敗、逾時或無法辨識時顯示錯誤，不推測 LED 狀態

## 三、支援指令

| 中文 | English | 功能 |
| --- | --- | --- |
| 左邊開燈 | Turn on the left light | 左側 LED ON |
| 左邊關燈 | Turn off the left light | 左側 LED OFF |
| 右邊開燈 | Turn on the right light | 右側 LED ON |
| 右邊關燈 | Turn off the right light | 右側 LED OFF |
| 閃爍三次 | Blink three times | 兩顆 LED 同時閃三次 |

英文亦接受 `left light on/off`、`right light on/off`；中文亦接受「兩個燈閃爍三次」及「兩顆燈閃爍三次」。命令採精確白名單比對；例如 `left`、`on`、`turn on left`、`hello` 等不會觸發 LED。

## 四、系統架構

```text
Browser Web UI
	→ Web Speech API
	→ Flask Command Parser
	→ Flask HTTP Proxy
	→ AMB82-MINI HTTP API
	→ Blue / Green LED
```

左側 LED = AMB82 BLUE LED；右側 LED = AMB82 GREEN LED。瀏覽器只呼叫本機 Flask 同源 API，由 backend 轉送既有 AMB82 HTTP API，避免瀏覽器 CORS 限制。Web UI 顯示的 LED 狀態來自 AMB82 HTTP response，不以操作意圖推測。

## 五、硬體需求

- Realtek AMB82-MINI
- USB cable
- Windows PC
- Wi-Fi Router

PC 與 AMB82-MINI 必須位於可互相連線的同一區域網路。PC 的防火牆或網路隔離設定也可能影響裝置連線。

## 六、軟體環境

已確認的開發環境版本：

| 軟體 | 版本 / 說明 |
| --- | --- |
| Windows | Windows（此工作環境） |
| Python | 3.13.5 |
| Arduino IDE | 2.3.10 |
| Arduino CLI | 1.5.1 |
| Realtek AmebaPro2 Arduino Core | 4.0.9-build20250805 |
| AMB82-MINI FQBN | `realtek:AmebaPro2:Ameba_AMB82-MINI` |
| Flask | 3.1.0（目前環境；requirements 設定相容版本範圍） |
| Chrome / Edge | 建議使用最新版；本環境未確認其版本 |
| Web Speech API | 由瀏覽器提供，需網路與麥克風權限 |

## 七、安裝 Python 套件

在專案根目錄執行：

```powershell
python -m pip install -r pc/requirements.txt
```

`Flask` 用於 Web Demo backend。`SpeechRecognition` 與 `PyAudio` 保留給 `pc/voice_control.py` 的獨立命令列麥克風模式；Web UI 的語音辨識使用瀏覽器 Web Speech API。

## 八、AMB82 設定

用 Arduino IDE 開啟 `ameba_led/ameba_led.ino`，在程式開頭填入自己的 Wi-Fi 資訊：

```cpp
char WIFI_SSID[] = "YOUR_WIFI_SSID";
char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
```

請勿將含有真實 Wi-Fi 資訊的版本提交到公開 GitHub。選擇 Board `AMB82-MINI`、FQBN `realtek:AmebaPro2:Ameba_AMB82-MINI` 及正確的 COM Port，再執行 Compile / Upload。也可在專案根目錄使用 Arduino CLI 編譯：

```powershell
$env:LC_ALL = 'C'
$env:LANG = 'C'
arduino-cli compile --fqbn realtek:AmebaPro2:Ameba_AMB82-MINI .\ameba_led
```

## 九、確認 AMB82 IP

開啟 Serial Monitor，baud rate 設為 `115200`，找到輸出：

```text
Ameba IP: http://192.168.x.x
```

可在同一區域網路的瀏覽器測試：

```text
http://AMB82_IP/status
```

例如 `http://192.168.1.109/status`。回應應包含 blue 與 green 狀態。Web UI 的裝置網址輸入欄可在 DHCP 配發位址改變時更新 IP。

## 十、啟動 Web UI

從專案根目錄執行：

```powershell
python pc/web_server.py
```

終端會顯示 Web UI 位址：

```text
http://127.0.0.1:5000
```

在瀏覽器開啟該網址，輸入 AMB82 IP，按「連線 / 測試連線」。這會呼叫 `GET /status`；連線成功後手動控制按鈕才會啟用。

## 十一、使用方法

1. 確認 AMB82-MINI 已上電並連上 Wi-Fi。
2. 在專案根目錄執行 `python pc/web_server.py`。
3. 開啟 `http://127.0.0.1:5000`。
4. 輸入 AMB82 IP 並按「連線 / 測試連線」。
5. 按「開始語音辨識」並允許瀏覽器使用麥克風。
6. 說出支援的中文或英文指令，查看辨識文字、解析結果、HTTP 執行結果及裝置回報狀態。
7. 按「停止語音辨識」結束辨識；也可使用手動 LED 按鈕或兩燈閃爍按鈕。

## 十二、HTTP API

| Method | Path | 用途 |
| --- | --- | --- |
| GET | `/blue/on` | 開啟藍燈（左側 LED） |
| GET | `/blue/off` | 關閉藍燈（左側 LED） |
| GET | `/green/on` | 開啟綠燈（右側 LED） |
| GET | `/green/off` | 關閉綠燈（右側 LED） |
| GET | `/blink` | 藍燈與綠燈同步閃爍三次，完成後皆為 OFF |
| GET | `/status` | 讀取目前兩顆 LED 狀態 |

成功控制 API 會回傳狀態 JSON，例如 `{"blue":"ON","green":"OFF"}`。更詳細的代理行為可參考 [docs/HTTP_API.md](docs/HTTP_API.md)。

## 十三、注意事項

- AMB82 IP 可能因 DHCP 改變；請查看 Serial Monitor 並更新網頁欄位。
- 麥克風需要使用者授權，頁面載入時不會自動啟動。
- 建議使用最新版 Chrome 或 Edge；瀏覽器需支援 `SpeechRecognition` 或 `webkitSpeechRecognition`。
- Web Speech API 需要網路，且可用性受瀏覽器、作業系統與語音服務狀態影響。
- Web Speech API 一次通常使用一個 `recognition.lang`。本程式在同一個開始/停止流程內，依序交替建立 `zh-TW` 與 `en-US` 單次 recognition session，不是同一模型同時辨識兩種語言，也不是自動語言偵測。某次 session 若遇到另一語言，可能辨識失敗；不符合精確命令白名單的結果不會控制 LED。
- 非控制指令不會送出 LED 控制要求。
- LED 狀態以 AMB82 回傳資料為準；HTTP 失敗或 timeout 時畫面顯示未知，不假設操作成功。
- 瀏覽器連到本機 Flask server；backend 才連 AMB82。兩端都須有網路路徑。

## 十四、測試

從專案根目錄執行全部 Python unit tests：

```powershell
python -m unittest discover -s pc -p "test_*.py"
```

目前測試涵蓋中英文命令 parser、非控制/不完整命令拒絕、HTTP status response 解析、控制 API 狀態代理、AMB82 connection failure / timeout、非法路徑拒絕，以及網頁資產路由。

Python syntax check：

```powershell
python -m py_compile pc\voice_control.py pc\web_server.py pc\test_voice_control.py
```

## 十五、Vibe Coding

本專案使用 VS Code + Codex 進行 Vibe Coding：以自然語言描述需求，逐步產生程式，透過測試與實機驗證發現問題，再修正並重新驗證。硬體行為仍以 AMB82 實際回應與 LED 測試為準。