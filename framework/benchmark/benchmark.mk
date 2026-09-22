# Source inventory shared by benchmark builds. The caller sets FRAMEWORK and
# owns its compiler flags, generated table, object directory and build rules.
PYTHON ?= python3
CLANG ?= clang-18
INSTRUMENT ?= 1
HWLOC_CFLAGS ?= $(shell pkg-config --cflags hwloc 2>/dev/null)
HWLOC_LIBS ?= $(shell pkg-config --libs hwloc 2>/dev/null || echo -lhwloc)
REGION_GENERATOR := $(FRAMEWORK)/region_control/instrument_regions.py
FRAMEWORK_C_SOURCES := $(FRAMEWORK)/region_control/region_control.c
FRAMEWORK_CXX_SOURCES := $(addprefix $(FRAMEWORK)/,benchmark/benchmark.cpp \
 hams/hams_binding.cpp tuner/tuner.cpp dummy/dummy.cpp offline/offline.cpp \
 j2025/j2025.cpp j2025_b/j2025_b.cpp otter/otter.cpp)
FRAMEWORK_HEADERS := $(foreach component,benchmark region_control tuner hams dummy offline j2025 j2025_b otter,$(wildcard $(FRAMEWORK)/$(component)/*.h))
FRAMEWORK_CPPFLAGS := -DREGION_INSTRUMENT=$(INSTRUMENT) \
 -I$(FRAMEWORK)/region_control -I$(FRAMEWORK)/benchmark $(HWLOC_CFLAGS)
FRAMEWORK_LIBS := -lstdc++ $(HWLOC_LIBS)
