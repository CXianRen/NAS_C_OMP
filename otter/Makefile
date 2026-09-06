.DEFAULT_GOAL := all

CC = clang-18
BUILD_DIR ?= build

BINDING_TARGET = $(BUILD_DIR)/otter_tuner_binding_correctness
SEARCH_TARGET = $(BUILD_DIR)/otter_tuner_search_correctness
CORRECTNESS_TARGETS = $(BINDING_TARGET) $(SEARCH_TARGET)
OVERHEAD_TARGET = $(BUILD_DIR)/otter_tuner_overhead
TARGETS = $(CORRECTNESS_TARGETS) $(OVERHEAD_TARGET)
COMMON_OBJECTS = \
	$(BUILD_DIR)/otter_tuner.o \
	$(BUILD_DIR)/otter_tuner_test_common.o
BINDING_OBJECTS = \
	$(BUILD_DIR)/otter_tuner_test_common.o \
	$(BUILD_DIR)/otter_tuner_binding_correctness.o
SEARCH_OBJECTS = $(BUILD_DIR)/otter_tuner_search_correctness.o
OVERHEAD_OBJECTS = \
	$(COMMON_OBJECTS) \
	$(BUILD_DIR)/otter_tuner_overhead.o
LEGACY_ARTIFACTS = \
	$(BUILD_DIR)/otter_tuner_correctness.o \
	$(BUILD_DIR)/otter_tuner_correctness \
	$(BUILD_DIR)/otter_tuner_ut.o \
	$(BUILD_DIR)/otter_tuner_ut

CFLAGS = -O2 -g -fopenmp -I. -Itests -Wall
LDFLAGS = -fopenmp
HWLOC_CFLAGS ?=
HWLOC_LIBS ?= -lhwloc

.PHONY: all correctness binding-correctness search-correctness overhead clean

all: $(TARGETS)

correctness: $(CORRECTNESS_TARGETS)

binding-correctness: $(BINDING_TARGET)

search-correctness: $(SEARCH_TARGET)

overhead: $(OVERHEAD_TARGET)

$(BUILD_DIR):
	@mkdir -p "$@"

$(BUILD_DIR)/otter_tuner.o: otter_tuner.c otter_tuner.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(HWLOC_CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/otter_tuner_test_common.o: \
		tests/otter_tuner_test_common.c \
		tests/otter_tuner_test_common.h \
		otter_tuner.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(HWLOC_CFLAGS) -c "$<" -o "$@"

# Correctness tests include the implementation to exercise private functions.
$(BUILD_DIR)/otter_tuner_binding_correctness.o: \
		tests/otter_tuner_binding_correctness.c \
		tests/otter_tuner_test_common.h \
		otter_tuner.c otter_tuner.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(HWLOC_CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/otter_tuner_overhead.o: \
		tests/otter_tuner_overhead.c \
		tests/otter_tuner_test_common.h \
		otter_tuner.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(HWLOC_CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/otter_tuner_search_correctness.o: \
		tests/otter_tuner_search_correctness.c \
		otter_tuner.c otter_tuner.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(HWLOC_CFLAGS) -c "$<" -o "$@"

$(BINDING_TARGET): $(BINDING_OBJECTS)
	$(CC) $(BINDING_OBJECTS) $(LDFLAGS) $(HWLOC_LIBS) -lm -o "$@"

$(SEARCH_TARGET): $(SEARCH_OBJECTS)
	$(CC) $(SEARCH_OBJECTS) $(LDFLAGS) $(HWLOC_LIBS) -lm -o "$@"

$(OVERHEAD_TARGET): $(OVERHEAD_OBJECTS)
	$(CC) $(OVERHEAD_OBJECTS) $(LDFLAGS) $(HWLOC_LIBS) -lm -o "$@"

clean:
	$(RM) $(COMMON_OBJECTS) \
		$(BUILD_DIR)/otter_tuner_binding_correctness.o \
		$(BUILD_DIR)/otter_tuner_search_correctness.o \
		$(BUILD_DIR)/otter_tuner_overhead.o \
		$(TARGETS) $(LEGACY_ARTIFACTS)
	@rmdir "$(BUILD_DIR)" 2>/dev/null || true
