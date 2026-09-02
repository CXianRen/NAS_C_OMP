.DEFAULT_GOAL := all

CC := clang
NPB_DIR := NPB3.3-OMP-C
BUILD_DIR ?= .build
BIN_DIR ?= bin

BENCHMARKS ?= BT CG DC EP FT IS LU MG SP UA
CLASS ?=
CLASSES ?= $(if $(CLASS),$(CLASS),S W A B)
NPB_CFLAGS ?= -O3 -fopenmp -mcmodel=medium -mlarge-data-threshold=67108864
NPB_LDFLAGS ?= -fopenmp -mcmodel=medium -mlarge-data-threshold=67108864
HWLOC_CFLAGS ?=
HWLOC_LIBS ?= -lhwloc
OTTER_UT_CFLAGS ?= -O2 -g -fopenmp
OTTER_UT_LDFLAGS ?= -fopenmp

ALL_BENCHMARKS := BT CG DC EP FT IS LU MG SP UA
ALL_CLASSES := S W A B C D E
RAND_BENCHMARKS := BT CG DC EP FT MG
COMMON_SOURCES := $(addprefix $(NPB_DIR)/common/,c_print_results.c c_timers.c print_results.c wtime.c)
OTTER_BENCHMARKS := BT CG FT LU MG SP
OTTER_SOURCE := $(NPB_DIR)/common/otter_tuner.c
OTTER_HEADER := $(NPB_DIR)/common/otter_tuner.h
OTTER_UT_SOURCE := tests/otter_tuner_ut.c
OTTER_UT_BINARY := $(BIN_DIR)/otter_tuner_ut

$(foreach b,$(ALL_BENCHMARKS),\
  $(eval $(b)_SOURCES := $(wildcard $(NPB_DIR)/$(b)/src/*.c) $(COMMON_SOURCES) \
    $(if $(filter $(b),$(RAND_BENCHMARKS)),$(NPB_DIR)/common/randdp.c) \
    $(if $(filter $(b),$(OTTER_BENCHMARKS)),$(OTTER_SOURCE))))
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

.PHONY: all check clean otter-ut

all: check $(TARGETS)
$(TARGETS): | check

otter-ut: $(OTTER_UT_BINARY)

$(OTTER_UT_BINARY): $(OTTER_UT_SOURCE) $(OTTER_SOURCE) $(OTTER_HEADER) Makefile
	@mkdir -p "$(BIN_DIR)"
	$(CC) $(OTTER_UT_CFLAGS) $(HWLOC_CFLAGS) -I"$(NPB_DIR)/common" \
		$(OTTER_UT_SOURCE) $(OTTER_SOURCE) -o "$@" \
		$(OTTER_UT_LDFLAGS) $(HWLOC_LIBS) -lm

check:
	@test -n "$(strip $(BENCHMARKS))" || { echo "BENCHMARKS must not be empty"; exit 2; }
	@test -n "$(strip $(CLASSES))" || { echo "CLASSES must not be empty"; exit 2; }
	@test -z "$(UNKNOWN_BENCHMARKS)" || { echo "Unknown benchmark(s): $(UNKNOWN_BENCHMARKS)"; exit 2; }
	@test -z "$(UNKNOWN_CLASSES)" || { echo "Unknown class(es): $(UNKNOWN_CLASSES)"; exit 2; }
	@test -z "$(strip $(UNSUPPORTED_PAIRS))" || { echo "Unsupported target(s): $(UNSUPPORTED_PAIRS)"; exit 2; }

$(SETPARAMS): $(NPB_DIR)/sys/setparams.c
	@mkdir -p "$(@D)"
	$(CC) -O2 "$<" -o "$@"

$(MAKEDEF): Makefile
	@mkdir -p "$(@D)"
	@printf '%s\n' \
		'CC = $(CC)' 'CLINK = $(CC)' 'C_LIB = -lm' 'C_INC = (none)' \
		'CFLAGS = $(NPB_CFLAGS)' 'CLINKFLAGS = $(NPB_LDFLAGS)' \
		'RAND = randdp' > "$@"

define BUILD_BINARY
$(BIN_DIR)/$(1).$(2): $$($(1)_SOURCES) $$(if $$(filter $(1),$$(OTTER_BENCHMARKS)),$$(OTTER_HEADER)) $(SETPARAMS) $(MAKEDEF) Makefile
	@mkdir -p "$(BUILD_DIR)/$(1).$(2)" "$(BIN_DIR)"
	@cd "$(BUILD_DIR)/$(1).$(2)" && ../setparams "$(1)" "$(2)"
	$(CC) $(NPB_CFLAGS) $(if $(filter $(1),$(OTTER_BENCHMARKS)),$(HWLOC_CFLAGS)) \
		-I"$(BUILD_DIR)/$(1).$(2)" -I"$(NPB_DIR)/common" -I"$(NPB_DIR)/$(1)/src" \
		$$($(1)_SOURCES) -o "$$@" $(NPB_LDFLAGS) \
		$(if $(filter $(1),$(OTTER_BENCHMARKS)),$(HWLOC_LIBS)) -lm
endef

$(foreach b,$(ALL_BENCHMARKS),\
  $(foreach c,$($(b)_CLASSES),$(eval $(call BUILD_BINARY,$(b),$(c)))))

clean:
	rm -rf "$(BUILD_DIR)" "$(BIN_DIR)"
