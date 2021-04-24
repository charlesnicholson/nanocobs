SRCS := cobs.c \
		tests/test_cobs_encode_max.cc \
		tests/test_cobs_encode.cc \
		tests/test_cobs_encode_inplace.cc \
		tests/test_cobs_decode_inplace.cc \
		tests/test_paper_figures.cc \
		tests/test_wikipedia.cc \
		tests/unittest_main.cc

BUILD_DIR := build
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)
OS := $(shell uname)

CPPFLAGS = -MMD -MP -Os -g -Wall -Werror -Wextra -Wno-c++98-compat

ifeq ($(OS),Darwin)
	CPPFLAGS += -Weverything -Wno-poison-system-directories -Wno-format-pedantic
else ifeq ($(OS),Linux)
  ifdef $(COBS_LINUX32)
	CPPFLAGS += -m32
	LDFLAGS += -m32
  endif
endif

CFLAGS = --std=c11
CXXFLAGS = --std=c++17

$(BUILD_DIR)/cobs_unittests: $(OBJS) Makefile
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.c.o: %.c Makefile
	mkdir -p $(dir $@) && $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cc.o: %.cc Makefile
	mkdir -p $(dir $@) && $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/cobs_unittests.timestamp: $(BUILD_DIR)/cobs_unittests
	$(BUILD_DIR)/cobs_unittests && touch $(BUILD_DIR)/cobs_unittests.timestamp

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

.DEFAULT_GOAL := $(BUILD_DIR)/cobs_unittests.timestamp

-include $(DEPS)

