CC = clang-18
CXX = clang++-18
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
C_OBJECTS = $(BUILD_DIR)/region_control.o
FRAMEWORK = ../framework
SOURCES = main.cpp $(FRAMEWORK)/j2025/j2025.cpp $(FRAMEWORK)/otter/otter.cpp \
	$(FRAMEWORK)/tuner/tuner.cpp $(FRAMEWORK)/hams/hams_binding.cpp
HEADERS = $(wildcard $(FRAMEWORK)/j2025/*.h $(FRAMEWORK)/otter/*.h \
	$(FRAMEWORK)/tuner/*.h $(FRAMEWORK)/hams/*.h $(FRAMEWORK)/region_control/*.h)

.PHONY: all check run test clean FORCE

all: check $(TARGET)

check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac

$(TARGET): $(SOURCES) $(HEADERS) $(C_OBJECTS) $(CONFIG) Makefile | check
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HWLOC_CFLAGS) -DREGION_INSTRUMENT=$(INSTRUMENT) -UNDEBUG \
		$(SOURCES) $(C_OBJECTS) $(LDFLAGS) $(HWLOC_LIBS) -o $@

$(BUILD_DIR)/region_control.o: $(FRAMEWORK)/region_control/region_control.c $(FRAMEWORK)/region_control/region_control.h $(CONFIG) Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DREGION_INSTRUMENT=$(INSTRUMENT) -UNDEBUG -c $< -o $@

FORCE:

$(CONFIG): FORCE | $(BUILD_DIR)
	@printf '%s\n' 'INSTRUMENT=$(INSTRUMENT)' 'CC=$(CC)' 'CXX=$(CXX)' \
		'CPPFLAGS=$(CPPFLAGS)' 'CFLAGS=$(CFLAGS)' 'CXXFLAGS=$(CXXFLAGS)' \
		'LDFLAGS=$(LDFLAGS)' 'HWLOC_CFLAGS=$(HWLOC_CFLAGS)' \
		'HWLOC_LIBS=$(HWLOC_LIBS)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && $(RM) "$@.tmp" || mv "$@.tmp" "$@"

$(BUILD_DIR):
	mkdir -p $@

run test: $(TARGET)
	env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
		OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=$(THREADS) \
		TUNER="$(TUNER)" $(TARGET)

clean:
	$(RM) -r $(BUILD_DIR)
