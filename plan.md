# CClaw - ZeroClaw Rust 到 C 迁移计划

> 使用 modern-c-makefile 构建系统和 sp.h (spclib) 单头文件库

## 项目概述

将 **ZeroClaw**（一个轻量级、高性能的AI助手框架，~37,500行Rust代码）从Rust重写为C语言，使用`sp.h`单头文件库提供类似Rust的内存安全性和现代化C编程实践。

### 核心技术栈
- **构建系统**: Modern C Makefile (基于 gnaro 最佳实践)
- **标准库替代**: sp.h 单头文件库
- **C标准**: C17 (GNU扩展)
- **编译器**: clang (首选) 或 gcc
- **HTTP**: libcurl
- **JSON**: cJSON 或 jansson
- **数据库**: SQLite3
- **测试**: CUnit

## 架构对比

| 特性 | Rust (原始) | C (目标) |
|------|-------------|----------|
| **语言** | Rust 2021 edition | C17 + sp.h |
| **二进制大小** | ~3.4MB | <2MB (静态链接) |
| **内存占用** | <5MB RAM | <3MB RAM |
| **启动时间** | <10ms | <5ms |
| **内存安全** | 编译期保证 | sp.h + 规范 |
| **异步** | async/await | libcurl multi + pthread |
| **构建系统** | Cargo | Modern Makefile |
| **测试** | 内置 | CUnit + valgrind |
| **静态分析** | clippy | clang-tidy + scan-build |

## 核心组件迁移策略

### 1. 配置系统 (`src/config/`)
**Rust特性**: TOML解析，serde序列化，自动环境变量覆盖
**C实现方案**:
- 使用`cJSON`或`Jansson`进行JSON配置（替代TOML）
- 手动解析环境变量
- `sp_str_t`用于字符串处理
- 配置结构体使用`SP_ZERO_INITIALIZE()`

### 2. Trait系统转换
**Rust trait示例**:
```rust
#[async_trait]
pub trait Provider: Send + Sync {
    async fn chat(&self, message: &str, model: &str, temperature: f64) -> anyhow::Result<String>;
}
```

**C vtable方案**:
```c
typedef struct provider_vtable {
    err_t (*chat)(void* self, sp_str_t message, sp_str_t model, f64 temperature, sp_str_t* out_response);
    err_t (*warmup)(void* self);
    void (*destroy)(void* self);
} provider_vtable_t;

typedef struct provider {
    provider_vtable_t* vtable;
    void* impl_data;
} provider_t;
```

### 3. 异步运行时 (`tokio`)
**挑战**: Rust的`async/await`需要转换为C的事件循环
**解决方案**:
- 使用`libuv`或`libevent`作为异步I/O基础
- 协程风格的回调函数
- 状态机管理长时间运行操作

### 4. 内存系统 (`src/memory/`)
**Rust实现**: SQLite + FTS5 + 向量搜索
**C实现**:
- 使用`sqlite3` C API
- `sp.h`动态数组用于内存管理
- 手动实现BM25评分和向量相似度

### 5. 通道系统 (`src/channels/`)
**Rust特性**: Telegram、Discord、Slack等集成
**C实现**:
- 每个通道作为独立插件
- HTTP客户端使用`libcurl`
- WebSocket使用`libwebsockets`
- 使用函数指针表进行统一接口

### 6. 工具系统 (`src/tools/`)
**Rust特性**: 动态工具加载和执行
**C实现**:
- 工具作为共享库(`.so`/`.dll`)加载
- `dlopen`/`dlsym`动态加载
- 统一的工具描述符结构

### 7. 安全系统 (`src/security/`)
**Rust特性**: 加密、配对、沙箱
**C实现**:
- 使用`libsodium`进行加密
- 手动实现配对协议
- Docker运行时集成

## 目录结构规划

```
cclaw/
├── CMakeLists.txt           # CMake构建系统
├── Makefile                 # 备用Makefile
├── src/
│   ├── main.c              # 程序入口
│   ├── core/
│   │   ├── alloc.c         # 内存分配器
│   │   ├── error.c         # 错误处理
│   │   ├── config.c        # 配置管理
│   │   └── vtable.c        # vtable基类
│   ├── providers/          # AI提供商
│   │   ├── base.c          # 提供商基类
│   │   ├── openai.c        # OpenAI实现
│   │   ├── anthropic.c     # Anthropic实现
│   │   └── router.c        # 提供商路由
│   ├── channels/           # 通信通道
│   │   ├── base.c          # 通道基类
│   │   ├── cli.c           # 命令行接口
│   │   ├── telegram.c      # Telegram集成
│   │   ├── discord.c       # Discord集成
│   │   └── websocket.c     # WebSocket支持
│   ├── memory/             # 记忆系统
│   │   ├── base.c          # 记忆基类
│   │   ├── sqlite.c        # SQLite后端
│   │   ├── vector.c        # 向量搜索
│   │   └── fts5.c          # 全文搜索
│   ├── tools/              # 工具系统
│   │   ├── registry.c      # 工具注册
│   │   ├── shell.c         # Shell执行
│   │   ├── file.c          # 文件操作
│   │   └── browser.c       # 浏览器控制
│   ├── runtime/            # 运行时
│   │   ├── event_loop.c    # 事件循环
│   │   ├── async.c         # 异步操作
│   │   └── docker.c        # Docker沙箱
│   ├── security/           # 安全
│   │   ├── crypto.c        # 加密
│   │   ├── pairing.c       # 配对协议
│   │   └── sandbox.c       # 沙箱
│   └── utils/              # 工具函数
│       ├── string.c        # 字符串处理
│       ├── json.c          # JSON解析
│       └── http.c          # HTTP客户端
├── include/                # 头文件
│   ├── cclaw.h            # 主头文件
│   ├── core/
│   ├── providers/
│   ├── channels/
│   ├── memory/
│   ├── tools/
│   ├── runtime/
│   ├── security/
│   └── utils/
├── third_party/           # 第三方库
│   ├── sp.h              # sp.h单头文件库
│   ├── cJSON/            # JSON解析
│   ├── libcurl/          # HTTP客户端
│   └── sqlite3/          # 数据库
├── tests/                # 测试
│   ├── unit/             # 单元测试
│   └── integration/      # 集成测试
└── examples/             # 示例
    ├── simple_bot.c      # 简单机器人
    └── custom_provider.c # 自定义提供商
```

## 依赖管理

### 核心依赖
1. **sp.h** - 内存安全、字符串处理、数据结构
2. **libcurl** - HTTP客户端 (HTTPS支持)
3. **sqlite3** - 嵌入式数据库
4. **libsodium** - 加密库
5. **libuv** - 异步I/O (备选: libevent)

### 可选依赖
1. **libwebsockets** - WebSocket支持
2. **docker-ce** - Docker运行时集成
3. **jansson** - JSON处理 (备选cJSON)

## 详细实施计划 (12周)

### 阶段 1: 基础设施搭建 (Week 1)

#### 1.1 项目结构创建
```
cclaw/
├── Makefile              # Modern C Makefile
├── src/
│   ├── main.c           # 入口点
│   ├── cclaw.h          # 主头文件
│   ├── sp.h             # sp.h 单头文件库
│   └── core/
│       ├── error.c      # 错误处理系统
│       ├── error.h
│       ├── string.c     # sp_str_t 扩展工具
│       └── string.h
├── include/             # 公共头文件
├── tests/               # 测试文件
├── build/               # 构建输出
└── docs/                # 文档
```

#### 1.2 Makefile 构建系统 (基于 modern-c-makefile)
- 基于 gnaro_makefile.md 模板
- 支持 debug/release 模式 (`make debug=1`)
- 集成 clang-tidy 静态分析 (`make lint`)
- 集成 valgrind 内存检查 (`make check`)
- 支持 CUnit 测试框架 (`make test`)
- 生成 compile_commands.json (`make bear`)

#### 1.3 sp.h 集成与平台适配
- 下载 sp.h 到项目
- 配置 `SP_IMPLEMENTATION` (仅 main.c)
- Android/Termux 适配: `-DSP_PS_DISABLE`
- 类型别名统一 (s32, u64, sp_str_t, f64 等)

**验收标准**:
- [ ] `make` 成功编译基础框架
- [ ] `make test` 运行通过空测试套件
- [ ] `make lint` 无警告
- [ ] `make check` valgrind 无内存泄漏

---

### 阶段 2: 核心类型与工具 (Week 1-2)

#### 2.1 错误处理系统 (src/core/error.h)
```c
typedef enum {
    ERR_OK = 0,
    ERR_INVALID_INPUT,
    ERR_OUT_OF_MEMORY,
    ERR_IO_ERROR,
    ERR_NOT_FOUND,
    ERR_NETWORK_ERROR,
    ERR_CONFIG_ERROR,
    ERR_PROVIDER_ERROR,
    ERR_CHANNEL_ERROR,
    ERR_UNAUTHORIZED,
    ERR_TIMEOUT,
} err_t;
```

- 错误码传播宏 (`sp_try`, `sp_try_as`)
- 错误信息字符串表
- 日志系统集成 (`SP_LOG`)

#### 2.2 sp_str_t 扩展工具
- `sp_str_from_cstr()` - C 字符串转换
- `sp_str_equal_nocase()` - 大小写不敏感比较
- `sp_str_trim()` - 去除空白
- `sp_str_split()` - 字符串分割
- `sp_str_join()` - 字符串连接
- `sp_str_replace()` - 子串替换

#### 2.3 路径处理工具
- 跨平台路径拼接 (`sp_os_path_join`)
- 家目录检测 (`~` 展开)
- 文件存在性检查 (`sp_os_path_exists`)

**验收标准**:
- [ ] 所有字符串操作有单元测试
- [ ] 动态数组操作无内存泄漏
- [ ] 错误处理宏工作正常

---

### 阶段 3: 配置系统 (Week 2)

#### 3.1 JSON 配置集成
- 使用 cJSON 或 jansson
- 包装为 sp.h 风格 API

#### 3.2 配置结构体定义
将所有 Rust Config 结构体重写到 C:

```c
typedef struct {
    sp_str_t workspace_dir;
    sp_str_t config_path;
    sp_str_t api_key;
    sp_str_t default_provider;
    sp_str_t default_model;
    f64 default_temperature;
    observability_config_t observability;
    autonomy_config_t autonomy;
    // ... 其他配置
} config_t;
```

#### 3.3 配置加载/保存
- `config_load_or_init()` - 加载或创建默认配置
- `config_save()` - 原子写入 (临时文件 + rename)
- `config_apply_env_overrides()` - 环境变量覆盖
- 环境变量完整支持 (ZEROCLAW_*)

**验收标准**:
- [ ] 配置可正确加载/保存
- [ ] 环境变量覆盖工作正常
- [ ] 无效配置 graceful 错误处理

---

### 阶段 4: HTTP 和网络层 (Week 2-3)

#### 4.1 libcurl 封装
```c
typedef struct {
    CURL* curl;
    sp_str_t base_url;
    sp_dyn_array(header_t) headers;
    u32 timeout_ms;
} http_client_t;
```

- `http_client_init()` - 初始化
- `http_get()` - GET 请求
- `http_post_json()` - POST JSON
- `http_post_multipart()` - 文件上传
- 响应结构体封装

#### 4.2 JSON 处理
- cJSON 或 jansson 集成
- JSON 到结构体的映射宏
- 错误处理封装

#### 4.3 TLS/SSL 配置
- 证书验证设置
- 自定义 CA 支持

**验收标准**:
- [ ] HTTP GET/POST 测试通过
- [ ] JSON 解析/生成正确
- [ ] 内存检查无泄漏

---

### 阶段 5: AI 提供商接口 (Week 3-4)

#### 5.1 Provider Trait 到 C (VTable 模式)
```c
typedef struct {
    sp_str_t name;
    err_t (*chat)(void* ctx, const sp_str_t* system_prompt,
                  const sp_str_t* user_msg, const sp_str_t* model,
                  f64 temperature, sp_str_t* out_response);
    err_t (*warmup)(void* ctx);
    void (*destroy)(void* ctx);
} provider_vtable_t;

typedef struct {
    provider_vtable_t* vtable;
    void* ctx;
} provider_t;
```

#### 5.2 OpenRouter 实现
- API 请求构造
- 错误处理 (速率限制等)

#### 5.3 Anthropic 实现
- Claude API 封装

#### 5.4 OpenAI 实现
- GPT API 封装

#### 5.5 Provider 路由
- 根据配置创建对应 provider
- 重试和故障转移逻辑

**验收标准**:
- [ ] 至少一个 provider 可成功对话
- [ ] 配置切换 provider 工作
- [ ] 错误重试机制正常

---

### 阶段 6: 内存系统 (Week 4)

#### 6.1 Memory Trait 到 C
```c
typedef struct {
    sp_str_t key;
    sp_str_t content;
    memory_category_t category;
    s64 timestamp;
} memory_entry_t;

typedef struct {
    err_t (*store)(void* ctx, const sp_str_t* key, const sp_str_t* content,
                   memory_category_t category);
    err_t (*recall)(void* ctx, const sp_str_t* query, u32 limit,
                    sp_dyn_array(memory_entry_t)* out_entries);
} memory_vtable_t;
```

#### 6.2 SQLite 后端
- 数据库初始化
- 表结构创建
- CRUD 操作
- 搜索功能 (LIKE 基础)

#### 6.3 Markdown 后端
- 文件系统存储
- 按日期组织
- 归档逻辑

**验收标准**:
- [ ] 内存可存储/检索
- [ ] SQLite 后端测试通过
- [ ] Markdown 后端文件正确生成

---

### 阶段 7: 工具系统 (Week 4-5)

#### 7.1 Tool Trait 到 C
```c
typedef struct {
    sp_str_t name;
    sp_str_t description;
    err_t (*execute)(void* ctx, const sp_str_t* args, sp_str_t* out_result);
} tool_def_t;
```

#### 7.2 Shell 工具
- 命令白名单检查
- 工作区限制验证
- 超时控制
- 输出捕获

#### 7.3 文件操作工具
- `file_read` - 读取文件
- `file_write` - 写入文件 (原子操作)
- 路径安全检查

#### 7.4 内存工具
- `memory_store` - 存储记忆
- `memory_recall` - 检索记忆
- `memory_forget` - 删除记忆

**验收标准**:
- [ ] Shell 工具白名单生效
- [ ] 文件操作路径限制工作
- [ ] 内存工具可存储/检索

---

### 阶段 8: 通道系统 (Week 5-6)

#### 8.1 Channel Trait 到 C
```c
typedef struct {
    sp_str_t id;
    sp_str_t sender;
    sp_str_t content;
    sp_str_t channel;
    s64 timestamp;
} channel_message_t;

typedef struct {
    sp_str_t name;
    err_t (*send)(void* ctx, const sp_str_t* message, const sp_str_t* recipient);
    err_t (*listen)(void* ctx, void (*on_message)(channel_message_t* msg));
    err_t (*health_check)(void* ctx, bool* out_healthy);
} channel_vtable_t;
```

#### 8.2 CLI 通道
- 标准输入读取
- 交互式提示

#### 8.3 Telegram Bot
- HTTP 长轮询
- 消息解析
- 发送消息 API

#### 8.4 Discord Bot
- WebSocket 连接
- 消息事件处理
- REST API 调用

#### 8.5 Webhook 通道
- HTTP 服务器基础
- HMAC 签名验证

**验收标准**:
- [ ] CLI 通道可交互
- [ ] 至少一个外部通道可用
- [ ] 多通道并发处理正常

---

### 阶段 9: Agent 核心 (Week 6)

#### 9.1 系统提示构建
- `build_system_prompt()` - 从 workspace 文件构建
- 工具描述注入
- Skills 列表注入
- AIEOS/OpenClaw 格式支持

#### 9.2 Agent 循环
- 消息接收处理
- 上下文构建
- LLM 调用
- 响应解析 (工具调用检测)
- 工具执行
- 结果返回

#### 9.3 消息路由
- 多通道消息分发
- 会话管理

**验收标准**:
- [ ] Agent 可接收消息并回复
- [ ] 系统提示正确构建
- [ ] 工具调用流程工作

---

### 阶段 10: 守护进程与调度 (Week 6-7)

#### 10.1 守护进程模式
- 后台运行支持
- PID 文件管理
- 信号处理 (SIGTERM/SIGHUP)

#### 10.2 Cron 调度器
- cron 表达式解析
- 任务队列
- 定时执行

#### 10.3 健康检查
- 组件状态跟踪
- 重启计数
- 健康端点

**验收标准**:
- [ ] 守护模式可正常启动/停止
- [ ] Cron 任务按时执行
- [ ] 健康检查端点工作

---

### 阶段 11: CLI 与命令 (Week 7)

#### 11.1 参数解析
- 类似 clap 的命令结构
- 子命令支持
- 帮助生成

#### 11.2 命令实现
- `cclaw onboard` - 初始化向导
- `cclaw agent` - 启动对话
- `cclaw daemon` - 守护模式
- `cclaw status` - 状态显示
- `cclaw channel` - 通道管理
- `cclaw cron` - 定时任务
- `cclaw doctor` - 诊断

#### 11.3 交互式向导
- 提示输入
- 选择菜单
- 配置验证

**验收标准**:
- [ ] 所有命令帮助信息完整
- [ ] onboard 向导可完成配置
- [ ] 命令错误处理友好

---

### 阶段 12: 系统集成与测试 (Week 8)

#### 12.1 集成测试
- 端到端工作流测试
- 多组件协作测试

#### 12.2 性能测试
- 内存使用分析
- 响应时间基准

#### 12.3 文档
- API 文档
- 使用指南
- 迁移指南 (从 zeroclaw)

#### 12.4 打包与发布
- 安装脚本
- 系统服务文件 (systemd)

**验收标准**:
- [ ] 完整功能测试通过
- [ ] 文档完整
- [ ] 可安装运行

---

## 进度跟踪

| 阶段 | 描述 | 预计时间 | 状态 | 完成日期 |
|------|------|----------|------|----------|
| 1 | 基础设施 | Week 1 | ✅ 已完成 | 2025-02-16 |
| 2 | 核心类型 | Week 1-2 | ✅ 已完成 | 2025-02-16 |
| 3 | 配置系统 | Week 2 | ✅ 已完成 | 2025-02-16 |
| 4 | HTTP/网络 | Week 2-3 | ✅ 已完成 | 2025-02-16 |
| 5 | AI 提供商 | Week 3-4 | ✅ 已完成 | 2026-02-16 |
| 6 | 内存系统 | Week 4 | ✅ 已完成 | 2026-02-16 |
| 7 | 工具系统 | Week 4-5 | ✅ 已完成 | 2026-02-16 |
| 8 | 通道系统 | Week 5-6 | ✅ 已完成 | 2026-02-16 |
| 9 | Agent 核心 | Week 6 | ✅ 已完成 | 2026-02-16 |
| 10 | 守护进程 | Week 6-7 | ✅ 已完成 | 2026-02-16 |
| 11 | CLI 命令 | Week 7 | ✅ 已完成 | 2026-02-16 |
| 12 | 集成测试 | Week 8 | ⏳ 未开始 | - |

### 阶段 1-4 完成总结

**已完成工作**:
- ✅ Makefile 构建系统 (基于 modern-c-makefile)
- ✅ sp.h 集成 (third_party/sp.h)
- ✅ 错误处理系统 (src/core/error.c, include/core/error.h)
- ✅ 核心类型定义 (include/core/types.h)
- ✅ JSON 解析器 (third_party/json_config.c/h)
- ✅ 完整配置系统 (src/core/config.c, include/core/config.h)
  - 默认配置生成
  - JSON 配置文件加载/保存
  - 环境变量覆盖
  - 原子写入 (临时文件 + rename)
- ✅ HTTP 客户端 (src/utils/http.c, include/utils/http.h)
  - GET/POST/PUT/PATCH/DELETE 方法
  - JSON 内容类型支持
  - 自定义请求头
  - URL 编码/解码
  - 查询字符串构建
  - 基于 libcurl
- ✅ 主程序入口 (src/main.c)
- ✅ CLI 参数解析框架

**构建验证**:
```bash
make              # ✅ 编译成功
./bin/cclaw --help # ✅ 程序可运行
./test_http       # ✅ HTTP 客户端测试通过
```

**HTTP 测试验证**:
```
HTTP Client Test
================
✓ HTTP initialized
✓ HTTP client created
✓ Default header added
✓ HTTP GET completed (Status: 200)
✓ HTTP POST completed (Status: 200)
✓ All tests completed
```

**二进制信息**:
- 大小: ~120KB (含 HTTP 模块, 测试程序)
- 依赖: libcurl, sqlite3, libsodium, libuv
- 平台: Android/Termux (已适配)

---

## sp.h (spclib) 编码规范

### 1. SP_IMPLEMENTATION 使用规则

sp.h 是**单头文件库**，必须严格遵守:
```c
// 在**一个且仅一个** C 文件中定义实现（通常是 main.c）
#define SP_IMPLEMENTATION
#include "sp.h"

// 其他文件只包含头文件：
#include "sp.h"
```

**错误现象**: 多个 .o 文件中出现重复定义的链接错误。

### 2. Android/Termux 平台适配

在 Android/Termux 环境中，某些 POSIX 函数不可用：
```makefile
# 在 Makefile 中添加：
CFLAGS += -DSP_PS_DISABLE
```

### 3. 核心类型别名

| sp.h 类型 | C 标准类型 | 描述 |
|-----------|------------|------|
| `s8` | `int8_t` | 有符号8位 |
| `s16` | `int16_t` | 有符号16位 |
| `s32` | `int32_t` | 有符号32位 |
| `s64` | `int64_t` | 有符号64位 |
| `u8` | `uint8_t` | 无符号8位 |
| `u16` | `uint16_t` | 无符号16位 |
| `u32` | `uint32_t` | 无符号32位 |
| `u64` | `uint64_t` | 无符号64位 |
| `f32` | `float` | 32位浮点 |
| `f64` | `double` | 64位浮点 |
| `c8` | `char` | UTF-8字符 |
| `sp_str_t` | `{char* data; u32 len;}` | 长度已知字符串 |

### 4. 字符串处理规则

```c
// ❌ 错误: 使用C字符串
const char* name = "Alice";
printf("Hello %s\n", name);

// ✅ 正确: 使用sp_str_t
sp_str_t name = sp_str_lit("Alice");
SP_LOG("Hello {}", SP_FMT_STR(name));

// ❌ 错误: 手动计算字符串长度
if (strlen(str) > 0) { ... }

// ✅ 正确: 使用sp.h的API
if (!sp_str_empty(str)) { ... }

// ❌ 错误: 字符串比较
strcmp(s1, s2) == 0

// ✅ 正确: 使用sp_str_equal
sp_str_equal(s1, s2)
```

### 5. 内存管理规则

```c
// ❌ 错误: 使用裸malloc
int* arr = malloc(sizeof(int) * 10);

// ✅ 正确: 使用sp_alloc
int* arr = sp_alloc(sizeof(int) * 10);

// ❌ 错误: 未初始化结构体
my_struct_t obj;

// ✅ 正确: 零初始化
my_struct_t obj = SP_ZERO_INITIALIZE();
// 或
my_struct_t obj = {0};  // C99替代
```

### 6. 动态数组使用

```c
// 声明数组（stb风格）
sp_dyn_array(int) numbers = SP_NULLPTR;

// 添加元素
sp_dyn_array_push(numbers, 42);
sp_dyn_array_push(numbers, 100);

// 遍历（不要用裸for循环）
sp_dyn_array_for(numbers, i) {
    SP_LOG("numbers[{}] = {}", SP_FMT_U32(i), SP_FMT_S32(numbers[i]));
}

// 清理
sp_dyn_array_free(numbers);
```

### 7. 日志和格式化

```c
// 替代 printf，支持类型安全和颜色
SP_LOG("Hello, {}!", SP_FMT_CSTR("world"));

// 数字格式化
s32 num = 42;
SP_LOG("The answer is {}", SP_FMT_S32(num));

// 颜色支持
SP_LOG("{:fg green}Success!{:reset}", SP_FMT_CSTR(""));
SP_LOG("{:fg red}Error:{:reset} {}", SP_FMT_CSTR(""), SP_FMT_CSTR("something wrong"));

// 可用颜色: black, red, green, yellow, blue, magenta, cyan, white
// 加bright前缀: bright-red, bright-green, 等等
```

### 8. 错误处理模式

```c
// 错误码定义
typedef enum {
    ERR_OK = 0,
    ERR_INVALID_INPUT,
    ERR_OUT_OF_MEMORY,
    ERR_NOT_FOUND,
    ERR_NETWORK_ERROR,
} err_t;

// 错误处理宏
err_t do_something(args) {
    // 检查输入
    sp_require(ptr != NULL, ERR_INVALID_INPUT);

    // 分配内存
    void* mem = sp_alloc(size);
    sp_require_as(mem != NULL, ERR_OUT_OF_MEMORY);

    // 传播错误
    err_t result = other_function();
    sp_try(result);  // 如果result != 0，返回result

    return ERR_OK;
}
```

### 9. Switch 语句规范

```c
// 总是处理所有情况，使用花括号
switch (state) {
    case STATE_IDLE: {
        // 处理空闲状态
        break;
    }
    case STATE_RUNNING: {
        // 处理运行状态
        break;
    }
    default: {
        SP_UNREACHABLE_CASE();  // 捕获未处理的情况
    }
}

// 如果需要fallthrough，显式标记
switch (value) {
    case 0: {
        // 处理0
        sp_fallthrough();  // 显式fallthrough
    }
    case 1: {
        // 处理0和1
        break;
    }
}
```

### 10. 代码检查清单

在提交代码前，确认：

- [ ] 使用 `SP_ZERO_INITIALIZE()` 初始化所有结构体
- [ ] 使用 `sp_str_t` 而不是 `const char*`
- [ ] 使用 `sp_alloc()` 而不是 `malloc()`
- [ ] 使用 `SP_LOG()` 而不是 `printf()`
- [ ] 使用 `sp_str_empty()` 而不是检查 `len > 0`
- [ ] Switch 语句处理所有枚举值
- [ ] 使用 `sp_dyn_array_for()` 或 `sp_carr_for()` 遍历数组
- [ ] 字符串比较使用 `sp_str_equal()` 而不是 `strcmp()`
- [ ] 错误处理使用 `sp_try()` 和 `sp_require()` 宏

---

## Makefile 构建系统规范

### 核心命令
```bash
make              # 构建项目（默认目标）
make debug=1      # 调试构建（带符号，无优化）
make test         # 运行测试
make lint         # 运行静态分析（clang-tidy）
make format       # 格式化代码（clang-format）
make check        # 内存检查（valgrind）
make clean        # 清理构建产物
make bear         # 生成 compile_commands.json
```

### 项目结构规范
```
cclaw/
├── Makefile          # 主 Makefile
├── src/              # 源文件 (*.c)
├── include/          # 头文件 (*.h)
├── lib/              # 第三方库
├── tests/            # 测试文件
├── build/            # 对象文件（生成）
└── bin/              # 可执行文件（生成）
```

---

## 安全考虑

### 1. 内存安全
- 所有分配通过sp.h分配器
- 零初始化所有结构体
- 边界检查数组访问

### 2. 输入验证
- 验证所有外部输入
- 限制文件系统访问
- 沙箱化工具执行

### 3. 加密安全
- 使用libsodium进行现代加密
- 安全密钥存储
- 传输层加密

### 4. 沙箱安全
- Docker容器隔离
- 资源限制
- 网络访问控制

## 性能目标

### 基准测试指标
1. **二进制大小**: <2MB (静态链接)
2. **内存使用**: <3MB (空闲状态)
3. **启动时间**: <5ms (冷启动)
4. **请求延迟**: <100ms (本地LLM)
5. **并发连接**: 1000+ (网关模式)

### 优化策略
1. 零拷贝字符串处理 (sp_str_t)
2. 连接池复用
3. 懒加载组件
4. 内存池分配器

## 测试策略

### 单元测试
- 每个模块独立的测试
- 使用CMake的CTest
- 覆盖率报告

### 集成测试
- 端到端场景测试
- 跨平台测试
- 性能基准测试

### 模糊测试
- 输入验证测试
- 内存安全测试
- 边界条件测试

## 开发准则

### 代码风格
1. **C11标准**: 使用现代C特性
2. **sp.h惯例**: 遵循sp.h最佳实践
3. **错误处理**: 所有函数返回错误码
4. **资源管理**: 清晰的获取/释放对
5. **文档**: 所有公共API文档化

### 安全实践
1. **初始化**: 所有变量显式初始化
2. **验证**: 检查所有外部输入
3. **边界**: 检查所有数组访问
4. **清理**: 及时释放资源
5. **审计**: 定期安全代码审查

## 风险评估

### 高风险
1. **异步复杂性**: 事件循环设计错误
2. **内存泄漏**: 资源管理错误
3. **安全漏洞**: 输入验证缺失

### 缓解措施
1. **渐进实现**: 小步骤验证
2. **代码审查**: 定期团队审查
3. **自动化测试**: 全面测试覆盖

## 成功标准

### 功能完整性
1. [ ] 支持所有原始提供商
2. [ ] 实现所有通道类型
3. [ ] 完整的工具系统
4. [ ] 安全特性全部实现

### 性能指标
1. [ ] 二进制大小 < 2MB
2. [ ] 内存使用 < 3MB
3. [ ] 启动时间 < 5ms
4. [ ] 请求延迟 < 100ms

### 质量指标
1. [ ] 单元测试覆盖率 > 80%
2. [ ] 零内存泄漏
3. [ ] 跨平台兼容性
4. [ ] 完整文档

## 下一步行动

### 当前状态 (阶段 1-4 已完成)

**已完成**:
- ✅ 项目目录结构
- ✅ Modern Makefile 构建系统
- ✅ sp.h 集成
- ✅ 错误处理系统 (error.c/h)
- ✅ 核心类型定义 (types.h)
- ✅ JSON 解析器 (json_config.c/h)
- ✅ 完整配置系统 (config.c/h)
- ✅ HTTP 客户端 (utils/http.c/h)
  - GET/POST/PUT/PATCH/DELETE 方法
  - JSON 支持
  - 自定义请求头
  - URL 编码/解码
- ✅ CLI 框架和参数解析
- ✅ 主程序入口

**待办事项**:

#### 阶段 5: AI 提供商接口 (已完成)
- [x] Provider VTable 定义 (include/providers/base.h)
- [x] OpenRouter 实现 (src/providers/openrouter.c)
- [x] Anthropic 实现 (src/providers/anthropic.c)
- [x] OpenAI 实现 (src/providers/openai.c)
- [x] DeepSeek 实现 (src/providers/deepseek.c)
- [x] Kimi 实现 (src/providers/kimi.c)
- [x] Provider 路由和选择逻辑 (src/providers/router.c)
- [x] 重试和故障转移逻辑 (src/providers/base.c:provider_chat_with_retry)
- [x] 流式响应支持 (SSE) (DeepSeek/OpenAI 已实现，Anthropic TODO)

#### 阶段 6: 内存系统 (已完成)
- [x] Memory VTable 定义 (include/core/memory.h)
- [x] SQLite 后端实现 (src/memory/sqlite.c)
- [x] Markdown 后端实现 (src/memory/markdown.c)
- [x] Null 后端实现 (src/memory/null.c)
- [x] 内存基类和注册表 (src/memory/base.c)
- [x] 基础搜索功能 (SQLite FTS5)

### 推荐下一步 (阶段 7: 工具系统)

1. **Tool VTable 定义**: 创建 include/core/tool.h 定义 tool 接口 VTable
2. **Shell 工具实现**: 实现安全的命令执行工具
3. **文件操作工具**: 实现文件读写工具
4. **内存工具**: 实现与内存系统交互的工具

### 开发建议

### 开发建议

```bash
# 快速开发循环
make clean && make && ./bin/cclaw status

# 调试构建
make clean && make debug=1 && ./bin/cclaw status

# 检查内存泄漏 (需要 valgrind)
make check
```

## 附录

### A. Rust到C的映射表

| Rust概念 | C实现 |
|----------|-------|
| `trait` | `struct` + 函数指针表 |
| `impl` | 包含vtable的结构体 |
| `async fn` | 回调函数 + 状态机 |
| `Result<T, E>` | 错误码 + 输出参数 |
| `Vec<T>` | `sp_dyn_array(T)` |
| `HashMap<K, V>` | `sp_ht(K, V)` |
| `String` | `sp_str_t` |
| `Option<T>` | 指针 + NULL检查 |
| `match` | `switch` + 枚举 |
| `unwrap()` | 错误检查宏 |

### B. 第三方库评估

1. **sp.h**: 已选定 - 提供内存安全基础
2. **libcurl vs. neon**: libcurl更成熟，HTTPS支持更好
3. **libuv vs. libevent**: libuv更现代，Node.js验证
4. **cJSON vs. Jansson**: cJSON更轻量，Jansson功能更全

### C. 平台支持目标

1. **Linux**: 主要支持平台
2. **macOS**: 完全支持
3. **Windows**: 通过WSL/MinGW支持
4. **Android/Termux**: 通过NDK支持

---

*计划版本: 1.0*
*创建日期: 2026-02-16*
*预计完成: 8-12周*