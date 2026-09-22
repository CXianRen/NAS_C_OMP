# Non-MPI LULESH with source-generated OpenMP hooks and the shared tuner runtime.
SHELL = /bin/sh
.DEFAULT_GOAL := all
CC = clang-18
CXX = clang++-18
CLANG ?= $(if $(findstring clang,$(CC)),$(CC),clang-18)
PYTHON ?= python3
BUILD_DIR ?= build
USE_MPI ?= 0
INSTRUMENT ?= 1
FRAMEWORK ?= ../framework
include $(FRAMEWORK)/benchmark/benchmark.mk

CFLAGS = -O3 -std=c11 -fopenmp -Wall
CXXFLAGS = -O3 -std=c++17 -fopenmp -Wall
LDFLAGS = -fopenmp
APP_SOURCES := $(sort $(wildcard *.cc))
APP_HEADERS := $(wildcard *.h)
GENERATED_DIR := $(BUILD_DIR)/generated
BUILD_CONFIG := $(BUILD_DIR)/build.config
LULESH_EXEC := $(BUILD_DIR)/lulesh2.0
APP_CPPFLAGS = $(CPPFLAGS) -DUSE_MPI=$(USE_MPI) $(FRAMEWORK_CPPFLAGS) \
	-I. -I"$(GENERATED_DIR)" -include "$(FRAMEWORK)/benchmark/benchmark.h"
GENERATED_FILES := $(addprefix $(GENERATED_DIR)/,$(APP_SOURCES)) \
	$(addprefix $(GENERATED_DIR)/,region_auto.h region_auto.c instrumentation.json instrumentation.d .region-auto-owner.json)
APP_OBJECTS := $(addprefix $(BUILD_DIR)/app/,$(APP_SOURCES:.cc=.o))
FRAMEWORK_C_OBJECTS := $(patsubst $(FRAMEWORK)/%.c,$(BUILD_DIR)/framework/%.o,$(FRAMEWORK_C_SOURCES))
FRAMEWORK_CXX_OBJECTS := $(patsubst $(FRAMEWORK)/%.cpp,$(BUILD_DIR)/framework/%.o,$(FRAMEWORK_CXX_SOURCES))
OBJECTS := $(APP_OBJECTS) $(BUILD_DIR)/region_auto.o $(FRAMEWORK_C_OBJECTS) $(FRAMEWORK_CXX_OBJECTS)

ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(wildcard $(GENERATED_DIR)/instrumentation.d)
endif

.PHONY: all check clean instrument-test FORCE
all: check $(LULESH_EXEC)

check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac
	@test "$(USE_MPI)" = 0 || { echo "This shared-tuner build supports USE_MPI=0"; exit 2; }

FORCE:

$(BUILD_CONFIG): Makefile $(FRAMEWORK)/benchmark/benchmark.mk FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' 'CC=$(CC)' 'CXX=$(CXX)' 'CLANG=$(CLANG)' 'PYTHON=$(PYTHON)' \
		'CPPFLAGS=$(APP_CPPFLAGS)' 'CFLAGS=$(CFLAGS)' 'CXXFLAGS=$(CXXFLAGS)' \
		'LDFLAGS=$(LDFLAGS)' 'LIBS=$(FRAMEWORK_LIBS)' 'INSTRUMENT=$(INSTRUMENT)' \
		'FRAMEWORK=$(FRAMEWORK)' 'SOURCES=$(APP_SOURCES)' \
		'FRAMEWORK_SOURCES=$(FRAMEWORK_C_SOURCES) $(FRAMEWORK_CXX_SOURCES)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

# Both modes generate identical descriptors; REGION_INSTRUMENT disables hooks.
$(GENERATED_FILES) &: $(APP_SOURCES) $(APP_HEADERS) $(FRAMEWORK_HEADERS) $(REGION_GENERATOR) $(BUILD_CONFIG) Makefile | check
	$(PYTHON) $(REGION_GENERATOR) $(APP_SOURCES) --clang "$(CLANG)" \
		--output "$(GENERATED_DIR)" --control '&benchmark_control' \
		--depfile "$(GENERATED_DIR)/instrumentation.d" -- $(APP_CPPFLAGS) $(CXXFLAGS)

$(BUILD_DIR)/app/%.o: $(GENERATED_DIR)/%.cc $(GENERATED_FILES) $(APP_HEADERS) $(FRAMEWORK_HEADERS) $(BUILD_CONFIG) | check
	@mkdir -p "$(@D)"
	$(CXX) $(APP_CPPFLAGS) $(CXXFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/region_auto.o: $(GENERATED_DIR)/region_auto.c $(GENERATED_FILES) $(FRAMEWORK_HEADERS) $(BUILD_CONFIG) | check
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(FRAMEWORK_CPPFLAGS) $(CFLAGS) -I"$(GENERATED_DIR)" -c "$<" -o "$@"

$(BUILD_DIR)/framework/%.o: $(FRAMEWORK)/%.c $(FRAMEWORK_HEADERS) $(BUILD_CONFIG) | check
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(FRAMEWORK_CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/framework/%.o: $(FRAMEWORK)/%.cpp $(FRAMEWORK_HEADERS) $(BUILD_CONFIG) | check
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(FRAMEWORK_CPPFLAGS) $(CXXFLAGS) -c "$<" -o "$@"

$(LULESH_EXEC): $(OBJECTS) $(BUILD_CONFIG) | check
	$(CXX) $(OBJECTS) $(LDFLAGS) $(FRAMEWORK_LIBS) -lm -o "$@"

instrument-test:
	$(PYTHON) tests/test_automatic_build.py --cc "$(CC)" --cxx "$(CXX)" --clang "$(CLANG)"

clean:
	$(RM) -r "$(BUILD_DIR)"
