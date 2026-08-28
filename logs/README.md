# logs/ — AI Coding 日志目录

本目录保存本作品开发期间导出的真实 AI Coding 会话。提交者为 GitHub
账号 `jxgfc`；每个会话按原始事件结构保留为 JSONL，清单记录导出时间、工具、会话
ID 和相对路径。提交前已对日志中的 Wi-Fi SSID/密码和网页令牌做定向脱敏，不改变
事件顺序。日志中可能包含源码路径和调试输出，但不应包含 Wi-Fi 密码、API Key 或
访问令牌。

## 目录结构

```text
logs/
└── jxgfc/
    ├── manifest.json            # 会话清单
    └── <date>/                  # 日期 YYYY-MM-DD
        └── <tool>__<sid>.jsonl  # 一个会话一个文件（工具名与 session id 用 __ 连接）
```

- `<tool>`：`claude-code` / `opencode` / `codex` / `kiro`
- 每个 `.jsonl` 每行一个事件，由组委会提供的日志归集工具导出，**只提交 JSONL 本身**。

当前会话清单位于 [`jxgfc/manifest.json`](jxgfc/manifest.json)，所有清单路径均
相对于仓库根目录且已逐一核对存在。导出与字段定义见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
