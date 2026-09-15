CXX = clang++-18
CXXFLAGS = -O2 -std=c++17 -fopenmp -Wall -Wextra
TARGET = region_control_ut
THREADS = 8
SOURCES = region_control_ut.cpp region_control.cpp ../hams/hams_binding.cpp
HEADERS = region_control.h ../hams/hams_binding.h

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG $(SOURCES) $(LDFLAGS) \
		-Wl,--wrap=omp_get_wtime -o $@

test: $(TARGET)
	env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
		OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=$(THREADS) ./$(TARGET)

clean:
	$(RM) $(TARGET)
