CC = clang-18
CXX = $(if $(findstring clang,$(CC)),$(subst clang,clang++,$(CC)),g++)
CFLAGS = -O2 -std=c11 -fopenmp -Wall -Wextra
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra
THREADS ?= 8
BUILD_DIR ?= build
HAMS = ../hams
J2025 = ../j2025
CONTROL = ../region_control
CONTROL_TESTS = $(BUILD_DIR)/region_control_ut $(BUILD_DIR)/region_control_ut_noinstrument
TARGETS = $(BUILD_DIR)/hams_binding_ut $(BUILD_DIR)/j2025_ut \
	$(BUILD_DIR)/j2025_runtime_ut $(CONTROL_TESTS)
RUN_OMP = env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
	OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=$(THREADS)

.PHONY: all test hams-test j2025-test region-control-test clean

all: $(TARGETS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/hams_binding_ut: hams_binding_ut.cpp $(HAMS)/hams_binding.cpp $(HAMS)/hams_binding.h Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fopenmp -UNDEBUG hams_binding_ut.cpp \
		$(HAMS)/hams_binding.cpp $(LDFLAGS) -o $@

# 搜索 UT 使用 mock 耗时，不依赖 OpenMP runtime 或真实绑定。
$(BUILD_DIR)/j2025_ut: j2025_ut.cpp $(J2025)/j2025.cpp $(J2025)/j2025.h $(HAMS)/hams_binding.h $(CONTROL)/region_control.h Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG j2025_ut.cpp $(J2025)/j2025.cpp $(LDFLAGS) -o $@

$(BUILD_DIR)/region_control.o: $(CONTROL)/region_control.c $(CONTROL)/region_control.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -c $< -o $@

$(BUILD_DIR)/j2025_runtime_ut: j2025_runtime_ut.cpp $(J2025)/j2025_runtime.cpp \
	$(J2025)/j2025_runtime.h $(J2025)/j2025.h $(HAMS)/hams_binding.cpp $(HAMS)/hams_binding.h \
	$(CONTROL)/region_control.h $(BUILD_DIR)/region_control.o Makefile
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fopenmp -UNDEBUG j2025_runtime_ut.cpp \
		$(J2025)/j2025_runtime.cpp $(HAMS)/hams_binding.cpp $(BUILD_DIR)/region_control.o \
		$(LDFLAGS) -Wl,--wrap=omp_get_wtime -o $@

# 同一份桩 UT 分别检查启用和关闭插桩的行为。
$(CONTROL_TESTS): region_control_ut.c $(CONTROL)/region_control.c $(CONTROL)/region_control.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG \
		$(if $(findstring noinstrument,$@),-DREGION_INSTRUMENT=0) \
		region_control_ut.c $(CONTROL)/region_control.c $(LDFLAGS) \
		-Wl,--wrap=omp_get_wtime -o $@

test: hams-test j2025-test region-control-test

hams-test: $(BUILD_DIR)/hams_binding_ut
	$(RUN_OMP) $<

j2025-test: $(BUILD_DIR)/j2025_ut $(BUILD_DIR)/j2025_runtime_ut
	$(BUILD_DIR)/j2025_ut
	$(RUN_OMP) $(BUILD_DIR)/j2025_runtime_ut

region-control-test: $(CONTROL_TESTS)
	$(RUN_OMP) $(BUILD_DIR)/region_control_ut
	$(RUN_OMP) $(BUILD_DIR)/region_control_ut_noinstrument
	python3 test_nowait.py --cc "$(CC)"

clean:
	$(RM) -r $(BUILD_DIR)
