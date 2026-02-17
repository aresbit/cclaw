# CClaw Makefile - Modern C build system for ZeroClaw C port

# Build configuration
debug ?= 0
NAME := cclaw
SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
BIN_DIR := bin
TESTS_DIR := tests
THIRD_PARTY_DIR := third_party

# Toolchain
CC := clang
CFLAGS := -std=gnu11 -D_GNU_SOURCE -D__STDC_WANT_LIB_EXT1__=1
CFLAGS += -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wno-unused-parameter
CFLAGS += -I$(INCLUDE_DIR) -I$(THIRD_PARTY_DIR)
CFLAGS += -DSP_IMPLEMENTATION

# Third-party sources
THIRD_PARTY_SRCS := $(THIRD_PARTY_DIR)/json_config.c
THIRD_PARTY_OBJS := $(patsubst $(THIRD_PARTY_DIR)/%.c,$(BUILD_DIR)/third_party/%.o,$(THIRD_PARTY_SRCS))

LDFLAGS := -lm -ldl -lpthread -lcurl -lsqlite3 -lsodium -luv -luuid
LDFLAGS += -L$(THIRD_PARTY_DIR)

# Disable LTO on Android
ifeq ($(PLATFORM),android)
    CFLAGS := $(filter-out -flto,$(CFLAGS))
    LDFLAGS := $(filter-out -flto,$(LDFLAGS))
endif

# Conditional flags
ifeq ($(debug),1)
    CFLAGS += -O0 -g -DDEBUG=1
    CFLAGS += -fsanitize=address -fsanitize=undefined
    LDFLAGS += -fsanitize=address -fsanitize=undefined
else
    CFLAGS += -O3 -DNDEBUG=1
    # LTO disabled on Android due to tagged pointer issues
    ifneq ($(PLATFORM),android)
        CFLAGS += -flto
    endif
    CFLAGS += -fomit-frame-pointer
endif

# Platform detection
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
    CFLAGS += -D_WIN32_WINNT=0x0600
    LDFLAGS += -lws2_32 -ladvapi32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        PLATFORM := linux
        CFLAGS += -D_POSIX_C_SOURCE=200809L
    endif
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := darwin
        CFLAGS += -D_DARWIN_C_SOURCE
        LDFLAGS += -framework CoreFoundation -framework Security
    endif
    ifeq ($(UNAME_S),Android)
        PLATFORM := android
        CFLAGS += -DSP_PS_DISABLE
        LDFLAGS := $(filter-out -lcurl -lsqlite3 -lsodium -luv,$(LDFLAGS))
        LDFLAGS += -lcurl -lsqlite3 -lsodium -luv
    endif
endif

# Source files
CORE_SRCS := $(wildcard $(SRC_DIR)/core/*.c)
PROVIDER_SRCS := $(wildcard $(SRC_DIR)/providers/*.c)
CHANNEL_SRCS := $(wildcard $(SRC_DIR)/channels/*.c)
MEMORY_SRCS := $(wildcard $(SRC_DIR)/memory/*.c)
TOOL_SRCS := $(wildcard $(SRC_DIR)/tools/*.c)
RUNTIME_SRCS := $(wildcard $(SRC_DIR)/runtime/*.c)
SECURITY_SRCS := $(wildcard $(SRC_DIR)/security/*.c)
UTILS_SRCS := $(wildcard $(SRC_DIR)/utils/*.c)
CLI_SRCS := $(wildcard $(SRC_DIR)/cli/*.c)

ALL_SRCS := $(CORE_SRCS) $(PROVIDER_SRCS) $(CHANNEL_SRCS) $(MEMORY_SRCS) \
            $(TOOL_SRCS) $(RUNTIME_SRCS) $(SECURITY_SRCS) $(UTILS_SRCS) \
            $(CLI_SRCS)
MAIN_SRC := $(SRC_DIR)/main.c

# Object files
CORE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SRCS))
PROVIDER_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(PROVIDER_SRCS))
CHANNEL_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CHANNEL_SRCS))
MEMORY_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(MEMORY_SRCS))
TOOL_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(TOOL_SRCS))
RUNTIME_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(RUNTIME_SRCS))
SECURITY_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SECURITY_SRCS))
UTILS_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(UTILS_SRCS))
CLI_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CLI_SRCS))
MAIN_OBJ := $(BUILD_DIR)/main.o

ALL_OBJS := $(CORE_OBJS) $(PROVIDER_OBJS) $(CHANNEL_OBJS) $(MEMORY_OBJS) \
            $(TOOL_OBJS) $(RUNTIME_OBJS) $(SECURITY_OBJS) $(UTILS_OBJS) \
            $(CLI_OBJS) $(THIRD_PARTY_OBJS) $(MAIN_OBJ)

# Test files
TEST_SRCS := $(wildcard $(TESTS_DIR)/*.c)
TEST_OBJS := $(patsubst $(TESTS_DIR)/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TESTS_DIR)/%.c,$(BIN_DIR)/test_%,$(TEST_SRCS))

# Development tools
LINTER := clang-tidy
FORMATTER := clang-format
MEMCHECK := valgrind

# Phony targets
.PHONY: all setup clean format lint test memory_test check dirs

# Default target
all: dirs $(BIN_DIR)/$(NAME)

# Create necessary directories
dirs:
	@mkdir -p $(BUILD_DIR)/core
	@mkdir -p $(BUILD_DIR)/providers
	@mkdir -p $(BUILD_DIR)/channels
	@mkdir -p $(BUILD_DIR)/memory
	@mkdir -p $(BUILD_DIR)/tools
	@mkdir -p $(BUILD_DIR)/runtime
	@mkdir -p $(BUILD_DIR)/security
	@mkdir -p $(BUILD_DIR)/utils
	@mkdir -p $(BUILD_DIR)/cli
	@mkdir -p $(BUILD_DIR)/third_party
	@mkdir -p $(BUILD_DIR)/tests
	@mkdir -p $(BIN_DIR)

# Pattern rule for third-party object files
$(BUILD_DIR)/third_party/%.o: $(THIRD_PARTY_DIR)/%.c
	@mkdir -p $(@D)
	@echo "Compiling third-party $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Main executable
$(BIN_DIR)/$(NAME): $(ALL_OBJS)
	@echo "Linking $(NAME)..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $@"
	@size $@ || true

# Pattern rule for object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Test object files
$(BUILD_DIR)/tests/%.o: $(TESTS_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Test executables - each test file becomes its own binary
test: dirs $(TEST_BINS)
	@echo "Running tests..."
	@failed=0; for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		$$test || failed=$$((failed + 1)); \
	done; \
	if [ $$failed -gt 0 ]; then \
		echo "$$failed test(s) failed"; \
		exit 1; \
	else \
		echo "All tests passed!"; \
	fi

$(BIN_DIR)/test_%: $(BUILD_DIR)/tests/%.o $(filter-out $(MAIN_OBJ),$(ALL_OBJS))
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Development tools
format:
	@echo "Formatting source files..."
	@$(FORMATTER) -i $(ALL_SRCS) $(MAIN_SRC) $(TEST_SRCS) $(wildcard $(INCLUDE_DIR)/*.h) $(wildcard $(INCLUDE_DIR)/*/*.h)

lint:
	@echo "Running static analysis..."
	@$(LINTER) $(ALL_SRCS) $(MAIN_SRC) -- $(CFLAGS) || true

check: $(BIN_DIR)/$(NAME)
	@echo "Running memory check..."
	@$(MEMCHECK) --leak-check=full --show-leak-kinds=all --track-origins=yes $(BIN_DIR)/$(NAME) --help

# Setup development environment (Debian/Ubuntu)
setup:
	@echo "Installing development dependencies..."
	@sudo apt-get update
	@sudo apt-get install -y clang clang-tidy clang-format valgrind \
		libcurl4-openssl-dev libsqlite3-dev libsodium-dev libuv1-dev \
		build-essential pkg-config

# Generate compile_commands.json for tooling
bear: clean
	@bear -- make

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@find . -name "*.o" -delete
	@find . -name "*.d" -delete
	@find . -name "*.so" -delete
	@find . -name "*.a" -delete
	@echo "Clean complete."

# Dependency generation (optional)
DEPFILES := $(ALL_OBJS:.o=.d)
-include $(DEPFILES)

%.d: %.c
	@$(CC) $(CFLAGS) -MM -MP -MT $*.o -MF $@ $<

# Help target
help:
	@echo "CClaw Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build project (default)"
	@echo "  debug=1   - Build with debug symbols and sanitizers"
	@echo "  test      - Build and run tests"
	@echo "  format    - Format source code"
	@echo "  lint      - Run static analysis"
	@echo "  check     - Run memory checks"
	@echo "  setup     - Install development dependencies"
	@echo "  bear      - Generate compile_commands.json"
	@echo "  clean     - Remove build artifacts"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Platform: $(PLATFORM)"
	@echo "Compiler: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"