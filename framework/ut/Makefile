CC = clang-18
CXX = $(if $(findstring clang,$(CC)),$(subst clang,clang++,$(CC)),g++)
CFLAGS = -O2 -std=c11 -fopenmp -Wall -Wextra
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra
THREADS ?= 8
BUILD_DIR ?= build
HAMS = ../hams
J2025 = ../j2025
OTTER = ../otter
DUMMY = ../dummy
TUNER_DIR = ../tuner
CONTROL = ../region_control
HWLOC_CFLAGS ?= $(shell pkg-config --cflags hwloc 2>/dev/null)
HWLOC_LIBS ?= $(shell pkg-config --libs hwloc 2>/dev/null || echo -lhwloc)
CONTROL_TESTS = $(BUILD_DIR)/region_control_ut $(BUILD_DIR)/region_control_ut_noinstrument
TARGETS = $(BUILD_DIR)/hams_binding_ut $(BUILD_DIR)/j2025_ut \
	$(BUILD_DIR)/otter_ut $(BUILD_DIR)/dummy_ut $(BUILD_DIR)/tuner_runtime_ut \
	$(BUILD_DIR)/j2025_runtime_ut $(CONTROL_TESTS)
RUN_OMP = env -u OMP_PLACES -u KMP_AFFINITY -u GOMP_CPU_AFFINITY \
	-u TUNER -u OTTER_PIN_THREADS -u OTTER_PHYSICAL_CORES -u OTTER_MAX_THREADS \
	-u OTTER_THRESHOLD_PERCENT -u OTTER_GOLDEN_DISTANCE \
	OMP_PROC_BIND=false OMP_DYNAMIC=false OMP_NUM_THREADS=$(THREADS)

.PHONY: all test hams-test j2025-test otter-test dummy-test tuner-test region-control-test clean

all: $(TARGETS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/hams_binding_ut: hams_binding_ut.cpp $(HAMS)/hams_binding.cpp $(HAMS)/hams_binding.h Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fopenmp -UNDEBUG hams_binding_ut.cpp \
		$(HAMS)/hams_binding.cpp $(LDFLAGS) -o $@

# 搜索 UT 使用 mock 耗时，不依赖 OpenMP runtime 或真实绑定。
$(BUILD_DIR)/j2025_ut: j2025_ut.cpp $(J2025)/j2025.cpp $(J2025)/j2025.h $(HAMS)/hams_binding.h $(CONTROL)/region_control.h Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG j2025_ut.cpp $(J2025)/j2025.cpp $(LDFLAGS) -o $@

# 白盒测试直接包含 otter.cpp，保持纯搜索测试无 OpenMP/hwloc 链接依赖。
$(BUILD_DIR)/otter_ut: otter_ut.cpp $(OTTER)/otter.cpp $(OTTER)/otter.h $(HAMS)/hams_binding.h $(CONTROL)/region_control.h Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG otter_ut.cpp $(LDFLAGS) -o $@

$(BUILD_DIR)/dummy_ut: dummy_ut.cpp $(DUMMY)/dummy.cpp $(DUMMY)/dummy.h $(HAMS)/hams_binding.h Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG dummy_ut.cpp $(DUMMY)/dummy.cpp $(LDFLAGS) -o $@

$(BUILD_DIR)/region_control.o: $(CONTROL)/region_control.c $(CONTROL)/region_control.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -c $< -o $@

$(BUILD_DIR)/j2025_runtime_ut: j2025_runtime_ut.cpp $(J2025)/j2025_runtime.cpp \
	$(J2025)/j2025_runtime.h $(J2025)/j2025.h $(HAMS)/hams_binding.cpp $(HAMS)/hams_binding.h \
	$(TUNER_DIR)/tuner.cpp $(TUNER_DIR)/tuner.h $(OTTER)/otter.cpp $(OTTER)/otter.h \
	$(DUMMY)/dummy.cpp $(DUMMY)/dummy.h \
	$(CONTROL)/region_control.h $(BUILD_DIR)/region_control.o Makefile
	$(CXX) $(CPPFLAGS) $(HWLOC_CFLAGS) $(CXXFLAGS) -fopenmp -UNDEBUG j2025_runtime_ut.cpp \
		$(J2025)/j2025_runtime.cpp $(TUNER_DIR)/tuner.cpp $(OTTER)/otter.cpp $(DUMMY)/dummy.cpp \
		$(HAMS)/hams_binding.cpp $(BUILD_DIR)/region_control.o \
		$(LDFLAGS) $(HWLOC_LIBS) -Wl,--wrap=omp_get_wtime -o $@

# mock 两种搜索策略，Dummy 使用真实实现；挂载、线程绑定和采样均使用公共框架。
$(BUILD_DIR)/tuner_runtime_ut: tuner_runtime_ut.cpp $(TUNER_DIR)/tuner.cpp $(TUNER_DIR)/tuner.h \
	$(J2025)/j2025.h $(OTTER)/otter.h $(HAMS)/hams_binding.cpp $(HAMS)/hams_binding.h \
	$(DUMMY)/dummy.cpp $(DUMMY)/dummy.h \
	$(CONTROL)/region_control.h $(BUILD_DIR)/region_control.o Makefile
	$(CXX) $(CPPFLAGS) $(HWLOC_CFLAGS) $(CXXFLAGS) -fopenmp -UNDEBUG tuner_runtime_ut.cpp \
		$(TUNER_DIR)/tuner.cpp $(DUMMY)/dummy.cpp $(HAMS)/hams_binding.cpp $(BUILD_DIR)/region_control.o \
		$(LDFLAGS) $(HWLOC_LIBS) -Wl,--wrap=omp_get_wtime -o $@

# 同一份桩 UT 分别检查启用和关闭插桩的行为。
$(CONTROL_TESTS): region_control_ut.c $(CONTROL)/region_control.c $(CONTROL)/region_control.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG \
		$(if $(findstring noinstrument,$@),-DREGION_INSTRUMENT=0) \
		region_control_ut.c $(CONTROL)/region_control.c $(LDFLAGS) \
		-Wl,--wrap=omp_get_wtime -o $@

test: hams-test j2025-test otter-test dummy-test tuner-test region-control-test

hams-test: $(BUILD_DIR)/hams_binding_ut
	$(RUN_OMP) $<

j2025-test: $(BUILD_DIR)/j2025_ut $(BUILD_DIR)/j2025_runtime_ut
	$(BUILD_DIR)/j2025_ut
	$(RUN_OMP) $(BUILD_DIR)/j2025_runtime_ut

otter-test: $(BUILD_DIR)/otter_ut
	$<

dummy-test: $(BUILD_DIR)/dummy_ut
	$<

tuner-test: $(BUILD_DIR)/tuner_runtime_ut
	$(RUN_OMP) $< default
	$(RUN_OMP) $< none
	$(RUN_OMP) $< dummy
	$(RUN_OMP) $< dummy-report
	$(RUN_OMP) OMP_NUM_THREADS=1 $< dummy
	$(RUN_OMP) OMP_NUM_THREADS=3 $< dummy
	$(RUN_OMP) OMP_NUM_THREADS=3 OMP_THREAD_LIMIT=2 $< dummy
	$(RUN_OMP) $< j2025
	$(RUN_OMP) $< otter
	$(RUN_OMP) $< otter-report

region-control-test: $(CONTROL_TESTS)
	$(RUN_OMP) $(BUILD_DIR)/region_control_ut
	$(RUN_OMP) $(BUILD_DIR)/region_control_ut_noinstrument
	python3 test_nowait.py --cc "$(CC)"

clean:
	$(RM) -r $(BUILD_DIR)
