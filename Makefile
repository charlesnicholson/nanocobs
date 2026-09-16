SRCS := $(wildcard tests/*.c) $(wildcard tests/*.cc)

BUILD_DIR := build
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
OS := $(shell uname)
COMPILER_VERSION := $(shell $(CXX) --version)

CFLAGS = -std=c99
CXXFLAGS = -std=c++20

# Shared by the unit tests and the benchmark, so both are held to identical
# strictness and differ only in optimization level and sanitizers.
WARN := -Wall -Werror -Wextra
WARN_C :=
WARN_CXX :=
WARN_BENCH :=

ifneq '' '$(findstring clang,$(COMPILER_VERSION))'
WARN += -Weverything \
		-Wno-unknown-warning-option \
		-Wno-unsafe-buffer-usage \
		-Wno-poison-system-directories \
		-Wno-format-pedantic \
		-Wno-switch-default \
		-Wno-thread-safety-negative \
		-Wno-padded
WARN_CXX += -Wno-c++98-compat \
			-Wno-c++98-compat-pedantic \
			-Wno-c++98-compat-bind-to-temporary-copy \
			-Wno-pre-c++20-compat-pedantic \
			-Wno-nrvo
WARN_C += -Wno-declaration-after-statement
WARN_BENCH += -Wno-global-constructors \
			  -Wno-exit-time-destructors \
			  -Wno-double-promotion \
			  -Wno-float-equal \
			  -Wno-switch-enum
else
WARN += -Wconversion
endif

ARCHFLAGS :=
ifeq ($(OS),Linux)
ifeq ($(COBS_LINUXARCH),32)
ARCHFLAGS := -m32
endif
endif

CPPFLAGS += -MMD -MP -Os -g $(ARCHFLAGS) $(WARN)
CFLAGS += $(WARN_C)
CXXFLAGS += $(WARN_CXX)
LDFLAGS += $(ARCHFLAGS)

ifdef COBS_SWAR_WORD_BITS
CPPFLAGS += -DCOBS_SWAR_WORD_BITS=$(COBS_SWAR_WORD_BITS)
endif

ifdef COBS_SANITIZER
SAN := -fsanitize=$(COBS_SANITIZER) -fsanitize-ignorelist=sanitize-ignorelist.txt
CPPFLAGS += $(SAN)
LDFLAGS += $(SAN)
endif

# tests/cobs_ref.c is the scalar half of the differential tests; the tests/*.c
# glob above picks it up like any other test source.
COBS_OBJS := $(BUILD_DIR)/cobs.c.o
DEPS := $(OBJS:.o=.d) $(COBS_OBJS:.o=.d)

$(BUILD_DIR)/cobs_unittests: $(OBJS) $(COBS_OBJS) Makefile
	$(CXX) $(LDFLAGS) $(OBJS) $(COBS_OBJS) -o $@

$(BUILD_DIR)/%.c.o: %.c Makefile
	mkdir -p $(dir $@) && $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cc.o: %.cc Makefile
	mkdir -p $(dir $@) && $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

DOCTEST_ARGS := -m -tse=slow

$(BUILD_DIR)/cobs_unittests.timestamp: $(BUILD_DIR)/cobs_unittests
	$(BUILD_DIR)/cobs_unittests $(DOCTEST_ARGS) && touch $(BUILD_DIR)/cobs_unittests.timestamp

.PHONY: test-all
test-all: $(BUILD_DIR)/cobs_unittests
	$(BUILD_DIR)/cobs_unittests -m

# ---------------------------------------------------------------- benchmark --
# Does not inherit CPPFLAGS: no -Os, no sanitizers. The .bo suffix keeps these
# pattern rules from colliding with the %.cc.o rules above.
BENCH_SRCS := $(wildcard bench/*.cc)
BENCH_OPT ?= -O2
BENCH_OBJS := $(BENCH_SRCS:%=$(BUILD_DIR)/%.bo) \
			  $(BUILD_DIR)/cobs.c.bo $(BUILD_DIR)/tests/cobs_ref.c.bo
BENCH_DEPS := $(BENCH_OBJS:.bo=.bd)
# Recursively expanded on purpose: -MF needs the per-target $@.
BENCH_REV := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
BENCH_CPPFLAGS = -MMD -MP -MF $(@:.bo=.bd) $(BENCH_OPT) -g -DNDEBUG $(ARCHFLAGS) $(WARN) \
				 -DCOBS_BENCH_REV='"$(BENCH_REV)"'

$(BUILD_DIR)/%.cc.bo: %.cc Makefile
	mkdir -p $(dir $@) && $(CXX) $(BENCH_CPPFLAGS) $(CXXFLAGS) $(WARN_BENCH) -c $< -o $@

$(BUILD_DIR)/cobs.c.bo: cobs.c cobs.h Makefile
	mkdir -p $(dir $@) && $(CC) $(BENCH_CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/cobs_ref.c.bo: tests/cobs_ref.c cobs.c cobs.h Makefile
	mkdir -p $(dir $@) && $(CC) $(BENCH_CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cobs_bench: $(BENCH_OBJS) Makefile
	$(CXX) $(ARCHFLAGS) $(BENCH_OBJS) -o $@

.PHONY: bench bench-quick bench-os bench-guard
# A benchmark built at -Os under a sanitizer measures the sanitizer.
bench-guard:
ifdef COBS_SANITIZER
	@echo "refusing to build the benchmark with COBS_SANITIZER set"; exit 1
endif

bench: bench-guard $(BUILD_DIR)/cobs_bench
	$(BUILD_DIR)/cobs_bench $(BENCH_ARGS)

bench-quick: bench-guard $(BUILD_DIR)/cobs_bench
	$(BUILD_DIR)/cobs_bench --quick $(BENCH_ARGS)

bench-os:
	$(MAKE) BENCH_OPT=-Os bench

# --------------------------------------------------------------- code size --
# The only build dependency, and only for the size targets; the bin/ wrappers fetch
# it on first use. Point ARM_CC/ARM_NM at your own cross compiler to opt out.
ARM_CC ?= ./bin/arm-none-eabi-gcc
ARM_NM ?= ./bin/arm-none-eabi-nm
ARM_CFLAGS := -mthumb -mcpu=cortex-m4 -Os -std=c99 -Wall -Wextra -Werror -Wconversion

include size-budget.mk

$(BUILD_DIR)/arm/cobs.o: cobs.c cobs.h Makefile
	mkdir -p $(dir $@) && $(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/arm/cobs_noswar.o: cobs.c cobs.h Makefile
	mkdir -p $(dir $@) && $(ARM_CC) $(ARM_CFLAGS) -DCOBS_SWAR_WORD_BITS=8 -c $< -o $@

# ------------------------------------------------------------------- qemu --
# The doctest suite only ever runs on a 64-bit host; these run a differential
# probe on emulated 32- and 16-bit machines. Needs qemu-system-{arm,avr}.
.PHONY: qemu qemu-cm3 qemu-avr
qemu: qemu-cm3 qemu-avr

qemu-cm3:
	./bin/python3 tests/qemu/run.py --target cm3

qemu-avr:
	./bin/python3 tests/qemu/run.py --target avr --word-bits 16

.PHONY: arm size size-check size-nolibc
arm: $(BUILD_DIR)/arm/cobs.o $(BUILD_DIR)/arm/cobs_noswar.o

size: $(BUILD_DIR)/arm/cobs.o $(BUILD_DIR)/arm/cobs_noswar.o
	@echo '> $(ARM_CC) $(ARM_CFLAGS) -c cobs.c'
	@echo '> $(ARM_NM) --print-size --size-sort cobs.o'
	@echo
	@$(ARM_NM) --print-size --size-sort $(BUILD_DIR)/arm/cobs.o
	@$(ARM_NM) --print-size --size-sort --radix=d $(BUILD_DIR)/arm/cobs.o \
		| awk '{t+=$$2} END {printf "Total %x (%d bytes)\n", t, t}'
	@echo
	@printf 'byte loop only (-DCOBS_SWAR_WORD_BITS=8): '
	@$(ARM_NM) --print-size --size-sort --radix=d $(BUILD_DIR)/arm/cobs_noswar.o \
		| awk '{t+=$$2} END {printf "%d bytes\n", t}'

size-check: $(BUILD_DIR)/arm/cobs.o
	@t=$$($(ARM_NM) --print-size --size-sort --radix=d $< | awk '{s+=$$2} END{print s+0}'); \
	 echo "cortex-m4 -Os total: $$t bytes (budget $(COBS_SIZE_MAX))"; \
	 if [ $$t -gt $(COBS_SIZE_MAX) ]; then echo "SIZE BUDGET EXCEEDED"; exit 1; fi

# cobs.c promises to call no standard library functions. Make that a build
# failure rather than a documentation claim.
size-nolibc: $(BUILD_DIR)/arm/cobs.o $(BUILD_DIR)/arm/cobs_noswar.o
	@for o in $^; do \
		u=$$($(ARM_NM) --undefined-only $$o); \
		if [ -n "$$u" ]; then echo "$$o has undefined symbols:"; echo "$$u"; exit 1; fi; \
	done; \
	echo "no undefined symbols in cobs.o"

.PHONY: clean

# Everything under build/ except the envy package cache: refetching a 135 MB
# cross toolchain is not what anyone means by `make clean`. Use `rm -rf build`.
clean:
	@if [ -d $(BUILD_DIR) ]; then \
		find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -name envy-cache -exec $(RM) -r {} +; \
	fi

.DEFAULT_GOAL := $(BUILD_DIR)/cobs_unittests.timestamp

-include $(DEPS)
-include $(BENCH_DEPS)
