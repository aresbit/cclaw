# CClaw TUI 修复计划 - 已完成

## 已修复问题

### 1. ✅ 方向键 segfault (Critical)
**文件**: `src/runtime/tui.c:542-578`
**问题**: `strncpy` 可能导致缓冲区溢出
**修复**: 使用 `memcpy` 替代，并手动确保 null 终止，添加长度检查

### 2. ✅ 状态栏模型显示错误
**文件**: `src/runtime/tui.c:485-490`
**问题**: 硬编码显示 "claude-3.5-sonnet"
**修复**: 从 `tui->agent->ctx->provider->config.default_model` 动态读取

### 3. ✅ 消息无回复
**文件**: `src/runtime/tui.c:614-656`
**问题**: 提交消息后没有调用 agent 处理
**修复**: 添加 `agent_process_message` 调用逻辑，处理响应并显示

### 4. ✅ 缺少 Session
**文件**: `src/cli/commands.c:358-373`
**问题**: cmd_tui 没有创建 session，导致消息处理失败
**修复**: 添加 session 创建，并设置默认模型

## 代码变更摘要

### src/runtime/tui.c
- 添加 `core/agent.h` 包含
- 修复方向键缓冲区处理
- 添加消息处理逻辑，调用 agent API
- 状态栏动态显示模型名称

### src/cli/commands.c
- cmd_tui 添加 session 创建
- 设置 session 默认模型

## 测试验证
```bash
# 配置并启动 TUI
./bin/cclaw onboard
./bin/cclaw tui

# 测试项目
1. 输入文本消息，检查是否有回复
2. 按方向键上下浏览历史，不崩溃
3. 状态栏显示配置的模型（如 deepseek）
```

### 5. ✅ 快捷键修复
**文件**: `src/runtime/tui.c:700-745`
**问题**: Ctrl+H/N/B 快捷键无效，且 Ctrl+B 处理缺失
**修复**:
- `Ctrl+H`: 显示帮助信息
- `Ctrl+N`: 创建新 session，复制当前 session 的模型设置
- `Ctrl+B`: 创建新 branch（实现为创建新 session）

### 6. ✅ UTF-8 中文输入支持
**文件**: `src/runtime/tui.c:844-918`
**问题**: 输入中文（UTF-8 编码）导致崩溃
**修复**:
- 添加 UTF-8 辅助函数：
  - `is_utf8_continuation()`: 判断是否为 UTF-8 续字节
  - `utf8_char_len()`: 获取 UTF-8 字符字节长度
  - `utf8_prev_char()`: 查找上一个 UTF-8 字符起始位置
- 修改输入处理逻辑，识别并读取完整 UTF-8 字符序列
- 修改 `tui_input_backspace()`: 删除整个 UTF-8 字符而非单个字节
- 修改 `tui_input_delete()`: 删除当前 UTF-8 字符
- 修改 `tui_input_move_left/right()`: 按 UTF-8 字符边界移动光标

## 键盘快捷键一览

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Q` / `Ctrl+C` | 退出 TUI |
| `Ctrl+H` | 显示帮助 |
| `Ctrl+N` | 创建新 session |
| `Ctrl+B` | 创建新 branch |
| `Ctrl+L` | 重绘界面 |
| `Tab` | 切换 Chat/Sidebar 面板 |
| `↑/↓` (Chat) | 浏览输入历史 |
| `↑/↓` (Sidebar) | 选择 session |
| `Enter` (Sidebar) | 激活选中的 session |
| `Enter` (Chat) | 发送消息 |

## 参考 zeroclaw
zeroclaw 的 CLI 通道 (`src/channels/cli.rs`) 使用简单的同步 I/O：
- 使用 `tokio::io` 异步读取 stdin
- 简单的行缓冲输入
- 直接调用 agent 处理

CClaw TUI 现在采用类似的架构：
- 同步读取输入
- 调用 agent_process_message
- 显示响应