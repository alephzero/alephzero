A0 = alephzero

SRC_DIR = src
OBJ_DIR = obj
LIB_DIR = lib
BIN_DIR = bin

CFLAGS += -Wall -Wextra -fPIC -Iinclude
CXXFLAGS += -Wall -Wextra -fPIC -Iinclude -Ithird_party/json/single_include -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=0
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

TEST_CXXFLAGS += -I. -Itest -Ithird_party/doctest/doctest
TEST_LDLIBS += -lm

DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS += -O0 -g3 -ggdb3
    CXXFLAGS += -O0 -g3 -ggdb3
else
    CFLAGS += -O2
    CXXFLAGS += -O2
endif

ifeq ($(PREFIX),)
    PREFIX := /usr
endif

.PHONY: all clean install test uninstall valgrind

all:
	@echo "TODO"

-include $(DEP)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cc
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(OBJ_DIR)/test/%.o: $(SRC_DIR)/test/%.cc
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(TEST_CXXFLAGS) -MMD -c $< -o $@

$(BIN_DIR)/test: $(TEST_OBJ) $(OBJ)
	@mkdir -p $(@D)
	$(CXX) $^ $(LDLIBS) $(TEST_LDLIBS) -o $@

$(LIB_DIR)/lib$(A0).a: $(OBJ)
	@mkdir -p $(@D)
	$(AR) r $@ $^

$(LIB_DIR)/lib$(A0).so: $(OBJ)
	@mkdir -p $(@D)
	$(CXX) -shared -o $@ $^

install: $(LIB_DIR)/lib$(A0).a $(LIB_DIR)/lib$(A0).so
	rm -rf $(DESTDIR)$(PREFIX)/include/a0/
	mkdir -p $(DESTDIR)$(PREFIX)/include/a0/
	cp include/a0/*.h $(DESTDIR)$(PREFIX)/include/a0/
	cp include/a0.h $(DESTDIR)$(PREFIX)/include/a0.h
	mkdir -p $(DESTDIR)$(PREFIX)/lib/
	cp -f $(LIB_DIR)/lib$(A0).* $(DESTDIR)$(PREFIX)/lib/
	mkdir -p $(DESTDIR)$(PREFIX)/lib/pkgconfig/
	cp -f $(A0).pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/

uninstall:
	rm -rf                                        \
	  $(DESTDIR)$(PREFIX)/include/a0/             \
	  $(DESTDIR)$(PREFIX)/include/a0.h            \
	  $(DESTDIR)$(PREFIX)/lib/lib$(A0).*          \
	  $(DESTDIR)$(PREFIX)/lib/pkgconfig/$(A0).pc

test: $(BIN_DIR)/test
	$(BIN_DIR)/test

valgrind: $(BIN_DIR)/test
	@command -v valgrind >/dev/null 2>&1 || {                   \
		echo "\e[01;31mError: valgrind is not installed\e[0m";  \
		exit 1;                                                 \
	}
	RUNNING_ON_VALGRIND=1 valgrind --tool=memcheck --leak-check=yes ./bin/test

clean:
	rm -rf $(OBJ_DIR)/ $(LIB_DIR)/ $(BIN_DIR)/
