SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

CFLAGS += -Wall -fPIC -Iinclude
LDLIBS += -lm -lpthread -lrt

SRC := $(wildcard $(SRC_DIR)/*.c)
OBJ := $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEP = $(OBJ:%.o=%.d)

TEST_SRC := $(wildcard $(SRC_DIR)/test/*.c)
TEST_OBJ := $(TEST_SRC:$(SRC_DIR)/test/%.c=$(OBJ_DIR)/test/%.o)
TEST_BIN := $(TEST_SRC:$(SRC_DIR)/test/%.c=$(BIN_DIR)/test/%)

TEST_CFLAGS += -I. -Itest -Ithird_party/cheat
TEST_LDLIBS += -lm

.PHONY: all clean test

all:
	@echo "TODO"

-include $(DEP)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(OBJ_DIR)/test/%.o: $(SRC_DIR)/test/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -MMD -c $< -o $@

$(BIN_DIR)/test/%: $(OBJ_DIR)/test/%.o $(OBJ)
	mkdir -p $(@D)
	$(CC) $< $(OBJ) $(LDLIBS) $(TEST_LDLIBS) -o $@

test: $(TEST_BIN)
	for test_bin in $(BIN_DIR)/test/*; do echo "\n$$(tput bold)Testing $$test_bin$$(tput sgr0)" ; $$test_bin; done

clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/*
