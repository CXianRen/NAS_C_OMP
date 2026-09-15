.DEFAULT_GOAL := all

CC := clang
CXX = $(if $(findstring clang,$(CC)),$(subst clang,clang++,$(CC)),g++)
INSTRUMENT ?= 1
TUNER ?= none
J2025_ENABLE ?= $(if $(filter j2025,$(TUNER)),1,0)
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

# J2025 模式才编译 C++；SP 和内置计时始终按 C 编译。
CONTROL_SOURCES := $(if $(filter 1,$(J2025_ENABLE)), \
	$(FRAMEWORK)/hams/hams_binding.cpp $(FRAMEWORK)/j2025/j2025.cpp $(FRAMEWORK)/j2025/j2025_runtime.cpp)
CONTROL_OBJECTS := $(patsubst $(FRAMEWORK)/%.cpp,$(BUILD_DIR)/control/%.o,$(CONTROL_SOURCES))
CONTROL_CPPFLAGS := -DJ2025_ENABLE=$(J2025_ENABLE)
CONTROL_LIBS := $(if $(CONTROL_OBJECTS),-lstdc++)
CONTROL_HEADERS := $(wildcard $(FRAMEWORK)/hams/*.h $(FRAMEWORK)/j2025/*.h $(FRAMEWORK)/region_control/*.h)

SP_SOURCES := $(wildcard $(NPB_DIR)/SP/src/*.c)
SOURCES := $(SP_SOURCES) $(NPB_DIR)/common/print_results.c $(FRAMEWORK)/region_control/region_control.c
HEADERS := $(wildcard $(NPB_DIR)/SP/src/*.h $(NPB_DIR)/common/*.h) $(CONTROL_HEADERS)
ALL_CLASSES := S W A B C D E
UNKNOWN_CLASSES := $(filter-out $(ALL_CLASSES),$(CLASSES))
TARGETS := $(addprefix $(BIN_DIR)/SP.,$(filter $(ALL_CLASSES),$(CLASSES)))

SETPARAMS := $(BUILD_DIR)/setparams
MAKEDEF := $(BUILD_DIR)/config/make.def

.PHONY: all check clean FORCE

all: check $(TARGETS)

check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac
	@case "$(TUNER)" in none|j2025) ;; *) echo "TUNER must be none or j2025"; exit 2 ;; esac
	@case "$(J2025_ENABLE)" in 0|1) ;; *) echo "J2025_ENABLE must be 0 or 1"; exit 2 ;; esac
	@test "$(J2025_ENABLE)" = 0 -o "$(INSTRUMENT)" = 1 || { echo "J2025_ENABLE=1 requires INSTRUMENT=1"; exit 2; }
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
		'TUNER = $(TUNER)' 'J2025_ENABLE = $(J2025_ENABLE)' \
		'CXX = $(CXX)' 'CXXFLAGS = $(NPB_CXXFLAGS)' \
		'SP_SOURCES = $(SP_SOURCES)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

$(BUILD_DIR)/control/%.o: $(FRAMEWORK)/%.cpp $(CONTROL_HEADERS) $(MAKEDEF)
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(NPB_CXXFLAGS) $(CONTROL_CPPFLAGS) -c "$<" -o "$@"

# 七个 Class 共用静态模式规则，参数头和构建目录各自独立。
$(addprefix $(BIN_DIR)/SP.,$(ALL_CLASSES)): $(BIN_DIR)/SP.%: $(SOURCES) $(HEADERS) $(SETPARAMS) $(MAKEDEF) $(CONTROL_OBJECTS) Makefile | check
	@mkdir -p "$(BUILD_DIR)/SP.$*" "$(@D)"
	@rm -f "$(BUILD_DIR)/SP.$*/npbparams.h"
	@cd "$(BUILD_DIR)/SP.$*" && ../setparams SP "$*"
	$(CC) $(CPPFLAGS) $(NPB_CFLAGS) $(CONTROL_CPPFLAGS) -DREGION_INSTRUMENT=$(INSTRUMENT) \
		-I"$(BUILD_DIR)/SP.$*" -I"$(NPB_DIR)/common" -I"$(NPB_DIR)/SP/src" \
		$(SOURCES) $(CONTROL_OBJECTS) -o "$@" $(NPB_LDFLAGS) $(CONTROL_LIBS) -lm

clean:
	rm -rf "$(BUILD_DIR)" "$(BIN_DIR)"
