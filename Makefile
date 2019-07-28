SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

CFLAGS += -Wall -fPIC -Iinclude
CXXFLAGS += -Wall -fPIC -Iinclude -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=0
LDLIBS += -lm -lpthread -lrt

SRC_C := $(wildcard $(SRC_DIR)/*.c)
SRC_CXX := $(wildcard $(SRC_DIR)/*.cc)

OBJ := $(SRC_C:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
OBJ += $(SRC_CXX:$(SRC_DIR)/%.cc=$(OBJ_DIR)/%.o)

DEP = $(OBJ:%.o=%.d)

TEST_SRC_C := $(wildcard $(SRC_DIR)/test/*.c)
TEST_SRC_CXX := $(wildcard $(SRC_DIR)/test/*.cc)

TEST_OBJ := $(TEST_SRC_C:$(SRC_DIR)/test/%.c=$(OBJ_DIR)/test/%.o)
TEST_OBJ += $(TEST_SRC_CXX:$(SRC_DIR)/test/%.cc=$(OBJ_DIR)/test/%.o)

TEST_CFLAGS += -I. -Itest -Ithird_party/catch2
TEST_CXXFLAGS += -I. -Itest -Ithird_party/catch2
TEST_LDLIBS += -lm

DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CXXFLAGS += -O0 -g3 -ggdb3
endif

.PHONY: all clean test

all:
	@echo "TODO"

-include $(DEP)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cc
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(OBJ_DIR)/test/%.o: $(SRC_DIR)/test/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -MMD -c $< -o $@

$(OBJ_DIR)/test/%.o: $(SRC_DIR)/test/%.cc
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(TEST_CXXFLAGS) -MMD -c $< -o $@

$(BIN_DIR)/test: $(TEST_OBJ) $(OBJ)
	mkdir -p $(@D)
	$(CXX) $(TEST_OBJ) $(OBJ) $(LDLIBS) $(TEST_LDLIBS) -o $@

test: $(BIN_DIR)/test
	$(BIN_DIR)/test

clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/*
