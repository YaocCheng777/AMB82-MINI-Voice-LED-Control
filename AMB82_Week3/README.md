# AMB82-MINI AI 手勢辨識互動答題系統

第三週完整原型（沿用第二週可執行版本）：透過 AMB82-MINI 攝影機與手部關鍵點模型辨識手勢，以序列埠連接 Python 網頁答題系統。

## 功能

| 手指數量 | 功能 | VLC 顯示 |
|---|---|---|
| 0 | 確認作答 | CONFIRM |
| 1 | 選擇 A | A |
| 2 | 選擇 B | B |
| 3 | 選擇 C | C |
| 4 | 選擇 D | D |
| 5 | 顯示結果後進入下一題 | NEXT |

系統保留 RTSP 影像串流與手部骨架顯示，並加入連續手勢穩定判斷及防止重複觸發機制。

## 專案結構

```text
GestureQuizABCD/GestureQuizABCD.ino  Arduino 主程式
library_patch/NNGestureDetection.cpp  手勢計數與 OSD 修改
library_patch/NNGestureDetection.h    對應標頭檔
server.py                             Python 序列埠與網頁服務
web/                                  網頁介面
requirements.txt                      Python 依賴
start_board_COM3.bat                   COM3 啟動捷徑
start_demo.bat                         無硬體模擬
```

## 安裝

1. 使用 AMB82-MINI、Realtek AmebaPro2 Arduino Core（開發版本為 `4.0.9-build20250805`）、Python 3.9+。
2. **先備份** Arduino 套件原本的 `NNGestureDetection.cpp` 與 `.h`，再將 `library_patch/` 兩個檔案覆蓋至：

   ```text
   %LOCALAPPDATA%\Arduino15\packages\realtek\hardware\AmebaPro2\4.0.9-build20250805\libraries\NeuralNetwork\src
   ```

3. 修改 `GestureQuizABCD/GestureQuizABCD.ino` 的 `YOUR_WIFI_SSID`、`YOUR_WIFI_PASSWORD`，**不要將真實密碼提交到 GitHub**。
4. 在專案根目錄編譯、上傳（COM3 請依實際裝置修改）：

   ```powershell
   arduino-cli compile --clean --fqbn realtek:AmebaPro2:Ameba_AMB82-MINI ./GestureQuizABCD
   arduino-cli upload --fqbn realtek:AmebaPro2:Ameba_AMB82-MINI --port COM3 ./GestureQuizABCD
   ```

5. 關閉 Serial Monitor，安裝並啟動網頁服務：

   ```powershell
   python -m pip install -r requirements.txt
   python server.py --port COM3 --baud 115200
   ```

6. 開啟 `http://127.0.0.1:8000`。

## 不接硬體測試

```powershell
python server.py --demo
```

瀏覽器使用 0～5 模擬按鈕測試選擇、確認與下一題流程。

## 與官方範例差異

官方 Hand Gesture Detection 範例提供手掌與關鍵點偵測、影像串流及骨架顯示；本專案增加手指數量辨識、A–D 答題映射、0 確認／5 下一題、OSD 操作提示、Serial 傳輸及網頁答題判分。

## 第三週測試與已知限制

- 握拳（0）需要模型仍能偵測到手部關鍵點；若未輸出 `fingers=0`，無法確認作答。
- 手勢角度、遮擋、距離及照明可能影響拇指與其他手指計數。
- VLC OSD 與網頁結果需以實機驗證同步性；不能僅依網頁結果推定影像顯示正確。
- 第三週將測試各手勢正確率、不同光線與距離、反應時間及重複觸發情況。

## 操作影片

第二週示範：https://youtu.be/ENPddKph15E

第三週 2～3 分鐘影片：**尚待錄製及填入連結**。須展示成功、無目標及失敗／限制情境。

## 來源與說明

本專案基於 Realtek AmebaPro2 官方 Hand Gesture Detection 範例延伸修改。板端程式需要在本機 Arduino Core 內替換函式庫檔案；單純下載 `.ino` 不足以重現自訂手勢辨識功能。程式碼尚未在此環境完成實體開發板編譯與測試。


## 材料、接線與 SDK

- 硬體：AMB82-MINI 開發板、板載／相容攝影機、USB 資料線、Windows 電腦、Wi-Fi 網路。
- 接線：USB 連接開發板與電腦，供電、燒錄及 Serial 115200；本專題不需外接 LED、按鈕或額外 GPIO。相機需依板子原本的相機連接方式安裝。
- SDK：Arduino CLI + Realtek AmebaPro2 Arduino Core `4.0.9-build20250805`，板型 `realtek:AmebaPro2:Ameba_AMB82-MINI`。Python 3.9+；`requirements.txt` 安裝相依套件。
- 模型：`GESTURE_DETECTION` 搭配 `DEFAULT_PALMDETECT`、`DEFAULT_HANDLANDMARK`；`NNGestureDetection.cpp/.h` 是針對官方 NeuralNetwork 函式庫的自訂修改。
- 相機：主串流 `VIDEO_FHD`、30 fps、H.264、2 Mbps；NN 輸入 RGB 192×192、10 fps；RTSP 透過 Wi-Fi 輸出。
- 網頁：Python 本機 `http://127.0.0.1:8000`；Serial 115200；預設 COM3，依實際連接埠調整。

## 第三週原始測試紀錄

`tests/raw_test_records_40.csv` 預留 30 次初測與 10 次重測，**所有結果欄位為空白，須實機填寫**。`tests/analyze_tests.py` 僅計算已填的實測資料；詳細操作見 `tests/TEST_PROTOCOL.md`。

## 原型驗證狀態

本壓縮檔沿用第二週原型，未在此環境重新對實體 AMB82-MINI 編譯、燒錄或測試。第三週的效能提升必須以實測資料確認，不應以新增測試工具視為準確率已改善。


## 第三週 VLC 純影像版本
- VLC / RTSP 只顯示攝影機原始畫面，不疊加骨架、關節點、辨識字母、CONFIRM/NEXT 或偵測框。
- 手勢辨識仍於板端背景執行，透過序列埠 `[QUIZ] fingers=...` 傳給 `server.py`，網頁答題功能維持不變。
- 需重新燒錄 `GestureQuizABCD/GestureQuizABCD.ino`，並將 `library_patch/NNGestureDetection.cpp` 與 `.h` 覆蓋至 Arduino Core 的 NeuralNetwork/src 後重新編譯。
- 若 VLC 仍看到舊骨架，請重新燒錄並重新開啟 RTSP 串流，確認使用的是此版本程式。
