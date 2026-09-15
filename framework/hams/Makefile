CXX = clang++-18
CXXFLAGS = -O2 -std=c++17 -fopenmp -Wall -Wextra
TARGET = hams_binding_ut
THREADS = 8

.PHONY: all test clean

all: $(TARGET)

$(TARGET): hams_binding_ut.cpp hams_binding.cpp hams_binding.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG hams_binding_ut.cpp hams_binding.cpp -o $@

test: $(TARGET)
	env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
		OMP_PROC_BIND=false OMP_NUM_THREADS=$(THREADS) ./$(TARGET)

clean:
	$(RM) $(TARGET)
