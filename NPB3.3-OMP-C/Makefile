.DEFAULT_GOAL := all

CC := clang
CXX = $(if $(findstring clang,$(CC)),$(subst clang,clang++,$(CC)),g++)
CLANG ?= clang
PYTHON ?= python3
INSTRUMENT ?= 1
TUNER ?= none
INSTRUMENT_FLAGS ?= $(CPPFLAGS) $(filter -D% -U% -I% -std=%,$(NPB_CFLAGS))
NPB_DIR := .
BUILD_DIR ?= .build
BIN_DIR ?= bin

BENCHMARKS ?= SP
CLASS ?=
CLASSES ?= $(if $(CLASS),$(CLASS),S W A B)
NPB_CFLAGS ?= -O3 -fopenmp -mcmodel=medium -mlarge-data-threshold=67108864
NPB_LDFLAGS ?= -fopenmp -mcmodel=medium -mlarge-data-threshold=67108864
NPB_CXXFLAGS ?= $(NPB_CFLAGS) -std=c++17

CONTROL_SOURCES := ../framework/hams/hams_binding.cpp ../framework/region_control/region_control.cpp \
	../framework/j2025/j2025.cpp common/region_control_adapter.cpp
CONTROL_HEADERS := $(wildcard ../framework/hams/*.h ../framework/region_control/*.h ../framework/j2025/*.h) \
	common/region_control_adapter.h common/region_timers.h ../framework/timer/region_timer.h
CONTROL_OBJECTS := $(if $(filter j2025,$(TUNER)),\
	$(addprefix $(BUILD_DIR)/control/,$(notdir $(CONTROL_SOURCES:.cpp=.o))))
CONTROL_CPPFLAGS := -DNPB_REGION_CONTROL=$(if $(filter j2025,$(TUNER)),1,0)
CONTROL_LIBS := $(if $(filter j2025,$(TUNER)),-lstdc++)

ALL_CLASSES := S W A B C D E
SP_SOURCES := $(wildcard $(NPB_DIR)/SP/src/*.c)
COMMON_SOURCES := $(addprefix $(NPB_DIR)/common/,region_timers.c print_results.c) \
	../framework/timer/region_timer.c
COMMON_HEADERS := $(wildcard $(NPB_DIR)/common/*.h) ../framework/timer/region_timer.h

KNOWN_CLASSES := $(filter $(ALL_CLASSES),$(CLASSES))
UNKNOWN_CLASSES := $(filter-out $(ALL_CLASSES),$(CLASSES))
TARGETS := $(addprefix $(BIN_DIR)/SP.,$(KNOWN_CLASSES))

SETPARAMS := $(BUILD_DIR)/setparams
MAKEDEF := $(BUILD_DIR)/config/make.def

.PHONY: all check clean time-test instrument-test control-test FORCE

all: check $(TARGETS)
$(TARGETS): | check

check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac
	@case "$(TUNER)" in none|j2025) ;; *) echo "TUNER must be none or j2025"; exit 2 ;; esac
	@test "$(TUNER)" = none -o "$(INSTRUMENT)" = 1 || { echo "TUNER=j2025 requires INSTRUMENT=1"; exit 2; }
	@test "$(strip $(BENCHMARKS))" = SP || { echo "Only SP is available"; exit 2; }
	@test -n "$(strip $(CLASSES))" || { echo "CLASSES must not be empty"; exit 2; }
	@test -z "$(UNKNOWN_CLASSES)" || { echo "Unknown class(es): $(UNKNOWN_CLASSES)"; exit 2; }

$(SETPARAMS): $(NPB_DIR)/sys/setparams.c
	@mkdir -p "$(@D)"
	$(CC) -O2 "$<" -o "$@"

FORCE:

$(MAKEDEF): Makefile FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' \
		'CC = $(CC)' 'CLINK = $(CC)' 'C_LIB = -lm' 'C_INC = (none)' \
		'CFLAGS = $(NPB_CFLAGS)' 'CLINKFLAGS = $(NPB_LDFLAGS)' \
		'RAND = (none)' 'CPPFLAGS = $(CPPFLAGS)' \
		'CLANG = $(CLANG)' 'INSTRUMENT_FLAGS = $(INSTRUMENT_FLAGS)' \
		'PYTHON = $(PYTHON)' 'INSTRUMENT = $(INSTRUMENT)' \
		'TUNER = $(TUNER)' 'CXX = $(CXX)' 'CXXFLAGS = $(NPB_CXXFLAGS)' \
		'SP_SOURCES = $(SP_SOURCES)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

# Compile the shared C++ controller once; benchmark source files remain C.
define BUILD_CONTROL_OBJECT
$(BUILD_DIR)/control/$(notdir $(1:.cpp=.o)): $(1) $(CONTROL_HEADERS) $(MAKEDEF)
	@mkdir -p "$$(@D)"
	$(CXX) $(CPPFLAGS) $(NPB_CXXFLAGS) $(CONTROL_CPPFLAGS) -Icommon -c "$$<" -o "$$@"
endef
$(foreach source,$(CONTROL_SOURCES),$(eval $(call BUILD_CONTROL_OBJECT,$(source))))

define BUILD_BINARY
$(BIN_DIR)/SP.$(1): $(SP_SOURCES) $(COMMON_SOURCES) $(COMMON_HEADERS) $(wildcard $(NPB_DIR)/SP/src/*.h) $(if $(filter 1,$(INSTRUMENT)),$(NPB_DIR)/tools/instrument_regions.py ../framework/region_control/instrument_regions.py) $(SETPARAMS) $(MAKEDEF) $(CONTROL_OBJECTS) Makefile
	@mkdir -p "$(BUILD_DIR)/SP.$(1)" "$(BIN_DIR)"
	@cd "$(BUILD_DIR)/SP.$(1)" && ../setparams SP "$(1)"
ifeq ($(INSTRUMENT),1)
	$(PYTHON) $(NPB_DIR)/tools/instrument_regions.py $(SP_SOURCES) \
		--clang "$(CLANG)" --output "$(BUILD_DIR)/SP.$(1)/instrumented" -- \
		-I"$(BUILD_DIR)/SP.$(1)" -I"$(NPB_DIR)/SP/src" $(INSTRUMENT_FLAGS)
endif
	$(CC) $(CPPFLAGS) $(NPB_CFLAGS) $(CONTROL_CPPFLAGS) -DNPB_REGION_TIMING=$(INSTRUMENT) \
		-I"$(BUILD_DIR)/SP.$(1)" -I"$(NPB_DIR)/common" -I"$(NPB_DIR)/SP/src" \
		$(if $(filter 1,$(INSTRUMENT)),"$(BUILD_DIR)/SP.$(1)/instrumented/"*.c,$(SP_SOURCES)) $(COMMON_SOURCES) $(CONTROL_OBJECTS) -o "$$@" $(NPB_LDFLAGS) $(CONTROL_LIBS) -lm
endef

$(foreach c,$(ALL_CLASSES),$(eval $(call BUILD_BINARY,$(c))))

clean:
	rm -rf "$(BUILD_DIR)" "$(BIN_DIR)"

time-test:
	python3 $(NPB_DIR)/tests/test_region_timers.py --cc "$(CC)"
	python3 $(NPB_DIR)/tests/check_region_reports.py

instrument-test:
	python3 $(NPB_DIR)/tests/test_instrument_regions.py --cc "$(CC)" --clang "$(CLANG)"
	python3 $(NPB_DIR)/tests/test_nowait_barriers.py --cc "$(CC)" --clang "$(CLANG)"

control-test:
	$(MAKE) -C ../framework/region_control test CXX="$(CXX)"
	$(MAKE) -C ../framework/j2025 test CXX="$(CXX)"
	python3 $(NPB_DIR)/tests/test_region_control.py --cc "$(CC)"
