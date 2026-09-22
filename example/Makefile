CC = clang-18
CXX = clang++-18
CLANG ?= $(if $(findstring clang,$(CC)),$(CC),clang-18)
PYTHON ?= python3
CFLAGS = -O2 -std=c11 -fopenmp -Wall -Wextra
CXXFLAGS = -O2 -std=c++17 -fopenmp -Wall -Wextra
LDFLAGS = -fopenmp
INSTRUMENT ?= 1
TUNER ?= none
THREADS ?= 8
HWLOC_CFLAGS ?= $(shell pkg-config --cflags hwloc 2>/dev/null)
HWLOC_LIBS ?= $(shell pkg-config --libs hwloc 2>/dev/null || echo -lhwloc)
BUILD_DIR ?= build
TARGET = $(BUILD_DIR)/example
CONFIG = $(BUILD_DIR)/config
C_OBJECTS = $(BUILD_DIR)/region_control.o $(BUILD_DIR)/region_auto.o
FRAMEWORK = ../framework
REGION_GENERATOR = $(FRAMEWORK)/region_control/instrument_regions.py
APP_SOURCES = main.cpp
CONTROL_SOURCES = $(FRAMEWORK)/j2025/j2025.cpp $(FRAMEWORK)/j2025_b/j2025_b.cpp \
	$(FRAMEWORK)/otter/otter.cpp \
	$(FRAMEWORK)/dummy/dummy.cpp $(FRAMEWORK)/offline/offline.cpp \
	$(FRAMEWORK)/tuner/tuner.cpp $(FRAMEWORK)/hams/hams_binding.cpp
SOURCES = $(APP_SOURCES) $(CONTROL_SOURCES)
GENERATED_DIR = $(BUILD_DIR)/generated
GENERATED_SOURCES = $(addprefix $(GENERATED_DIR)/,$(notdir $(APP_SOURCES)))
GENERATED_FILES = $(GENERATED_SOURCES) \
	$(addprefix $(GENERATED_DIR)/,region_auto.h region_auto.c instrumentation.json instrumentation.d .region-auto-owner.json)
HEADERS = $(wildcard $(FRAMEWORK)/j2025/*.h $(FRAMEWORK)/j2025_b/*.h $(FRAMEWORK)/otter/*.h \
	$(FRAMEWORK)/dummy/*.h $(FRAMEWORK)/offline/*.h $(FRAMEWORK)/tuner/*.h \
	$(FRAMEWORK)/hams/*.h $(FRAMEWORK)/region_control/*.h)

ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(wildcard $(GENERATED_DIR)/instrumentation.d)
endif

.DEFAULT_GOAL := all

.PHONY: all check run test clean FORCE

all: check $(TARGET)

check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac

$(TARGET): $(SOURCES) $(HEADERS) $(GENERATED_FILES) $(C_OBJECTS) $(CONFIG) Makefile | check
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HWLOC_CFLAGS) -I"$(GENERATED_DIR)" \
		-iquote . -I"$(FRAMEWORK)/region_control" -DREGION_INSTRUMENT=$(INSTRUMENT) -UNDEBUG \
		$(GENERATED_SOURCES) $(CONTROL_SOURCES) $(C_OBJECTS) $(LDFLAGS) $(HWLOC_LIBS) -o $@

$(BUILD_DIR)/region_control.o: $(FRAMEWORK)/region_control/region_control.c $(FRAMEWORK)/region_control/region_control.h $(CONFIG) Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DREGION_INSTRUMENT=$(INSTRUMENT) -UNDEBUG -c $< -o $@

$(BUILD_DIR)/region_auto.o: $(GENERATED_FILES) $(CONFIG) Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I"$(GENERATED_DIR)" -I"$(FRAMEWORK)/region_control" \
		-DREGION_INSTRUMENT=$(INSTRUMENT) -UNDEBUG -c "$(GENERATED_DIR)/region_auto.c" -o "$@"

$(GENERATED_FILES) &: $(APP_SOURCES) $(HEADERS) $(CONFIG) $(REGION_GENERATOR) Makefile | $(BUILD_DIR)
	$(PYTHON) $(REGION_GENERATOR) $(APP_SOURCES) --clang "$(CLANG)" \
		--output "$(GENERATED_DIR)" --control '&control' --depfile "$(GENERATED_DIR)/instrumentation.d" -- \
		$(CPPFLAGS) $(CXXFLAGS) $(HWLOC_CFLAGS) -I"$(FRAMEWORK)/region_control" \
		-DREGION_INSTRUMENT=$(INSTRUMENT) -UNDEBUG

FORCE:

$(CONFIG): FORCE | $(BUILD_DIR)
	@printf '%s\n' 'INSTRUMENT=$(INSTRUMENT)' 'CC=$(CC)' 'CXX=$(CXX)' \
		'CLANG=$(CLANG)' 'PYTHON=$(PYTHON)' \
		'CPPFLAGS=$(CPPFLAGS)' 'CFLAGS=$(CFLAGS)' 'CXXFLAGS=$(CXXFLAGS)' \
		'LDFLAGS=$(LDFLAGS)' 'HWLOC_CFLAGS=$(HWLOC_CFLAGS)' \
		'HWLOC_LIBS=$(HWLOC_LIBS)' 'SOURCES=$(SOURCES)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && $(RM) "$@.tmp" || mv "$@.tmp" "$@"

$(BUILD_DIR):
	mkdir -p $@

run test: $(TARGET)
	env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
		OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=$(THREADS) \
		TUNER="$(TUNER)" $(TARGET)

clean:
	$(RM) -r $(BUILD_DIR)
