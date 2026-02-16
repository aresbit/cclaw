# CClaw Agent Framework

基于 **Pi Agent 哲学** 和 **ZeroClaw** 架构的 Agent 框架实现。

## 核心哲学

参考 [Pi Agent 博客](https://lucumr.pocoo.org/2026/1/31/pi/) 和 [代码](https://github.com/badlogic/pi-mono)：

> "LLMs are really good at writing and running code, so embrace this"

- **最小化核心**: 精简系统提示，核心工具集
- **自我扩展**: Agent 通过编写代码来扩展自己，而非下载扩展
- **软件如黏土**: 可塑性强，随时可以修改

## 架构特性

### 1. 树形会话结构 (Pi-Style)

```
┌─ Conversation Tree ─────────────────────────┐
├─ [S] System prompt
│  ├─ [U] User: "Help me refactor..."
│  │  ├─ [A] Assistant: "Sure! Share code..."
│  │  │  ├─ [U] User: "def calc(x)..."
│  │  │  │  ├─ [A] Branch 1: "Use type hints..."
│  │  │  │  └─ [A] Branch 2: "Use classes..."  <-- Alternative path
│  │  │  │     └─ [U] "Why classes?"
└─────────────────────────────────────────────┘
```

支持：
- 分支创建 (`/new`)
- 回溯导航 (`/back`)
- 多路径探索
- 历史保留

### 2. 扩展系统

Pi 哲学的核心：**代码即扩展**

```c
// Agent 生成工具扩展
err_t extension_generate_tool(
    &STR_LIT("calculator"),
    &STR_LIT("Simple calculator"),
    &schema,
    &implementation_code,
    &out_source
);
```

特性：
- 热重载支持
- 运行时编译
- 代码生成 API
- 安全沙箱

### 3. Agent 循环

```
User Input → Context Build → LLM Call → Parse Response
                                              ↓
Tool Result ← Execute Tool ← Tool Call? ← Response
   ↓
Display → Next Input
```

### 4. 文件结构

```
include/core/agent.h       # Agent API 头文件
include/core/extension.h   # 扩展系统头文件
include/runtime/agent_loop.h  # 运行时循环

src/core/agent.c           # Agent 核心实现
src/core/extension.c       # 扩展系统实现
src/runtime/agent_loop.c   # 交互式运行时

examples/agent_example.c       # 使用示例
examples/agent_tree_demo.c     # 树形结构演示
```

## API 概览

### 创建 Agent

```c
agent_config_t config = agent_config_default();
config.max_iterations = 10;
config.autonomy_level = AUTONOMY_LEVEL_SUPERVISED;

agent_t* agent = NULL;
agent_create(&config, &agent);
```

### 会话管理

```c
// 创建会话
agent_session_t* session;
agent_session_create(agent, &STR_LIT("my_session"), &session);

// 处理消息
str_t response;
agent_process_message(agent, session, &user_input, &response);
```

### 树形导航

```c
// 创建分支
agent_message_t* branch;
agent_create_branch(agent, current_message, &branch);

// 导航
agent_navigate_back(agent);
agent_navigate_to(agent, specific_message);
```

## 命令

交互模式下支持：

| 命令 | 描述 |
|------|------|
| `/help` | 显示帮助 |
| `/quit` | 退出 |
| `/new` | 创建分支 |
| `/back` | 返回上级 |
| `/tools` | 列出工具 |
| `/model <name>` | 切换模型 |
| `/temp <0-2>` | 设置温度 |

## 配置

```c
typedef struct agent_config_t {
    uint32_t max_iterations;        // 最大迭代次数
    uint32_t max_context_messages;  // 上下文消息数
    bool enable_summarization;      // 自动摘要
    bool enable_extensions;         // 启用扩展
    bool hot_reload_extensions;     // 热重载
    autonomy_level_t autonomy_level;
} agent_config_t;
```

## 与 Pi 框架对比

| 特性 | Pi (TypeScript) | CClaw (C) |
|------|-----------------|-----------|
| 会话结构 | 树形 | 树形 |
| 工具数量 | 4 (Read/Write/Edit/Bash) | 可配置 |
| 扩展方式 | 代码生成 | 代码生成 + 热重载 |
| 多模型 | 支持 | 支持 |
| TUI | 内置 | 规划中 |
| 大小 | ~MBs | < 2MB |

## 构建

```bash
make clean && make
./bin/cclaw agent        # 交互模式
./bin/cclaw agent -m "Hello"  # 单消息模式
```

## 下一步

- [ ] WebSocket 实时通信
- [ ] TUI 界面 (基于 ncurses)
- [ ] 会话持久化到 SQLite
- [ ] 扩展编译器集成
- [ ] Docker 沙箱执行

## 参考

- [Pi Agent Framework](https://lucumr.pocoo.org/2026/1/31/pi/)
- [Pi Mono Repo](https://github.com/badlogic/pi-mono)
- [ZeroClaw](https://github.com/0xzeroclaw/zeroclaw)
