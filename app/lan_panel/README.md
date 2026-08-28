# R528 LAN panel

`lan_panel` 是基于现有 `wifi_manager` 的低资源 HTTP 面板。它不负责 Wi-Fi 关联；`wifi_manager` 仍读取 `/data/wifi.cfg` 并发布脱敏状态到 `/data/wifi.status`。

## 当前接口

- `GET /`：环境数据、Wi-Fi、温度阈值、日程、历史和运行曲线页面
- `GET /api/v1/status`：读取 `routine_mgr` 原子发布的 `/data/routine/status.json`
- `GET /api/v1/wifi/status`：读取脱敏 Wi-Fi 状态
- `POST /api/v1/wifi/config`：提交与 `/data/wifi.cfg` 相同格式的 bounded 配置，成功后 manager 在轮询周期内应用
- `GET|POST /api/v1/routine/config`：读写温度高低阈值、三条日程和历史采样间隔
- `GET /api/v1/history`：每次从设备重新读取全部 JSONL 历史
- `POST /api/v1/history/export`：把全部历史导出到 FAT32 SD 卡
- `POST /api/v1/light/config`：保存智能照明阈值和档位
- `GET /api/v1/kws/<session>/samples/<sample>.pcm|json`：认证的精确 SD 文件下载

服务只接受 `GET`、`HEAD` 和已实现的 `POST`，不提供通用文件系统浏览、上传、删除、重命名或命令执行。KWS 下载严格限定在 `/sdcard/kws_corpus`，不允许隐藏临时文件或路径穿越。

## 认证

首次启动从 `/dev/urandom` 生成4位数字 bearer token，保存到
`/data/panel.auth`（0600）。Wi-Fi 取得 IPv4 后，设备屏幕会弹出 SSID、IP、
网页地址和4位令牌，因此独立运行时不需要串口。以后每次重新进入设备
Wi-Fi 页面都会重新显示连接信息，方便找回令牌。检测到旧格式令牌文件时会在
升级后自动换成4位数字令牌。令牌不放入网页 URL 或历史日志。

除根页面外的接口均要求：

```text
Authorization: Bearer <token>
```

Wi-Fi 密码不会写入状态文件、日志或 HTTP 响应；页面中的密码字段也不回填保存值。

## 启动

板级 ROMFS `rcS` 会在 `/data` 挂载完成后自动启动：

```text
wifi_manager &
lan_panel &
routine_mgr &
```

`lan_panel` 可以在 Wi-Fi 尚未获取 IPv4 时启动并等待；`routine_mgr` 也不依赖 Wi-Fi。联网并取得 IPv4 后，`wifi_manager` 自动校准系统时钟；断网后系统时钟继续运行。默认监听 TCP `8080`。

## 历史与 SD 卡

默认1分钟记录一次，可选5秒、10秒、1、5、10、30或60分钟。点击网页“加载全部历史”或
打开“运行曲线”时都会发起新请求，不使用上次缓存。导出时会自动尝试把
`/dev/mmcsd1` 挂载到 `/sdcard`，并写入
`/sdcard/routine_history/routine-history-<epoch>.jsonl`。已有同名文件不会被覆盖。
