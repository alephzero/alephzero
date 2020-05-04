A0 = alephzero

SRC_DIR = src
OBJ_DIR = obj
LIB_DIR = lib
BIN_DIR = bin

CXFLAGS += -Wall -Wextra -fPIC -Iinclude
CXXFLAGS += -std=c++17 -Wno-missing-field-initializers -Wno-braced-scalar-init
LDFLAGS += -lm -lpthread -lrt

SRC_C := $(wildcard $(SRC_DIR)/*.c)
SRC_CXX := $(wildcard $(SRC_DIR)/*.cpp)

OBJ := $(SRC_C:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
OBJ += $(SRC_CXX:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

DEP = $(OBJ:%.o=%.d)

TEST_SRC_C := $(wildcard $(SRC_DIR)/test/*.c)
TEST_SRC_CXX := $(wildcard $(SRC_DIR)/test/*.cpp)

TEST_OBJ := $(TEST_SRC_C:$(SRC_DIR)/test/%.c=$(OBJ_DIR)/test/%.o)
TEST_OBJ += $(TEST_SRC_CXX:$(SRC_DIR)/test/%.cpp=$(OBJ_DIR)/test/%.o)

TEST_CXXFLAGS += -I. -Itest -Ithird_party/doctest/doctest
TEST_LDFLAGS += -lm

BENCH_SRC_C := $(wildcard $(SRC_DIR)/bench/*.c)
BENCH_SRC_CXX := $(wildcard $(SRC_DIR)/bench/*.cpp)

BENCH_OBJ := $(BENCH_SRC_C:$(SRC_DIR)/bench/%.c=$(OBJ_DIR)/bench/%.o)
BENCH_OBJ += $(BENCH_SRC_CXX:$(SRC_DIR)/bench/%.cpp=$(OBJ_DIR)/bench/%.o)

BENCH_CXXFLAGS += -I. -Ibench -Ithird_party/picobench/include

DEBUG ?= 0
ifneq ($(DEBUG), 1)
	REQUIRE_DEBUG := $(filter $(MAKECMDGOALS),asan tsan ubsan valgrind cov covweb)
	DEBUG = $(if $(REQUIRE_DEBUG),1,0)
endif

cov: CXFLAGS += -fprofile-arcs -ftest-coverage --coverage
cov: LDFLAGS += -lgcov

asan: CXFLAGS += -fsanitize=address -fsanitize=leak -fno-sanitize-recover=address
asan: LDFLAGS += -fsanitize=address -fsanitize=leak

tsan: CXFLAGS += -fsanitize=thread -fno-sanitize-recover=thread
tsan: LDFLAGS += -fsanitize=thread

ubsan: CXFLAGS += -fsanitize=undefined -fno-sanitize-recover=undefined
ubsan: LDFLAGS += -fsanitize=undefined -lubsan

ifeq ($(DEBUG), 1)
	CXFLAGS += -O0 -g3 -ggdb3 -DDEBUG
else
	CXFLAGS += -O2 -DNDEBUG
endif

ifeq ($(PROFILE), 1)
	CXFLAGS += -g3 -ggdb3
	LDFLAGS += -Wl,--no-as-needed -lprofiler -Wl,--as-needed
endif

ifeq ($(PREFIX),)
	PREFIX := /usr
endif

.PHONY: all asan bench clean cov covweb install iwyu test tsan ubsan uninstall valgrind

all:
	@echo "TODO"

-include $(DEP)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CXFLAGS) -MMD -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXFLAGS) $(CXXFLAGS) -MMD -c $< -o $@

$(OBJ_DIR)/test/%.o: $(SRC_DIR)/test/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXFLAGS) $(CXXFLAGS) $(TEST_CXXFLAGS) -MMD -c $< -o $@

$(BIN_DIR)/test: $(TEST_OBJ) $(OBJ)
	@mkdir -p $(@D)
	$(CXX) $^ $(LDFLAGS) $(TEST_LDFLAGS) -o $@

$(OBJ_DIR)/bench/%.o: $(SRC_DIR)/bench/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXFLAGS) $(CXXFLAGS) $(BENCH_CXXFLAGS) -MMD -c $< -o $@

$(BIN_DIR)/bench: $(BENCH_OBJ) $(OBJ)
	@mkdir -p $(@D)
	$(CXX) $^ $(LDFLAGS) $(BENCH_LDFLAGS) -o $@

$(LIB_DIR)/lib$(A0).a: $(OBJ)
	@mkdir -p $(@D)
	$(AR) r $@ $^

$(LIB_DIR)/lib$(A0).so: $(OBJ)
	@mkdir -p $(@D)
	$(CXX) -shared -o $@ $^

install: $(LIB_DIR)/lib$(A0).a $(LIB_DIR)/lib$(A0).so
	rm -rf $(DESTDIR)$(PREFIX)/include/a0/
	mkdir -p $(DESTDIR)$(PREFIX)/include/a0/
	cp include/a0/* $(DESTDIR)$(PREFIX)/include/a0/
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

bench: $(BIN_DIR)/bench
	$(BIN_DIR)/bench

asan tsan ubsan: $(BIN_DIR)/test
	RUNNING_ON_VALGRIND=1 $(BIN_DIR)/test

valgrind: $(BIN_DIR)/test
	@command -v valgrind >/dev/null 2>&1 || {                   \
		echo "\e[01;31mError: valgrind is not installed\e[0m";  \
		exit 1;                                                 \
	}
	RUNNING_ON_VALGRIND=1 valgrind --tool=memcheck --leak-check=yes ./bin/test

cov: $(BIN_DIR)/test
	$(BIN_DIR)/test
	gcov -o $(OBJ_DIR) -bmr $(SRC_DIR)/*

covweb: cov
	@mkdir -p cov/web
	lcov --capture --quiet --rc lcov_branch_coverage=1 --directory . --no-external --output-file cov/tmp.info
	lcov --rc lcov_branch_coverage=1 --remove cov/tmp.info "*/third_party/*" "*/test/*" --output-file cov/coverage.info
	genhtml cov/coverage.info --branch-coverage --output-directory cov/web
	(cd cov/web ; python3 -m http.server)

iwyu/%.c.ok: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	iwyu $(CFLAGS) $(CXFLAGS) -Xiwyu --mapping_file=./iwyu.imp -c $< ; \
	if [ $$? -eq 2 ]; then touch $@ ; else exit 1 ; fi

iwyu/%.cpp.ok: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	iwyu $(CXFLAGS) $(CXXFLAGS) -Xiwyu --mapping_file=./iwyu.imp -c $< ; \
	if [ $$? -eq 2 ]; then touch $@ ; else exit 1 ; fi

iwyu: $(SRC_C:$(SRC_DIR)/%.c=iwyu/%.c.ok) $(SRC_CXX:$(SRC_DIR)/%.cpp=iwyu/%.cpp.ok)

clean:
	rm -rf $(OBJ_DIR)/ $(LIB_DIR)/ $(BIN_DIR)/ cov/ iwyu/ *.gcov
