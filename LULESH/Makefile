# Default non-MPI OpenMP build with Clang.

SHELL = /bin/sh
.SUFFIXES: .cc .o
.DEFAULT_GOAL := all

BUILD_DIR ?= build
LULESH_EXEC = $(BUILD_DIR)/lulesh2.0

CC = clang
CXX = clang++
CLANG ?= clang
PYTHON ?= python3
USE_MPI ?= 0
INSTRUMENT ?= 1
INSTRUMENT_FLAGS ?= $(CPPFLAGS) $(filter -D% -U% -I% -std=%,$(CXXFLAGS))
NPB_DIR := ../NPB3.3-OMP-C
COMMON_DIR := $(NPB_DIR)/common
INSTRUMENT_TOOL := $(NPB_DIR)/tools/instrument_regions.py
GENERATED_DIR := $(BUILD_DIR)/instrumented
BUILD_CONFIG := $(BUILD_DIR)/build.config
BUILD_CPPFLAGS = -DUSE_MPI=$(USE_MPI) -DNPB_REGION_TIMING=$(INSTRUMENT) $(CPPFLAGS)

SOURCES2.0 = \
	lulesh.cc \
	lulesh-comm.cc \
	lulesh-viz.cc \
	lulesh-util.cc \
	lulesh-init.cc
OBJECTS2.0 = $(addprefix $(BUILD_DIR)/,$(SOURCES2.0:.cc=.o))

# Default OpenMP flags
CXXFLAGS = -g -O3 -fopenmp -I. -Wall
CFLAGS ?= -g -O3 -fopenmp -Wall
LDFLAGS = -g -O3 -fopenmp

#Below are reasonable default flags for a serial build
#CXXFLAGS = -g -O3 -I. -Wall
#LDFLAGS = -g -O3 

#common places you might find silo on the Livermore machines.
#SILO_INCDIR = /opt/local/include
#SILO_LIBDIR = /opt/local/lib
#SILO_INCDIR = ./silo/4.9/1.8.10.1/include
#SILO_LIBDIR = ./silo/4.9/1.8.10.1/lib

#If you do not have silo and visit you can get them at:
#silo:  https://wci.llnl.gov/codes/silo/downloads.html
#visit: https://wci.llnl.gov/codes/visit/download.html

#below is and example of how to make with silo, hdf5 to get vizulization by default all this is turned off.  All paths are Livermore specific.
#CXXFLAGS = -g -DVIZ_MESH -I${SILO_INCDIR} -Wall -Wno-pragmas
#LDFLAGS = -g -L${SILO_LIBDIR} -Wl,-rpath -Wl,${SILO_LIBDIR} -lsiloh5 -lhdf5

.PHONY: all check clean tar instrument-test FORCE

check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac

$(BUILD_DIR):
	@mkdir -p "$@"

FORCE:

$(BUILD_CONFIG): Makefile FORCE | $(BUILD_DIR)
	@printf '%s\n' \
		'CC = $(CC)' 'CXX = $(CXX)' 'CPPFLAGS = $(BUILD_CPPFLAGS)' \
		'CFLAGS = $(CFLAGS)' 'CXXFLAGS = $(CXXFLAGS)' 'LDFLAGS = $(LDFLAGS)' \
		'CLANG = $(CLANG)' 'PYTHON = $(PYTHON)' 'INSTRUMENT = $(INSTRUMENT)' \
		'INSTRUMENT_FLAGS = $(INSTRUMENT_FLAGS)' 'SOURCES = $(SOURCES2.0)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

ifeq ($(INSTRUMENT),1)
GENERATED_FILES := $(addprefix $(GENERATED_DIR)/,$(SOURCES2.0)) \
	$(GENERATED_DIR)/npb_generated_regions.h $(GENERATED_DIR)/npb_generated_regions.c \
	$(GENERATED_DIR)/instrumentation.json
OBJECTS2.0 += $(BUILD_DIR)/npb_generated_regions.o $(BUILD_DIR)/region_timers.o

# One invocation publishes all generated files before any compiler starts.
$(GENERATED_FILES) &: $(SOURCES2.0) lulesh.h lulesh_tuple.h $(COMMON_DIR)/region_timers.h $(INSTRUMENT_TOOL) $(BUILD_CONFIG) | check
	$(PYTHON) $(INSTRUMENT_TOOL) $(SOURCES2.0) \
		--clang "$(CLANG)" --output "$(GENERATED_DIR)" -- \
		-DUSE_MPI=$(USE_MPI) -DNPB_REGION_TIMING=1 -I. $(INSTRUMENT_FLAGS)

$(BUILD_DIR)/%.o: $(GENERATED_DIR)/%.cc $(GENERATED_DIR)/npb_generated_regions.h $(BUILD_CONFIG) | check
	@echo "Building $<"
	$(CXX) $(BUILD_CPPFLAGS) $(CXXFLAGS) -I"$(COMMON_DIR)" -c -o "$@" "$<"

$(BUILD_DIR)/npb_generated_regions.o: $(GENERATED_DIR)/npb_generated_regions.c $(GENERATED_DIR)/npb_generated_regions.h $(BUILD_CONFIG) | check
	$(CC) $(BUILD_CPPFLAGS) $(CFLAGS) -I"$(COMMON_DIR)" -c -o "$@" "$<"

$(BUILD_DIR)/region_timers.o: $(COMMON_DIR)/region_timers.c $(COMMON_DIR)/region_timers.h $(BUILD_CONFIG) | check
	$(CC) $(BUILD_CPPFLAGS) $(CFLAGS) -c -o "$@" "$<"
else
$(BUILD_DIR)/%.o: %.cc lulesh.h lulesh_tuple.h $(BUILD_CONFIG) | check
	@echo "Building $<"
	$(CXX) $(BUILD_CPPFLAGS) $(CXXFLAGS) -c -o "$@" "$<"
endif

all: check $(LULESH_EXEC)

$(LULESH_EXEC): $(OBJECTS2.0) $(BUILD_CONFIG) | check
	@echo "Linking"
	$(CXX) $(OBJECTS2.0) $(LDFLAGS) -lm -o $@

clean:
	$(RM) $(addprefix $(BUILD_DIR)/,$(SOURCES2.0:.cc=.o)) \
		$(BUILD_DIR)/npb_generated_regions.o $(BUILD_DIR)/region_timers.o \
		$(LULESH_EXEC) $(BUILD_CONFIG) $(BUILD_CONFIG).tmp
	rm -rf "$(GENERATED_DIR)"
	@rmdir "$(BUILD_DIR)" 2>/dev/null || true

instrument-test:
	$(PYTHON) tests/test_automatic_build.py --cc "$(CC)" --cxx "$(CXX)" --clang "$(CLANG)"

tar: clean
	cd .. ; tar cvf lulesh-2.0.tar LULESH-2.0 ; mv lulesh-2.0.tar LULESH-2.0
