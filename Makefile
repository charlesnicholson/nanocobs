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

# ------------------------------------------------------------------- wasm/js --
# The npm package. Needs a pinned wasm32 clang, which the bin/ wrappers fetch on
# first use; `make` and `make bench` never touch it. Set WASM_CC to use your own.
WASM_CC ?= ./bin/wasm32-clang
PYTHON ?= ./bin/python3
NODE ?= node

include js/wasm-size-budget.mk

JS_PKG := $(BUILD_DIR)/js
# Stand-in for the version outside a release; release.py stamps the real tag over it.
JS_DEV_VERSION := 0.0.0-dev

# The native backend. One prebuild per (platform, arch, libc): N-API is ABI-stable
# across Node versions, so nothing here varies with the Node that loads it. The
# package resolves these at require time, so no install script is ever needed and
# `npm ci --ignore-scripts` still lands on native.
# js/tools/build_addon.py finds the headers, picks the per-platform link line and
# names the output directory the way js/src/loader.js will look for it. Nothing here
# installs anything: without headers the addon is skipped and the package ships
# wasm-only, which is valid, just slower.

# Golden vectors, produced by the C so the JS package is pinned to its bytes. Built
# like the benchmark: own flags, no -Os, no sanitizers, .vo suffix to avoid collisions.
VEC_CPPFLAGS = -MMD -MP -MF $(@:.vo=.vd) -O1 -g -DNDEBUG $(ARCHFLAGS) $(WARN) \
			   -Itests -I.
VEC_OBJS := $(BUILD_DIR)/js/tools/vectors_main.cc.vo $(BUILD_DIR)/cobs.c.vo
VEC_DEPS := $(VEC_OBJS:.vo=.vd)

$(BUILD_DIR)/js/tools/%.cc.vo: js/tools/%.cc Makefile
	mkdir -p $(dir $@) && $(CXX) $(VEC_CPPFLAGS) $(CXXFLAGS) $(WARN_BENCH) -c $< -o $@

$(BUILD_DIR)/cobs.c.vo: cobs.c cobs.h Makefile
	mkdir -p $(dir $@) && $(CC) $(VEC_CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cobs_vectors: $(VEC_OBJS) Makefile
	$(CXX) $(ARCHFLAGS) $(VEC_OBJS) -o $@

.PHONY: js-wasm js-package js-test js-pack js-vectors js-addon

# Exit 2 is "no Node headers here", which is not a failure: a wasm-only package is
# still a correct package, and `make js-test` should work on a machine that has never
# run node-gyp. Any other nonzero status is a real build failure and propagates.
js-addon:
	@$(PYTHON) js/tools/build_addon.py --node $(NODE) --pkg $(JS_PKG); \
	 rc=$$?; \
	 if [ $$rc -eq 2 ]; then \
		echo "js-addon: skipping the native backend; the package will be wasm-only."; \
	 elif [ $$rc -ne 0 ]; then exit $$rc; fi

# Compile the wasm and the JS module that inlines it, both under build/. Nothing
# generated is checked in. build_wasm.py then runs wasm_inspect.py on the result.
js-wasm:
	$(PYTHON) js/tools/build_wasm.py --cc $(WASM_CC) --size-max $(COBS_WASM_SIZE_MAX)

js-vectors: $(BUILD_DIR)/cobs_vectors
	@mkdir -p $(JS_PKG)/test
	$(BUILD_DIR)/cobs_vectors > $(JS_PKG)/test/vectors.json
	@echo "wrote $(JS_PKG)/test/vectors.json ($$(wc -c < $(JS_PKG)/test/vectors.json) bytes)"

# The publishable tree: hand-written sources from js/, the generated wasm, and the
# root LICENSE rather than a copy in js/.
js-package: js-wasm js-vectors js-addon
	@mkdir -p $(JS_PKG)/src $(JS_PKG)/test $(JS_PKG)/native
	@# cp never removes, so a source file deleted upstream would linger here and ship.
	@# Clear what this recipe hand-copies; wasm.js and vectors.json come from the
	@# prerequisites above and must survive.
	@find $(JS_PKG)/src -maxdepth 1 \( -name '*.js' ! -name 'wasm.js' -o -name '*.d.ts' \) \
		-delete
	@rm -f $(JS_PKG)/test/*.mjs $(JS_PKG)/native/*
	@# The manifest placeholder is not valid semver, so npm publish refuses it and a
	@# release that skipped its stamp cannot ship. npm pack does not check, so a
	@# throwaway prerelease goes in here to keep local packing and tests working.
	@sed 's/@COBS_VERSION@/$(JS_DEV_VERSION)/' js/package.json > $(JS_PKG)/package.json
	@cp js/src/index.js js/src/index.d.ts js/src/base64.js js/src/shared.js \
		js/src/native.js js/src/node.js js/src/loader.js $(JS_PKG)/src/
	@# The addon's sources ship too, so a platform without a prebuild can build it by
	@# hand. cobs.c and cobs.h come from the repo root; binding.gyp expects native/.
	@cp js/native/cobs_napi.c cobs.c cobs.h $(JS_PKG)/native/
	@cp js/binding.gyp $(JS_PKG)/
	@cp LICENSE $(JS_PKG)/LICENSE
	@cp js/README.md $(JS_PKG)/README.md
	@if [ -d js/test ]; then cp js/test/*.mjs $(JS_PKG)/test/ 2>/dev/null || true; fi
	@echo "assembled $(JS_PKG)"

# Bare --test: Node 24 treats a directory argument as a file to execute and fails.
js-test: js-package
	cd $(JS_PKG) && $(NODE) --test

# What the release publishes, from the tested tree. Destination is build/, not the
# package dir: a tarball inside it would end up inside the next one.
js-pack: js-package
	cd $(JS_PKG) && npm pack --pack-destination $(CURDIR)/$(BUILD_DIR)

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
-include $(VEC_DEPS)
