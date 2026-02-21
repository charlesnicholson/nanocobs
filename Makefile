SRCS := $(wildcard tests/*.c) $(wildcard tests/*.cc)

BUILD_DIR := build
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)
OS := $(shell uname)
COMPILER_VERSION := $(shell $(CXX) --version)

CFLAGS = -std=c99
CXXFLAGS = -std=c++20

CPPFLAGS += -MMD -MP -Os -g

ifeq ($(OS),Linux)
ifeq ($(COBS_LINUXARCH),32)
CPPFLAGS += -m32
LDFLAGS += -m32
endif
endif

CPPFLAGS += -Wall -Werror -Wextra

ifneq '' '$(findstring clang,$(COMPILER_VERSION))'
CPPFLAGS += -Weverything \
			-Wno-unknown-warning-option \
			-Wno-unsafe-buffer-usage \
			-Wno-poison-system-directories \
			-Wno-format-pedantic \
			-Wno-switch-default \
			-Wno-padded
CXXFLAGS += -Wno-c++98-compat \
			-Wno-c++98-compat-bind-to-temporary-copy \
			-Wno-pre-c++20-compat-pedantic
CFLAGS += -Wno-declaration-after-statement
else
CPPFLAGS += -Wconversion
endif

ifdef COBS_SANITIZER
CPPFLAGS += -fsanitize=$(COBS_SANITIZER) -fsanitize-ignorelist=sanitize-ignorelist.txt
LDFLAGS += -fsanitize=$(COBS_SANITIZER) -fsanitize-ignorelist=sanitize-ignorelist.txt
endif

$(BUILD_DIR)/cobs_unittests: $(OBJS) $(BUILD_DIR)/cobs.c.o Makefile
	$(CXX) $(LDFLAGS) $(OBJS) $(BUILD_DIR)/cobs.c.o -o $@

$(BUILD_DIR)/%.c.o: %.c Makefile
	mkdir -p $(dir $@) && $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cc.o: %.cc Makefile
	mkdir -p $(dir $@) && $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/cobs_unittests.timestamp: $(BUILD_DIR)/cobs_unittests
	$(BUILD_DIR)/cobs_unittests -m && touch $(BUILD_DIR)/cobs_unittests.timestamp

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

.DEFAULT_GOAL := $(BUILD_DIR)/cobs_unittests.timestamp

-include $(DEPS)
