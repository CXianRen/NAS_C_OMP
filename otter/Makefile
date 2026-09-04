.DEFAULT_GOAL := all

CC = clang-18
BUILD_DIR ?= build
TARGET = $(BUILD_DIR)/otter_tuner_ut
OBJECTS = $(BUILD_DIR)/otter_tuner.o $(BUILD_DIR)/otter_tuner_ut.o

CFLAGS = -O2 -g -fopenmp -I. -Wall
LDFLAGS = -fopenmp
HWLOC_CFLAGS ?=
HWLOC_LIBS ?= -lhwloc

.PHONY: all clean

all: $(TARGET)

$(BUILD_DIR):
	@mkdir -p "$@"

$(BUILD_DIR)/otter_tuner.o: otter_tuner.c otter_tuner.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(HWLOC_CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/otter_tuner_ut.o: tests/otter_tuner_ut.c otter_tuner.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(HWLOC_CFLAGS) -c "$<" -o "$@"

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(HWLOC_LIBS) -lm -o "$@"

clean:
	$(RM) $(OBJECTS) $(TARGET)
	@rmdir "$(BUILD_DIR)" 2>/dev/null || true
