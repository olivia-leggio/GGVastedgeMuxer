CXX := g++
CPP_VERSION := c++20
TARGET_EXEC := vastedge-muxer

BUILD_DIR := ./build
SRC_DIRS := .
LIB_DIR := ./lib
INC_PARENT_DIR := ./include

MODE ?= debug

INC_DIRS := $(shell find $(INC_PARENT_DIR) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))
INC_FLAGS := -I$(INC_PARENT_DIR)
LDFLAGS := -Llib -Wl,-rpath='$$ORIGIN/../../lib' -lswresample -lavformat -lavcodec -lavutil -lswscale

ifeq ($(MODE), debug)
	X_BUILD_DIR := $(BUILD_DIR)/debug
	CPP_FLAGS := $(INC_FLAGS) -g -O0 -DDEBUG -Wall -Wextra -MMD -MP -std=$(CPP_VERSION)
else ifeq ($(MODE), release)
	X_BUILD_DIR := $(BUILD_DIR)/release
	CPP_FLAGS := $(INC_FLAGS) -O3 -Wall -Wextra -MMD -MP -std=$(CPP_VERSION)
else
	$(error Unknown build option: $(MODE). Use 'debug' or 'release')
endif

SRCS := $(shell find $(SRC_DIRS) -name '*.cpp')

OBJS := $(SRCS:%=$(X_BUILD_DIR)/%.o)

DEPS := $(OBJS:.o=.d)


$(X_BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@

$(X_BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPP_FLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -r $(BUILD_DIR)


-include $(DEPS)
