# CClaw 🦀

**Zero overhead. Zero compromise. 100% C.**

CClaw is a C port of [ZeroClaw](https://github.com/theonlyhennygod/zeroclaw), a fast, small, and fully autonomous AI assistant infrastructure. It maintains the same architecture and feature set as the original Rust implementation while providing the performance and portability of C.

## Features

- 🏎️ **Ultra-Lightweight**: <3MB memory footprint
- 💰 **Minimal Cost**: Runs on $10 hardware
- ⚡ **Lightning Fast**: <5ms startup time
- 🌍 **True Portability**: Single binary across ARM, x86, and RISC-V
- 🔧 **Fully Swappable**: Plugin architecture with vtables
- 🔒 **Secure by Design**: Encryption, pairing, and sandboxing

## Architecture

Every subsystem is a **vtable-based interface** — swap implementations with a config change, zero code changes.

| Subsystem | Interface | Ships with | Extend |
|-----------|-----------|------------|--------|
| **AI Models** | `provider_t` | OpenAI, Anthropic, OpenRouter, Ollama, Gemini | Custom providers |
| **Channels** | `channel_t` | CLI, Telegram, Discord, Slack, WhatsApp, Matrix | Any messaging API |
| **Memory** | `memory_t` | SQLite with hybrid search (FTS5 + vector) | Any persistence backend |
| **Tools** | `tool_t` | shell, file operations, memory management | Any capability |
| **Security** | Various | Gateway pairing, sandbox, allowlists | Custom policies |

## Quick Start

### Build from Source

```bash
# Clone the repository
git clone https://github.com/your-org/cclaw.git
cd cclaw

# Build release version
make

# Build debug version with sanitizers
make debug=1

# Run tests
make test

# Format code
make format

# Run static analysis
make lint
```

### Install Dependencies

```bash
# Debian/Ubuntu
make setup

# Or manually
sudo apt-get install clang clang-tidy clang-format valgrind \
    libcurl4-openssl-dev libsqlite3-dev libsodium-dev libuv1-dev
```

### Basic Usage

```bash
# Show help
./bin/cclaw --help

# Show version
./bin/cclaw --version

# Initialize configuration
./bin/cclaw onboard --api-key sk-... --provider openrouter

# Chat with AI
./bin/cclaw agent -m "Hello, CClaw!"

# Start gateway server
./bin/cclaw gateway --port 8080

# Check status
./bin/cclaw status
```

## Project Structure

```
cclaw/
├── Makefile              # Build system
├── plan.md              # Migration plan and architecture
├── src/                 # Source code
│   ├── main.c          # Program entry point
│   ├── core/           # Core infrastructure
│   ├── providers/      # AI provider implementations
│   ├── channels/       # Communication channels
│   ├── memory/         # Memory system
│   ├── tools/          # Tool implementations
│   ├── runtime/        # Async runtime
│   ├── security/       # Security features
│   └── utils/          # Utility functions
├── include/            # Header files
├── third_party/        # External dependencies
├── tests/              # Test suite
└── examples/           # Example programs
```

## Design Principles

### 1. Memory Safety
- Uses `sp.h` single-header library for memory management
- All allocations go through context-aware allocators
- Zero initialization of all structures
- Automatic leak detection in debug builds

### 2. Error Handling
- Unified error code system (`err_t`)
- Error context propagation
- Comprehensive error messages
- No silent failures

### 3. Plugin Architecture
- Vtable-based interfaces for all subsystems
- Dynamic loading of plugins
- Version compatibility checking
- Hot-swappable components

### 4. Async Runtime
- Event-driven architecture with `libuv`
- Non-blocking I/O operations
- Coroutine-style task management
- Efficient resource utilization

## Configuration

Configuration uses JSON format (migrated from TOML in the Rust original):

```json
{
  "api_key": "sk-...",
  "default_provider": "openrouter",
  "default_model": "anthropic/claude-sonnet-4-20250514",
  "default_temperature": 0.7,
  "memory": {
    "backend": "sqlite",
    "auto_save": true,
    "embedding_provider": "openai"
  },
  "gateway": {
    "port": 8080,
    "host": "127.0.0.1",
    "require_pairing": true
  }
}
```

Configuration files are stored in `~/.cclaw/config.json` by default.

## Security Features

- **Gateway Pairing**: 6-digit one-time code exchange
- **Filesystem Scoping**: Workspace-only access by default
- **Channel Allowlists**: Explicit user/contact authorization
- **Encrypted Secrets**: API keys encrypted at rest
- **Docker Sandboxing**: Optional container isolation
- **Rate Limiting**: Request throttling per client

## Performance Targets

| Metric | Target | Status |
|--------|--------|--------|
| Binary Size | <2MB | ⏳ In progress |
| Memory Usage | <3MB | ⏳ In progress |
| Startup Time | <5ms | ⏳ In progress |
| Request Latency | <100ms | ⏳ In progress |
| Concurrent Connections | 1000+ | ⏳ In progress |

## Development

### Prerequisites
- C11 compatible compiler (clang or gcc)
- CMake 3.10+ (optional)
- Development libraries: curl, sqlite3, sodium, uv

### Building

```bash
# Debug build with sanitizers
make debug=1

# Release build (optimized)
make

# Clean build
make clean

# Generate compile_commands.json
make bear
```

### Testing

```bash
# Run all tests
make test

# Run memory checks
make check

# Run static analysis
make lint
```

### Code Style

- Follow `sp.h` best practices
- Use `clang-format` for formatting
- Zero warnings policy
- Comprehensive documentation

## Migration from ZeroClaw (Rust)

CClaw maintains API compatibility with ZeroClaw where possible:

| Rust Concept | C Implementation |
|--------------|------------------|
| `trait` | `struct` + vtable |
| `impl` | Implementation struct |
| `async fn` | Callback + state machine |
| `Result<T, E>` | `err_t` + output parameters |
| `Vec<T>` | `sp_da(T)` dynamic array |
| `String` | `sp_str_t` (ptr + len) |
| `HashMap<K, V>` | `sp_ht(K, V)` hash table |

## Roadmap

### Phase 1: Core Infrastructure ✓
- [x] Build system
- [x] Basic types and error handling
- [x] Configuration system
- [ ] Memory allocators

### Phase 2: Plugin System
- [ ] Vtable infrastructure
- [ ] Provider interface
- [ ] Channel interface
- [ ] Tool interface

### Phase 3: Async Runtime
- [ ] Event loop with libuv
- [ ] HTTP client
- [ ] WebSocket support
- [ ] Task scheduler

### Phase 4: Features
- [ ] SQLite memory backend
- [ ] OpenAI provider
- [ ] CLI channel
- [ ] Basic tools

### Phase 5: Polish
- [ ] Performance optimization
- [ ] Security audit
- [ ] Documentation
- [ ] Packaging

## License

MIT - See [LICENSE](LICENSE) file.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Acknowledgments

- [ZeroClaw](https://github.com/theonlyhennygod/zeroclaw) - Original Rust implementation
- [sp.h](https://github.com/your-org/sp.h) - Single-header C library
- [libuv](https://github.com/libuv/libuv) - Async I/O library

---

**CClaw** — Zero overhead. Zero compromise. Deploy anywhere. Swap anything. 🦀