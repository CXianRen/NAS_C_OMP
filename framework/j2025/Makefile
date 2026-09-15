CXX = clang++-18
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra
TARGET = j2025_ut

.PHONY: all test clean

all: $(TARGET)

$(TARGET): j2025_ut.cpp j2025.cpp j2025.h ../region_control/region_control.h ../hams/hams_binding.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG j2025_ut.cpp j2025.cpp -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
