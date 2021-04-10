SRCS := ./src/cobs.c ./src/cobs_test.cc ./src/unittest_main.cc

BUILD_DIR ?= ./build
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

CPPFLAGS ?= -MMD -MP -Os -Wall -Werror -Wextra
CFLAGS = --std=c11
CXXFLAGS = --std=c++17

MKDIR_P ?= mkdir -p

$(BUILD_DIR)/cobs_unittests: $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@) && $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cc.o: %.cc
	$(MKDIR_P) $(dir $@) && $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/cobs_unittests.timestamp: $(BUILD_DIR)/cobs_unittests
	$(BUILD_DIR)/cobs_unittests && touch $(BUILD_DIR)/cobs_unittests.timestamp

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

.DEFAULT_GOAL := $(BUILD_DIR)/cobs_unittests.timestamp

-include $(DEPS)

