.DEFAULT_GOAL := all

CC := clang
CLANG ?= clang
PYTHON ?= python3
INSTRUMENT ?= 1
INSTRUMENT_FLAGS ?= $(CPPFLAGS) $(filter -D% -U% -I% -std=%,$(NPB_CFLAGS))
NPB_DIR := .
BUILD_DIR ?= .build
BIN_DIR ?= bin

BENCHMARKS ?= BT CG DC EP FT IS LU MG SP UA
CLASS ?=
CLASSES ?= $(if $(CLASS),$(CLASS),S W A B)
NPB_CFLAGS ?= -O3 -fopenmp -mcmodel=medium -mlarge-data-threshold=67108864
NPB_LDFLAGS ?= -fopenmp -mcmodel=medium -mlarge-data-threshold=67108864

ALL_BENCHMARKS := BT CG DC EP FT IS LU MG SP UA
ALL_CLASSES := S W A B C D E
RAND_BENCHMARKS := BT CG DC EP FT MG
COMMON_SOURCES := $(addprefix $(NPB_DIR)/common/,c_print_results.c region_timers.c print_results.c)
COMMON_HEADERS := $(wildcard $(NPB_DIR)/common/*.h)

$(foreach b,$(ALL_BENCHMARKS),\
  $(eval $(b)_SOURCES := $(wildcard $(NPB_DIR)/$(b)/src/*.c)) \
  $(eval $(b)_COMMON := $(COMMON_SOURCES) \
    $(if $(filter $(b),$(RAND_BENCHMARKS)),$(NPB_DIR)/common/randdp.c)))
UA_SOURCES := $(filter-out $(NPB_DIR)/UA/src/transfer_au.c,$(UA_SOURCES))

BT_CLASSES := S W A B C D E
CG_CLASSES := S W A B C D
DC_CLASSES := S W A B
EP_CLASSES := S W A B C D E
FT_CLASSES := S W A B C D E
IS_CLASSES := S W A B C D
LU_CLASSES := S W A B C D E
MG_CLASSES := S W A B C D
SP_CLASSES := S W A B C D E
UA_CLASSES := S W A B C D

KNOWN_BENCHMARKS := $(filter $(ALL_BENCHMARKS),$(BENCHMARKS))
KNOWN_CLASSES := $(filter $(ALL_CLASSES),$(CLASSES))
UNKNOWN_BENCHMARKS := $(filter-out $(ALL_BENCHMARKS),$(BENCHMARKS))
UNKNOWN_CLASSES := $(filter-out $(ALL_CLASSES),$(CLASSES))
UNSUPPORTED_PAIRS := $(foreach b,$(KNOWN_BENCHMARKS),\
  $(foreach c,$(KNOWN_CLASSES),$(if $(filter $(c),$($(b)_CLASSES)),,$(b).$(c))))
TARGETS := $(foreach b,$(KNOWN_BENCHMARKS),\
  $(foreach c,$(filter $(KNOWN_CLASSES),$($(b)_CLASSES)),$(BIN_DIR)/$(b).$(c)))

SETPARAMS := $(BUILD_DIR)/setparams
MAKEDEF := $(BUILD_DIR)/config/make.def

.PHONY: all check clean time-test instrument-test FORCE

all: check $(TARGETS)
$(TARGETS): | check

check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac
	@test -n "$(strip $(BENCHMARKS))" || { echo "BENCHMARKS must not be empty"; exit 2; }
	@test -n "$(strip $(CLASSES))" || { echo "CLASSES must not be empty"; exit 2; }
	@test -z "$(UNKNOWN_BENCHMARKS)" || { echo "Unknown benchmark(s): $(UNKNOWN_BENCHMARKS)"; exit 2; }
	@test -z "$(UNKNOWN_CLASSES)" || { echo "Unknown class(es): $(UNKNOWN_CLASSES)"; exit 2; }
	@test -z "$(strip $(UNSUPPORTED_PAIRS))" || { echo "Unsupported target(s): $(UNSUPPORTED_PAIRS)"; exit 2; }

$(SETPARAMS): $(NPB_DIR)/sys/setparams.c
	@mkdir -p "$(@D)"
	$(CC) -O2 "$<" -o "$@"

FORCE:

$(MAKEDEF): Makefile FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' \
		'CC = $(CC)' 'CLINK = $(CC)' 'C_LIB = -lm' 'C_INC = (none)' \
		'CFLAGS = $(NPB_CFLAGS)' 'CLINKFLAGS = $(NPB_LDFLAGS)' \
		'RAND = randdp' 'CPPFLAGS = $(CPPFLAGS)' \
		'CLANG = $(CLANG)' 'INSTRUMENT_FLAGS = $(INSTRUMENT_FLAGS)' \
		'PYTHON = $(PYTHON)' 'INSTRUMENT = $(INSTRUMENT)' \
		$(foreach b,$(ALL_BENCHMARKS),'$(b)_SOURCES = $($(b)_SOURCES)') > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

define BUILD_BINARY
$(BIN_DIR)/$(1).$(2): $$($(1)_SOURCES) $$($(1)_COMMON) $(COMMON_HEADERS) $(wildcard $(NPB_DIR)/$(1)/src/*.h $(NPB_DIR)/$(1)/src/*.incl) $(if $(filter 1,$(INSTRUMENT)),$(NPB_DIR)/tools/instrument_regions.py) $(SETPARAMS) $(MAKEDEF) Makefile
	@mkdir -p "$(BUILD_DIR)/$(1).$(2)" "$(BIN_DIR)"
	@cd "$(BUILD_DIR)/$(1).$(2)" && ../setparams "$(1)" "$(2)"
ifeq ($(INSTRUMENT),1)
	$(PYTHON) $(NPB_DIR)/tools/instrument_regions.py $$($(1)_SOURCES) \
		--clang "$(CLANG)" --output "$(BUILD_DIR)/$(1).$(2)/instrumented" -- \
		-I"$(BUILD_DIR)/$(1).$(2)" -I"$(NPB_DIR)/$(1)/src" $(INSTRUMENT_FLAGS)
endif
	$(CC) $(CPPFLAGS) $(NPB_CFLAGS) -DNPB_REGION_TIMING=$(INSTRUMENT) \
		-I"$(BUILD_DIR)/$(1).$(2)" -I"$(NPB_DIR)/common" -I"$(NPB_DIR)/$(1)/src" \
		$(if $(filter 1,$(INSTRUMENT)),"$(BUILD_DIR)/$(1).$(2)/instrumented/"*.c,$$($(1)_SOURCES)) $$($(1)_COMMON) -o "$$@" $(NPB_LDFLAGS) -lm
endef

$(foreach b,$(ALL_BENCHMARKS),\
  $(foreach c,$($(b)_CLASSES),$(eval $(call BUILD_BINARY,$(b),$(c)))))

clean:
	rm -rf "$(BUILD_DIR)" "$(BIN_DIR)"

time-test:
	python3 $(NPB_DIR)/tests/test_region_timers.py --cc "$(CC)"
	python3 $(NPB_DIR)/tests/check_region_reports.py

instrument-test:
	python3 $(NPB_DIR)/tests/test_instrument_regions.py --cc "$(CC)" --clang "$(CLANG)"
	python3 $(NPB_DIR)/tests/test_nowait_barriers.py --cc "$(CC)" --clang "$(CLANG)"
