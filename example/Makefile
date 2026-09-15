CC = clang-18
CXX = clang++-18
CFLAGS = -O2 -std=c11 -fopenmp -Wall -Wextra
CXXFLAGS = -O2 -std=c++17 -fopenmp -Wall -Wextra
LDFLAGS = -fopenmp
J2025_ENABLE ?= 1
THREADS ?= 8
BUILD_DIR ?= build
TARGET = $(BUILD_DIR)/example
CONFIG = $(BUILD_DIR)/config
C_OBJECTS = $(BUILD_DIR)/region_control.o
FRAMEWORK = ../framework
SOURCES = main.cpp
HEADERS = $(FRAMEWORK)/j2025/j2025_runtime.h $(FRAMEWORK)/region_control/region_control.h
ifeq ($(J2025_ENABLE),1)
SOURCES += $(FRAMEWORK)/j2025/j2025.cpp $(FRAMEWORK)/j2025/j2025_runtime.cpp \
	$(FRAMEWORK)/hams/hams_binding.cpp
HEADERS += $(FRAMEWORK)/j2025/j2025.h $(FRAMEWORK)/hams/hams_binding.h
endif

.PHONY: all run test clean FORCE

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS) $(C_OBJECTS) $(CONFIG) Makefile
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DJ2025_ENABLE=$(J2025_ENABLE) -UNDEBUG \
		$(SOURCES) $(C_OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/region_control.o: $(FRAMEWORK)/region_control/region_control.c $(FRAMEWORK)/region_control/region_control.h $(CONFIG) Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -c $< -o $@

FORCE:

$(CONFIG): FORCE | $(BUILD_DIR)
	@printf '%s\n' 'J2025_ENABLE=$(J2025_ENABLE)' 'CC=$(CC)' 'CXX=$(CXX)' \
		'CPPFLAGS=$(CPPFLAGS)' 'CFLAGS=$(CFLAGS)' 'CXXFLAGS=$(CXXFLAGS)' \
		'LDFLAGS=$(LDFLAGS)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && $(RM) "$@.tmp" || mv "$@.tmp" "$@"

$(BUILD_DIR):
	mkdir -p $@

run test: $(TARGET)
	env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
		OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=$(THREADS) $(TARGET)

clean:
	$(RM) -r $(BUILD_DIR)
