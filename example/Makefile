CC = clang-18
CXX = clang++-18
CFLAGS = -O2 -std=c11 -fopenmp -Wall -Wextra
CXXFLAGS = -O2 -std=c++17 -fopenmp -Wall -Wextra
LDFLAGS = -fopenmp
THREADS ?= 8
BUILD_DIR ?= build
TARGET = $(BUILD_DIR)/example
TIMER_OBJECT = $(BUILD_DIR)/region_timer.o
FRAMEWORK = ../framework
SOURCES = main.cpp $(FRAMEWORK)/j2025/j2025.cpp \
	$(FRAMEWORK)/region_control/region_control.cpp $(FRAMEWORK)/hams/hams_binding.cpp
HEADERS = $(FRAMEWORK)/j2025/j2025.h $(FRAMEWORK)/region_control/region_control.h \
	$(FRAMEWORK)/hams/hams_binding.h $(FRAMEWORK)/timer/region_timer.h

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS) $(TIMER_OBJECT) Makefile
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG $(SOURCES) $(TIMER_OBJECT) $(LDFLAGS) -o $@

$(TIMER_OBJECT): $(FRAMEWORK)/timer/region_timer.c $(FRAMEWORK)/timer/region_timer.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

run test: $(TARGET)
	env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
		OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=$(THREADS) $(TARGET)

clean:
	$(RM) -r $(BUILD_DIR)
