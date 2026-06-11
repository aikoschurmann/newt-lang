# Makefile (improved)
OUT_DIR := out
SRC_DIR := src
TEST_DIR := tests
OBJ_DIR := obj

CC := gcc

LLVM_CONFIG := /opt/homebrew/opt/llvm/bin/llvm-config
LLVM_CFLAGS := $(shell $(LLVM_CONFIG) --cflags)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags --libs core analysis bitwriter target native)

# Base flags
CFLAGS_BASE := -Iinclude -Iinclude/cli -Iinclude/core -Iinclude/codegen -Iinclude/datastructures -Iinclude/lexing -Iinclude/parsing -Iinclude/sema -Iinclude/types -Iinclude/test $(LLVM_CFLAGS) -MMD -MP -g
LDFLAGS_BASE := -lm $(LLVM_LDFLAGS)

# Release flags
CFLAGS_RELEASE := $(CFLAGS_BASE) -O3
LDFLAGS_RELEASE := $(LDFLAGS_BASE)

# Dev flags
CFLAGS_DEV := $(CFLAGS_BASE) -O0 -DDEV_BUILD
LDFLAGS_DEV := $(LDFLAGS_BASE)

# ASAN flags
CFLAGS_ASAN := $(CFLAGS_BASE) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-common
LDFLAGS_ASAN := $(LDFLAGS_BASE) -fsanitize=address,undefined

NAME := compiler
NAME_DEV := compiler-dev

# Gather sources
SRC_FILES := $(shell find $(SRC_DIR) -type f -name "*.c")
# Exclude main.c from common objects
COMMON_SRC_FILES := $(filter-out $(SRC_DIR)/main.c,$(SRC_FILES))

COMMON_OBJ_FILES_RELEASE := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/release/%.o,$(COMMON_SRC_FILES))
COMMON_OBJ_FILES_DEV     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/dev/%.o,$(COMMON_SRC_FILES))

.PHONY: all release dev clean run run-dev test asan

all: release dev

release: $(OUT_DIR)/$(NAME)
dev: $(OUT_DIR)/$(NAME_DEV)

# Release Build
$(OUT_DIR)/$(NAME): $(OBJ_DIR)/release/main.o $(COMMON_OBJ_FILES_RELEASE)
	@mkdir -p $(OUT_DIR)
	$(CC) $^ -o $@ $(LDFLAGS_RELEASE)

$(OBJ_DIR)/release/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_RELEASE) -c $< -o $@

# Dev Build
$(OUT_DIR)/$(NAME_DEV): $(OBJ_DIR)/dev/main.o $(COMMON_OBJ_FILES_DEV)
	@mkdir -p $(OUT_DIR)
	$(CC) $^ -o $@ $(LDFLAGS_DEV)

$(OBJ_DIR)/dev/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DEV) -c $< -o $@

# ASAN Build
asan:
	@mkdir -p $(OBJ_DIR)/asan $(OUT_DIR)
	$(CC) $(CFLAGS_ASAN) $(SRC_FILES) -o $(OUT_DIR)/$(NAME)-asan $(LDFLAGS_ASAN)
	@echo "Built ASAN binary: ./$(OUT_DIR)/$(NAME)-asan"

# Test Runner
TEST_SRC_FILES := $(wildcard test/*.c)
TEST_OBJ_FILES := $(patsubst test/%.c,$(OBJ_DIR)/test/%.o,$(TEST_SRC_FILES))

$(OUT_DIR)/test_runner: $(COMMON_OBJ_FILES_DEV) $(TEST_OBJ_FILES)
	@mkdir -p $(OUT_DIR)
	$(CC) $^ -o $@ $(LDFLAGS_DEV)

$(OBJ_DIR)/test/%.o: test/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DEV) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(OUT_DIR)

run: release
	./$(OUT_DIR)/$(NAME) ./input/test.tn --run

run-dev: dev
	./$(OUT_DIR)/$(NAME_DEV) ./input/test.tn --run

test: $(OUT_DIR)/test_runner
	./$(OUT_DIR)/test_runner
