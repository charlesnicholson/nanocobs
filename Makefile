SRCS := tests/test_cobs_encode_max.cc \
		tests/test_cobs_encode.cc \
		tests/test_cobs_encode_inc.cc \
		tests/test_cobs_encode_inplace.cc \
		tests/test_cobs_decode.cc \
		tests/test_cobs_decode_inplace.cc \
		tests/test_paper_figures.cc \
		tests/test_wikipedia.cc \
		tests/unittest_main.cc

BUILD_DIR := build
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)
OS := $(shell uname)
COMPILER_VERSION := $(shell $(CXX) --version)

CFLAGS = --std=c99
CXXFLAGS = --std=c++17

CPPFLAGS += -MMD -MP -Os -g

ifeq ($(OS),Linux)
ifeq ($(COBS_LINUXARCH),32)
CPPFLAGS += -m32
LDFLAGS += -m32
endif
endif

ifeq ($(COBS_SANITIZE),1)
CPPFLAGS_SAN += -fsanitize=undefined,address
LDFLAGS_SAN += -fsanitize=undefined,address
endif

CPPFLAGS += -Wall -Werror -Wextra

ifneq '' '$(findstring clang,$(COMPILER_VERSION))'
CPPFLAGS += -Weverything \
			-Wno-poison-system-directories \
			-Wno-format-pedantic \
			-Wno-c++98-compat-bind-to-temporary-copy
CFLAGS += -Wno-declaration-after-statement
else
CPPFLAGS += -Wconversion
endif

CPPFLAGS += -Wno-c++98-compat -Wno-padded

$(BUILD_DIR)/cobs_unittests: $(OBJS) $(BUILD_DIR)/cobs.c.o Makefile
	$(CXX) $(LDFLAGS) $(LDFLAGS_SAN) $(OBJS) $(BUILD_DIR)/cobs.c.o -o $@

$(BUILD_DIR)/cobs.c.o: cobs.c cobs.h Makefile
	mkdir -p $(dir $@) && $(CC) $(CPPFLAGS) $(CFLAGS) $(CPPFLAGS_SAN) -c $< -o $@

$(BUILD_DIR)/%.c.o: %.c Makefile
	mkdir -p $(dir $@) && $(CC) $(CPPFLAGS) $(CFLAGS) $(CPPFLAGS_SAN) -c $< -o $@

$(BUILD_DIR)/%.cc.o: %.cc Makefile
	mkdir -p $(dir $@) && $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CPPFLAGS_SAN) -c $< -o $@

$(BUILD_DIR)/cobs_unittests.timestamp: $(BUILD_DIR)/cobs_unittests
	$(BUILD_DIR)/cobs_unittests -m && touch $(BUILD_DIR)/cobs_unittests.timestamp

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

.DEFAULT_GOAL := $(BUILD_DIR)/cobs_unittests.timestamp

-include $(DEPS)

