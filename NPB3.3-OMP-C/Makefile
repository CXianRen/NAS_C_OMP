.DEFAULT_GOAL := all

CC := clang
CXX = $(if $(findstring clang,$(CC)),$(subst clang,clang++,$(CC)),g++)
CLANG ?= $(if $(findstring clang,$(CC)),$(CC),clang-18)
PYTHON ?= python3
INSTRUMENT ?= 1
NPB_DIR := .
FRAMEWORK := ../framework
BUILD_DIR ?= .build
BIN_DIR ?= bin

BENCHMARKS ?= SP
CLASS ?=
CLASSES ?= $(if $(CLASS),$(CLASS),S W A B)
NPB_FLAGS := -fopenmp -mcmodel=medium -mlarge-data-threshold=67108864
NPB_CFLAGS ?= -O3 $(NPB_FLAGS)
NPB_LDFLAGS ?= $(NPB_FLAGS)
NPB_CXXFLAGS ?= $(NPB_CFLAGS) -std=c++17

# 所有 tuner 编译进同一个程序，运行时由环境变量 TUNER 选择。
# SP 和内置计时始终按 C 编译。
HWLOC_CFLAGS ?= $(shell pkg-config --cflags hwloc 2>/dev/null)
HWLOC_LIBS ?= $(shell pkg-config --libs hwloc 2>/dev/null || echo -lhwloc)
CONTROL_SOURCES := $(FRAMEWORK)/hams/hams_binding.cpp $(FRAMEWORK)/j2025/j2025.cpp \
	$(FRAMEWORK)/j2025_b/j2025_b.cpp $(FRAMEWORK)/otter/otter.cpp \
	$(FRAMEWORK)/dummy/dummy.cpp $(FRAMEWORK)/offline/offline.cpp $(FRAMEWORK)/tuner/tuner.cpp
CONTROL_OBJECTS := $(patsubst $(FRAMEWORK)/%.cpp,$(BUILD_DIR)/control/%.o,$(CONTROL_SOURCES))
CONTROL_CPPFLAGS := -DREGION_INSTRUMENT=$(INSTRUMENT) $(HWLOC_CFLAGS)
CONTROL_LIBS := -lstdc++ $(HWLOC_LIBS)
CONTROL_HEADERS := $(wildcard $(FRAMEWORK)/hams/*.h $(FRAMEWORK)/j2025/*.h \
	$(FRAMEWORK)/j2025_b/*.h $(FRAMEWORK)/otter/*.h $(FRAMEWORK)/dummy/*.h $(FRAMEWORK)/offline/*.h \
	$(FRAMEWORK)/tuner/*.h $(FRAMEWORK)/region_control/*.h)

SP_SOURCES := $(wildcard $(NPB_DIR)/SP/src/*.c)
SOURCES := $(SP_SOURCES) $(NPB_DIR)/common/print_results.c $(FRAMEWORK)/region_control/region_control.c
HEADERS := $(wildcard $(NPB_DIR)/SP/src/*.h $(NPB_DIR)/common/*.h) $(CONTROL_HEADERS)
ALL_CLASSES := S W A B C D E
UNKNOWN_CLASSES := $(filter-out $(ALL_CLASSES),$(CLASSES))
TARGETS := $(addprefix $(BIN_DIR)/SP.,$(filter $(ALL_CLASSES),$(CLASSES)))

SETPARAMS := $(BUILD_DIR)/setparams
MAKEDEF := $(BUILD_DIR)/config/make.def
REGION_GENERATOR := $(FRAMEWORK)/region_control/instrument_regions.py
CLASS_DIRS := $(addprefix $(BUILD_DIR)/SP.,$(ALL_CLASSES))
EXPLICIT_TARGETS := $(filter $(addprefix $(BIN_DIR)/SP.,$(ALL_CLASSES)),$(MAKECMDGOALS))
DEPENDENCY_CLASSES := $(if $(EXPLICIT_TARGETS),$(patsubst $(BIN_DIR)/SP.%,%,$(EXPLICIT_TARGETS)),$(filter $(ALL_CLASSES),$(CLASSES)))

# Included depfiles are generated targets: load only the requested classes so
# changing flags does not regenerate unrelated builds, or build during clean.
ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(wildcard $(addsuffix /generated/instrumentation.d,$(addprefix $(BUILD_DIR)/SP.,$(DEPENDENCY_CLASSES))))
endif

.PHONY: all check clean FORCE

all: check $(TARGETS)

check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac
	@test "$(strip $(BENCHMARKS))" = SP || { echo "Only SP is available"; exit 2; }
	@test -n "$(strip $(CLASSES))" || { echo "CLASSES must not be empty"; exit 2; }
	@test -z "$(UNKNOWN_CLASSES)" || { echo "Unknown class(es): $(UNKNOWN_CLASSES)"; exit 2; }

$(SETPARAMS): $(NPB_DIR)/sys/setparams.c
	@mkdir -p "$(@D)"
	$(CC) -O2 "$<" -o "$@"

FORCE:

# 同时供 setparams 读取编译信息；仅配置或源文件列表改变时更新时间戳。
$(MAKEDEF): Makefile FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' \
		'CC = $(CC)' 'CLINK = $(CC)' 'C_LIB = -lm' 'C_INC = (none)' \
		'CFLAGS = $(NPB_CFLAGS)' 'CLINKFLAGS = $(NPB_LDFLAGS)' \
		'RAND = (none)' 'CPPFLAGS = $(CPPFLAGS)' \
		'INSTRUMENT = $(INSTRUMENT)' \
		'CXX = $(CXX)' 'CXXFLAGS = $(NPB_CXXFLAGS)' \
		'CLANG = $(CLANG)' 'PYTHON = $(PYTHON)' \
		'HWLOC_CFLAGS = $(HWLOC_CFLAGS)' 'HWLOC_LIBS = $(HWLOC_LIBS)' \
		'SP_SOURCES = $(SP_SOURCES)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

$(BUILD_DIR)/control/%.o: $(FRAMEWORK)/%.cpp $(CONTROL_HEADERS) $(MAKEDEF)
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(NPB_CXXFLAGS) $(CONTROL_CPPFLAGS) -c "$<" -o "$@"

# 七个 Class 的参数头和生成副本各自独立；只扫描 SP 应用源码。
$(addsuffix /npbparams.h,$(CLASS_DIRS)): $(BUILD_DIR)/SP.%/npbparams.h: $(SETPARAMS) $(MAKEDEF) Makefile
	@mkdir -p "$(@D)"
	@rm -f "$@"
	@cd "$(@D)" && ../setparams SP "$*"

# A grouped rule restores any removed generated output and runs once per class.
define SP_CLASS_RULES
SP_GENERATED_$1 := $(addprefix $(BUILD_DIR)/SP.$1/generated/,$(notdir $(SP_SOURCES))) \
	$(addprefix $(BUILD_DIR)/SP.$1/generated/,region_auto.h region_auto.c instrumentation.json instrumentation.d .region-auto-owner.json)

$$(SP_GENERATED_$1) &: $(SP_SOURCES) $(HEADERS) $(BUILD_DIR)/SP.$1/npbparams.h $(MAKEDEF) $(REGION_GENERATOR) Makefile
	$(PYTHON) $(REGION_GENERATOR) $(SP_SOURCES) --clang "$(CLANG)" \
		--output "$(BUILD_DIR)/SP.$1/generated" --control '&sp_control' \
		--depfile "$(BUILD_DIR)/SP.$1/generated/instrumentation.d" -- \
		$(CPPFLAGS) $(NPB_CFLAGS) $(CONTROL_CPPFLAGS) \
		-I"$(BUILD_DIR)/SP.$1" -I"$(NPB_DIR)/common" -I"$(NPB_DIR)/SP/src" \
		-I"$(FRAMEWORK)/region_control"

$(BIN_DIR)/SP.$1: $(SOURCES) $(HEADERS) $(BUILD_DIR)/SP.$1/npbparams.h $$(SP_GENERATED_$1) $(MAKEDEF) $(CONTROL_OBJECTS) Makefile | check
	@mkdir -p "$$(@D)"
	$(CC) $(CPPFLAGS) $(NPB_CFLAGS) $(CONTROL_CPPFLAGS) \
		-I"$(BUILD_DIR)/SP.$1/generated" -I"$(BUILD_DIR)/SP.$1" \
		-I"$(NPB_DIR)/common" -iquote "$(NPB_DIR)/SP/src" -I"$(FRAMEWORK)/region_control" \
		$(addprefix $(BUILD_DIR)/SP.$1/generated/,$(notdir $(SP_SOURCES))) \
		$(BUILD_DIR)/SP.$1/generated/region_auto.c $(NPB_DIR)/common/print_results.c \
		$(FRAMEWORK)/region_control/region_control.c $(CONTROL_OBJECTS) \
		-o "$$@" $(NPB_LDFLAGS) $(CONTROL_LIBS) -lm
endef
$(foreach class,$(ALL_CLASSES),$(eval $(call SP_CLASS_RULES,$(class))))

clean:
	rm -rf "$(BUILD_DIR)" "$(BIN_DIR)"
