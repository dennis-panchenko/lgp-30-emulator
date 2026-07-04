CC ?= cc
STD := -std=c11
WARN := -Wall -Wextra -Wpedantic -Werror

MODE ?= release
ifeq ($(MODE),debug)
  OPT := -g -O0 -fsanitize=address,undefined
else
  OPT := -O2
endif

CFLAGS := $(STD) $(WARN) $(OPT) -Icore -Iio
OBJDIR := build/obj/$(MODE)
BINDIR := bin

LIB_SRCS := $(wildcard core/*.c) $(wildcard core/asm/*.c) $(wildcard io/*.c)
LIB_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(LIB_SRCS))

CLI_SRCS := $(wildcard host/cli/*.c)
CLI_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(CLI_SRCS))

TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BINDIR)/tests/%,$(TEST_SRCS))

.PHONY: all debug test test-run clean

all: $(BINDIR)/lgp30

debug:
	$(MAKE) MODE=debug all

# Tests always run under the sanitizer build, regardless of the ambient
# MODE, since that's where they're most useful. Re-invoke as a sub-make so
# every immediately-expanded variable above (OBJDIR, CFLAGS, ...) gets
# recomputed for MODE=debug from a clean parse.
test:
	$(MAKE) MODE=debug test-run

test-run: $(TEST_BINS) $(BINDIR)/lgp30
	@status=0; \
	for t in $(TEST_BINS); do \
		echo "== $$t =="; \
		./$$t || status=1; \
	done; \
	echo "== tests/cli_test.sh =="; \
	tests/cli_test.sh $(BINDIR)/lgp30 || status=1; \
	echo "== tests/repl_test.sh =="; \
	tests/repl_test.sh $(BINDIR)/lgp30 || status=1; \
	exit $$status

$(BINDIR)/lgp30: $(CLI_OBJS) $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR)/tests/%: $(OBJDIR)/tests/%.o $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf build bin
