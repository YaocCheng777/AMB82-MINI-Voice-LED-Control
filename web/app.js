"use strict";

const DEFAULT_DEVICE_URL = "http://192.168.1.109";
const LANGUAGE_CYCLE = [
  { code: "zh-TW", label: "繁體中文" },
  { code: "en-US", label: "English" },
];
const API_TIMEOUT_MS = 12000;
const SpeechRecognitionAPI = window.SpeechRecognition || window.webkitSpeechRecognition;

const elements = {
  connectionForm: document.querySelector("#connection-form"),
  deviceUrl: document.querySelector("#device-url"),
  connectionStatus: document.querySelector("#connection-status"),
  connectionLabel: document.querySelector("#connection-label"),
  blueState: document.querySelector("#blue-state"),
  greenState: document.querySelector("#green-state"),
  refreshStatus: document.querySelector("#refresh-status"),
  startListening: document.querySelector("#start-listening"),
  stopListening: document.querySelector("#stop-listening"),
  listeningDisplay: document.querySelector(".listening-display"),
  listeningLabel: document.querySelector("#listening-label"),
  languageLabel: document.querySelector("#language-label"),
  speechSupportMessage: document.querySelector("#speech-support-message"),
  recognizedText: document.querySelector("#recognized-text"),
  parsedCommand: document.querySelector("#parsed-command"),
  executionResult: document.querySelector("#execution-result"),
  activityLine: document.querySelector(".activity-line"),
  activityMessage: document.querySelector("#activity-message"),
  controlButtons: document.querySelectorAll("[data-path]"),
};

let deviceConnected = false;
let listening = false;
let recognitionActive = false;
let processingRecognition = false;
let currentRecognition = null;
let languageIndex = 0;
let sessionNumber = 0;
let restartTimer = null;
const handledResults = new Set();

const savedUrl = localStorage.getItem("amb82-device-url");
if (savedUrl) elements.deviceUrl.value = savedUrl;
setControlAvailability(false);

function deviceUrl() {
  return elements.deviceUrl.value.trim() || DEFAULT_DEVICE_URL;
}

function setConnection(state, message) {
  elements.connectionStatus.className = `connection-state state-${state}`;
  elements.connectionLabel.textContent = message;
  deviceConnected = state === "connected";
  setControlAvailability(deviceConnected);
}

function setControlAvailability(enabled) {
  elements.controlButtons.forEach((button) => {
    button.disabled = !enabled;
  });
}

function updateLedReadout(element, value) {
  element.classList.remove("on", "off", "unknown");
  if (value === "ON" || value === "OFF") {
    element.textContent = value;
    element.classList.add(value.toLowerCase());
  } else {
    element.textContent = "未知";
    element.classList.add("unknown");
  }
}

function updateDeviceState(state) {
  updateLedReadout(elements.blueState, state?.blue);
  updateLedReadout(elements.greenState, state?.green);
}

function setActivity(message, kind = "neutral") {
  elements.activityMessage.textContent = message;
  elements.activityLine.className = `activity-line ${kind === "neutral" ? "" : `is-${kind}`}`.trim();
}

function setExecutionResult(message, kind = "neutral") {
  elements.executionResult.textContent = message;
  elements.executionResult.className = `result-${kind}`;
}

async function localApi(url, options = {}) {
  const controller = new AbortController();
  const timeoutId = window.setTimeout(() => controller.abort(), API_TIMEOUT_MS);
  try {
    const response = await fetch(url, { ...options, signal: controller.signal });
    const payload = await response.json();
    if (!response.ok || payload.ok === false) {
      throw new Error(payload.error || `本機服務回應 HTTP ${response.status}`);
    }
    return payload;
  } catch (error) {
    if (error.name === "AbortError") throw new Error("等待本機服務逾時");
    throw error;
  } finally {
    window.clearTimeout(timeoutId);
  }
}

function markDeviceFailure(error) {
  setConnection("failed", "連線失敗");
  updateDeviceState(null);
  setActivity(error.message || String(error), "error");
}

async function testConnection() {
  setConnection("pending", "測試連線中");
  setActivity("正在讀取 AMB82 /status…");
  try {
    const query = new URLSearchParams({ device_url: deviceUrl() });
    const payload = await localApi(`/api/status?${query.toString()}`);
    updateDeviceState(payload.state);
    setConnection("connected", "已連線");
    setActivity("AMB82 回應正常，狀態已同步。", "success");
    localStorage.setItem("amb82-device-url", deviceUrl());
  } catch (error) {
    markDeviceFailure(error);
  }
}

async function executeControl(path, label, source = "voice") {
  if (source === "manual") {
    elements.recognizedText.textContent = "手動控制";
  }
  elements.parsedCommand.textContent = label;
  setExecutionResult("傳送中…", "neutral");
  setActivity(`傳送 GET ${path}…`);
  try {
    const payload = await localApi("/api/control", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ device_url: deviceUrl(), path }),
    });
    updateDeviceState(payload.state);
    setConnection("connected", "已連線");
    setExecutionResult("成功", "success");
    setActivity(`AMB82 已回傳 blue=${payload.state.blue}，green=${payload.state.green}`, "success");
    localStorage.setItem("amb82-device-url", deviceUrl());
  } catch (error) {
    setExecutionResult("執行失敗，狀態未知", "error");
    markDeviceFailure(error);
  }
}

async function processTranscript(text) {
  elements.recognizedText.textContent = text || "無法辨識語音";
  if (!text) {
    elements.parsedCommand.textContent = "無控制指令";
    setExecutionResult("未傳送任何控制命令", "warning");
    return;
  }

  const query = new URLSearchParams({ text });
  try {
    const parsed = await localApi(`/api/parse?${query.toString()}`);
    if (!parsed.path) {
      elements.parsedCommand.textContent = "非控制指令";
      setExecutionResult("未傳送任何控制命令", "warning");
      return;
    }
    await executeControl(parsed.path, parsed.label);
  } catch (error) {
    elements.parsedCommand.textContent = "無法解析指令";
    setExecutionResult("執行失敗，未傳送控制命令", "error");
    setActivity(error.message || String(error), "error");
  }
}

function displayNoSpeech(message = "無法辨識語音") {
  elements.recognizedText.textContent = message;
  elements.parsedCommand.textContent = "無控制指令";
  setExecutionResult("未傳送任何控制命令", "warning");
}

function setListeningDisplay(active, languageLabel = "辨識語言由系統輪替") {
  elements.listeningDisplay.classList.toggle("is-listening", active);
  elements.listeningLabel.textContent = active ? "正在聆聽" : "語音辨識已停止";
  elements.languageLabel.textContent = languageLabel;
  elements.startListening.disabled = active;
  elements.stopListening.disabled = !active;
}

function scheduleNextRecognition() {
  if (!listening || processingRecognition || recognitionActive) return;
  window.clearTimeout(restartTimer);
  restartTimer = window.setTimeout(beginRecognitionSession, 220);
}

function stopListening(message = "語音辨識已停止") {
  listening = false;
  window.clearTimeout(restartTimer);
  if (currentRecognition) {
    const recognition = currentRecognition;
    currentRecognition = null;
    try { recognition.abort(); } catch (_) { /* Session may already have ended. */ }
  }
  recognitionActive = false;
  setListeningDisplay(false);
  elements.languageLabel.textContent = message;
}

function beginRecognitionSession() {
  if (!listening || processingRecognition || recognitionActive) return;
  const language = LANGUAGE_CYCLE[languageIndex % LANGUAGE_CYCLE.length];
  languageIndex += 1;
  const thisSession = ++sessionNumber;
  const recognition = new SpeechRecognitionAPI();
  let receivedFinalResult = false;
  let sessionError = false;

  recognition.lang = language.code;
  recognition.continuous = false;
  recognition.interimResults = false;
  recognition.maxAlternatives = 1;
  currentRecognition = recognition;

  recognition.onstart = () => {
    if (currentRecognition !== recognition) return;
    recognitionActive = true;
    setListeningDisplay(true, `本次辨識：${language.label} (${language.code})`);
  };

  recognition.onresult = (event) => {
    for (let index = event.resultIndex; index < event.results.length; index += 1) {
      const result = event.results[index];
      if (!result.isFinal) continue;
      const transcript = result[0]?.transcript?.trim() || "";
      const key = `${thisSession}:${index}:${transcript.toLocaleLowerCase()}`;
      if (handledResults.has(key)) continue;
      handledResults.add(key);
      receivedFinalResult = true;
      if (!transcript) {
        displayNoSpeech();
        continue;
      }
      processingRecognition = true;
      void processTranscript(transcript).finally(() => {
        processingRecognition = false;
        scheduleNextRecognition();
      });
    }
  };

  recognition.onerror = (event) => {
    if (event.error === "aborted") return;
    sessionError = true;
    if (event.error === "no-speech") {
      displayNoSpeech();
    } else if (event.error === "not-allowed" || event.error === "service-not-allowed") {
      elements.speechSupportMessage.textContent = "麥克風權限被拒絕，請在瀏覽器網站設定中允許麥克風後再開始。";
      elements.speechSupportMessage.hidden = false;
      stopListening("麥克風權限被拒絕");
    } else if (event.error === "audio-capture") {
      elements.speechSupportMessage.textContent = "找不到可用的麥克風，請確認 Windows 輸入裝置。";
      elements.speechSupportMessage.hidden = false;
      stopListening("麥克風無法使用");
    } else {
      elements.speechSupportMessage.textContent = `語音辨識服務錯誤：${event.error}`;
      elements.speechSupportMessage.hidden = false;
      displayNoSpeech("語音辨識服務錯誤");
    }
  };

  recognition.onend = () => {
    if (currentRecognition === recognition) currentRecognition = null;
    recognitionActive = false;
    if (!receivedFinalResult && !sessionError) displayNoSpeech();
    scheduleNextRecognition();
  };

  try {
    recognition.start();
  } catch (error) {
    currentRecognition = null;
    recognitionActive = false;
    elements.speechSupportMessage.textContent = `無法啟動語音辨識：${error.message}`;
    elements.speechSupportMessage.hidden = false;
    stopListening("語音辨識已停止");
  }
}

elements.connectionForm.addEventListener("submit", (event) => {
  event.preventDefault();
  void testConnection();
});
elements.deviceUrl.addEventListener("input", () => {
  localStorage.setItem("amb82-device-url", deviceUrl());
  if (deviceConnected) {
    setConnection("idle", "未連線");
    updateDeviceState(null);
  }
});
elements.refreshStatus.addEventListener("click", () => void testConnection());
elements.startListening.addEventListener("click", () => {
  if (!SpeechRecognitionAPI) return;
  elements.speechSupportMessage.hidden = true;
  elements.speechSupportMessage.textContent = "";
  handledResults.clear();
  languageIndex = 0;
  listening = true;
  setListeningDisplay(true, "準備啟動繁體中文辨識");
  beginRecognitionSession();
});
elements.stopListening.addEventListener("click", () => stopListening());
elements.controlButtons.forEach((button) => {
  button.addEventListener("click", () => {
    void executeControl(button.dataset.path, button.dataset.label, "manual");
  });
});

if (!SpeechRecognitionAPI) {
  elements.startListening.disabled = true;
  elements.speechSupportMessage.textContent = "此瀏覽器不支援 Web Speech API。請使用最新版 Chrome 或 Edge；手動控制仍可使用。";
  elements.speechSupportMessage.hidden = false;
}