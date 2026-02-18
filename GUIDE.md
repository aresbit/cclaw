# CClaw 配置及运行指南

## 快速开始

### 1. 安装依赖

```bash
# Debian/Ubuntu
sudo apt-get install -y clang libcurl4-openssl-dev libsqlite3-dev libsodium-dev libuv1-dev uuid-dev

# Termux (Android)
pkg install clang curl sqlite libsodium libuv libuuid

# macOS
brew install curl sqlite libsodium libuv ossp-uuid
```

### 2. 编译

```bash
cd /data/data/com.termux/files/home/yyscode/dev216/cclaw
make clean && make
```

### 3. 初始化配置

```bash
./bin/cclaw onboard
```

按提示输入：
- API Key (从 OpenRouter/Anthropic/OpenAI/kimi/deepseek 获取)
- 默认提供商 (openrouter/anthropic/openai/kimi/deepseek)
- 默认模型
- 内存后端 (sqlite/markdown/none)

## 配置文件

配置文件位于 `~/.cclaw/config.json`：

```json
{
  "api_key": "sk-or-v1-...",
  "default_provider": "openrouter",
  "default_model": "anthropic/claude-sonnet-4-20250514",
  "default_temperature": 0.7,
  "memory": {
    "backend": "sqlite",
    "auto_save": true,
    "conversation_retention_days": 30
  },
  "gateway": {
    "port": 8080,
    "host": "127.0.0.1"
  },
  "autonomy": {
    "level": 1,
    "workspace_only": true,
    "max_actions_per_hour": 100
  },
  "channels": {
    "cli": true
  }
}
```

### 配置项说明

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `api_key` | LLM API 密钥 | 必填 |
| `default_provider` | 默认提供商 | openrouter |
| `default_model` | 默认模型 | anthropic/claude-sonnet-4 |
| `default_temperature` | 采样温度 | 0.7 |
| `memory.backend` | 记忆后端 | sqlite |
| `gateway.port` | 网关端口 | 8080 |
| `autonomy.level` | 自主级别 (0-2) | 1 |

## 运行模式

### 1. 交互式 Agent (推荐)

```bash
./bin/cclaw agent
```

快捷键：
- `Ctrl+H` - 显示帮助
- `Ctrl+N` - 创建新分支 (Pi-style)
- `Ctrl+B` - 显示分支
- `Ctrl+L` - 清屏
- `Ctrl+Q` - 退出
- `↑/↓` - 历史记录

### 2. TUI 界面

```bash
./bin/cclaw tui
```

TUI 特点：
- 侧边栏显示会话列表
- 状态栏显示模型和 token 数
- 支持分支导航
- 颜色主题支持

### 3. 单消息模式

```bash
./bin/cclaw agent -m "你好，请帮我写一段 Python 代码"
```

### 4. 守护进程模式

启动守护进程：
```bash
./bin/cclaw daemon start
```

查看状态：
```bash
./bin/cclaw daemon status
```

停止守护进程：
```bash
./bin/cclaw daemon stop
```

重启：
```bash
./bin/cclaw daemon restart
```

守护进程特点：
- 后台运行
- 自动重连
- Cron 定时任务
- 健康检查端点

## 命令参考

### 基础命令

| 命令 | 说明 | 示例 |
|------|------|------|
| `onboard` | 初始化配置 | `cclaw onboard` |
| `agent` | 启动交互式 Agent | `cclaw agent -m "Hello"` |
| `tui` | 启动 TUI 界面 | `cclaw tui` |
| `daemon` | 守护进程管理 | `cclaw daemon start` |
| `status` | 查看状态 | `cclaw status` |
| `doctor` | 诊断检查 | `cclaw doctor` |
| `channel` | 通道管理 | `cclaw channel list` |
| `cron` | 定时任务 | `cclaw cron list` |

### Agent 命令

```bash
# 交互模式
cclaw agent

# 单消息模式
cclaw agent -m "你好"

# 指定模型
cclaw agent --model "gpt-4o"

# 指定温度
cclaw agent -t 0.5
```

### Daemon 命令

```bash
# 启动
cclaw daemon start

# 停止
cclaw daemon stop

# 重启
cclaw daemon restart

# 查看状态
cclaw daemon status

# 指定 PID 文件
cclaw daemon start -p ~/.cclaw/daemon.pid
```

### Cron 命令

```bash
# 列出定时任务
cclaw cron list

# 添加任务 (格式: 分 时 日 月 周)
cclaw cron add "0 9 * * *" "backup"

# 删除任务
cclaw cron remove <job-id>
```

## 环境变量

可以通过环境变量覆盖配置：

```bash
export CCLAW_API_KEY="sk-..."
export CCLAW_PROVIDER="openrouter"
export CCLAW_MODEL="anthropic/claude-sonnet-4"
export CCLAW_WORKSPACE="/path/to/workspace"

./bin/cclaw agent
```

## 高级用法

### 自定义系统提示

在 `~/.cclaw/system_prompt.txt` 创建文件：

```
你是一个专业的编程助手，擅长：
- Python/Rust/C 语言
- 代码审查和重构
- 算法优化
```

### 扩展开发 (Pi Philosophy)

CClaw 支持 Agent 自我扩展：

```c
// 生成工具扩展代码
str_t source;
extension_generate_tool(
    &STR_LIT("my_tool"),
    &STR_LIT("My custom tool"),
    &schema,
    &implementation,
    &source
);
```

### 会话管理 (Pi-Style)

```
User: 帮我优化这段代码
Agent: 请分享代码
User: [分享代码]
    ├── Branch 1: 使用函数式编程
    └── Branch 2: 使用面向对象  <-- 你在这里
        └── User: 为什么用类？
```

使用 `/new` 创建分支，`/back` 返回上级。

## 故障排除

### 1. 配置文件错误

```bash
# 重新初始化
rm ~/.cclaw/config.json
./bin/cclaw onboard
```

### 2. API 连接失败

```bash
# 检查网络
./bin/cclaw doctor

# 测试 API
curl -H "Authorization: Bearer $API_KEY" \
  https://openrouter.ai/api/v1/models
```

### 3. 守护进程无法启动

```bash
# 检查 PID 文件
rm ~/.cclaw/daemon.pid

# 手动启动查看错误
./bin/cclaw daemon start
```

### 4. TUI 显示异常

```bash
# 检查终端支持
echo $TERM

# 设置合适的 TERM
export TERM=xterm-256color
```

## 示例工作流

### 编程助手

```bash
# 1. 进入工作目录
cd ~/projects/myapp

# 2. 启动 Agent
./bin/cclaw agent

# 3. 对话
> 请帮我审查 main.py
> [Agent 读取文件]
> 这里有三个问题：...

# 4. 创建分支探讨不同方案
/new
> 如果改用 asyncio 会怎样？
```

### 自动化任务

```bash
# 1. 配置定时任务
cclaw cron add "0 9 * * *" "daily_report"

# 2. 启动守护进程
cclaw daemon start

# 3. 查看日志
tail -f ~/.cclaw/daemon.log
```

## 系统服务 (systemd)

创建 `/etc/systemd/system/cclaw.service`：

```ini
[Unit]
Description=CClaw AI Assistant
After=network.target

[Service]
Type=forking
User=username
ExecStart=/usr/local/bin/cclaw daemon start
ExecStop=/usr/local/bin/cclaw daemon stop
PIDFile=/home/username/.cclaw/daemon.pid
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

启用服务：
```bash
sudo systemctl enable cclaw
sudo systemctl start cclaw
```

## 更多信息

- 项目文档: `AGENT_FRAMEWORK.md`
- 开发计划: `plan.md`
- 示例代码: `examples/`

## 获取帮助

```bash
# 查看帮助
./bin/cclaw help

# 命令帮助
./bin/cclaw help agent
./bin/cclaw help daemon
```
## Workspace 工作目录

CClaw 使用 **workspace** 作为 Agent 的安全工作沙盒，限制文件操作范围。

### 默认配置

- **workspace_dir**: `~/.cclaw/workspace`（自动创建，不在 config.json 中显示）
- **autonomy.workspace_only**: `true`（限制 agent 只能访问 workspace 内文件）

### 相关配置项

```json
{
  "autonomy": {
    "workspace_only": true,           // 安全开关，限制文件访问范围
    "max_actions_per_hour": 20,       // 每小时最大操作数
    "allowed_commands": [...],        // 允许的命令白名单
    "forbidden_paths": [...]          // 禁止访问的路径
  },
  "runtime": {
    "docker": {
      "mount_workspace": false,       // Docker 模式是否挂载 workspace
      "allowed_workspace_roots": []   // 多 workspace 根目录白名单
    }
  }
}
```

### 修改 Workspace

**方法 1 - 环境变量**（推荐）：
```bash
export CCLAW_WORKSPACE=/path/to/your/workspace
./bin/cclaw agent
```

**方法 2 - 环境变量（别的方式）**：
```bash
export ZEROCLAW_WORKSPACE=/path/to/workspace
./bin/cclaw agent
```

### Workspace 用途

1. **文件隔离**：防止 Agent 意外修改系统文件（如 `/etc`, `/usr` 等）
2. **会话数据**：存储对话历史、记忆文件、临时文件
3. **工具限制**：`file_read`/`file_write`/`shell` 工具默认只能在 workspace 内操作
4. **项目隔离**：不同项目使用不同 workspace，避免文件冲突

### 安全建议

- 保持 `workspace_only: true`（默认）
- 敏感项目使用独立 workspace
- 定期清理 `~/.cclaw/workspace` 中的临时文件
- 使用 Docker 模式时启用 `mount_workspace` 进行额外隔离

