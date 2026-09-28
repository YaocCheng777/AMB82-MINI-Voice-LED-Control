# AMB82-MINI HTTP API

AMB82-MINI 在 port 80 提供下列 API。成功的控制路徑與 `/status` 都回傳兩顆 LED 的目前狀態：

```json
{"blue":"ON","green":"OFF"}
```

| Method | Path | 行為 |
| --- | --- | --- |
| GET | `/blue/on` | 藍燈 ON |
| GET | `/blue/off` | 藍燈 OFF |
| GET | `/green/on` | 綠燈 ON |
| GET | `/green/off` | 綠燈 OFF |
| GET | `/blink` | 藍燈與綠燈同步 ON 500 ms、OFF 500 ms，共三次；最後皆 OFF |
| GET | `/status` | 回報目前狀態，不改變 LED |

本機 Flask proxy 的 `/api/control` 只允許上述五個控制路徑，不允許任意網址或路徑。`/api/status` 只代理裝置 `/status`。裝置 timeout、HTTP 錯誤或無法解析完整狀態時，proxy 回傳錯誤，前端不得將狀態顯示為成功。