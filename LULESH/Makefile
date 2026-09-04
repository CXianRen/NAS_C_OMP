# Default non-MPI OpenMP build with Clang.

SHELL = /bin/sh
.SUFFIXES: .cc .o
.DEFAULT_GOAL := all

BUILD_DIR ?= build
LULESH_EXEC = $(BUILD_DIR)/lulesh2.0

CC = clang-18
CXX = clang++ -DUSE_MPI=0

SOURCES2.0 = \
	lulesh.cc \
	lulesh-comm.cc \
	lulesh-viz.cc \
	lulesh-util.cc \
	lulesh-init.cc
OTTER_DIR = ../otter
OTTER_SOURCE = $(OTTER_DIR)/otter_tuner.c
OTTER_HEADER = $(OTTER_DIR)/otter_tuner.h
OBJECTS2.0 = $(addprefix $(BUILD_DIR)/,$(SOURCES2.0:.cc=.o)) \
	$(BUILD_DIR)/otter_tuner.o

# Default OpenMP flags
CFLAGS = -g -O3 -fopenmp -I. -I$(OTTER_DIR) -Wall
CXXFLAGS = -g -O3 -fopenmp -I. -I$(OTTER_DIR) -Wall
LDFLAGS = -g -O3 -fopenmp
HWLOC_LIBS = -lhwloc

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

$(BUILD_DIR):
	@mkdir -p "$@"

$(BUILD_DIR)/%.o: %.cc lulesh.h lulesh_tuple.h $(OTTER_HEADER) | $(BUILD_DIR)
	@echo "Building $<"
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(BUILD_DIR)/otter_tuner.o: $(OTTER_SOURCE) $(OTTER_HEADER) | $(BUILD_DIR)
	@echo "Building $<"
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: all clean tar

all: $(LULESH_EXEC)

$(LULESH_EXEC): $(OBJECTS2.0) | $(BUILD_DIR)
	@echo "Linking"
	$(CXX) $(OBJECTS2.0) $(LDFLAGS) $(HWLOC_LIBS) -lm -o $@

clean:
	$(RM) $(OBJECTS2.0) $(LULESH_EXEC)
	@rmdir "$(BUILD_DIR)" 2>/dev/null || true

tar: clean
	cd .. ; tar cvf lulesh-2.0.tar LULESH-2.0 ; mv lulesh-2.0.tar LULESH-2.0
