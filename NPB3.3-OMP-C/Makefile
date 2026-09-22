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
include $(FRAMEWORK)/benchmark/benchmark.mk

ALL_BENCHMARKS := BT CG DC EP FT IS LU MG SP UA
ALL_CLASSES := S W A B C D E
BENCHMARKS ?= $(ALL_BENCHMARKS)
CLASS ?=
CLASSES ?= $(if $(CLASS),$(CLASS),S W A B)
NPB_FLAGS := -fopenmp -mcmodel=medium -mlarge-data-threshold=67108864
NPB_CFLAGS ?= -O3 $(NPB_FLAGS)
NPB_LDFLAGS ?= $(NPB_FLAGS)
NPB_CXXFLAGS ?= $(NPB_CFLAGS) -std=c++17

# C application/generated sources use CC; shared tuners use CXX exactly once.
CONTROL_OBJECTS := $(patsubst $(FRAMEWORK)/%.cpp,$(BUILD_DIR)/control/%.o,$(FRAMEWORK_CXX_SOURCES))
COMMON_SOURCES := $(addprefix $(NPB_DIR)/common/,c_print_results.c print_results.c) $(FRAMEWORK_C_SOURCES)
COMMON_HEADERS := $(wildcard $(NPB_DIR)/common/*.h) $(FRAMEWORK_HEADERS)
RAND_BENCHMARKS := BT CG DC EP FT MG
$(foreach b,$(ALL_BENCHMARKS),\
  $(eval $(b)_SOURCES := $(wildcard $(NPB_DIR)/$(b)/src/*.c)) \
  $(eval $(b)_HEADERS := $(wildcard $(NPB_DIR)/$(b)/src/*.h $(NPB_DIR)/$(b)/src/*.incl)) \
  $(eval $(b)_COMMON := $(COMMON_SOURCES) $(if $(filter $(b),$(RAND_BENCHMARKS)),$(NPB_DIR)/common/randdp.c)))
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
UNSUPPORTED_PAIRS := $(foreach b,$(KNOWN_BENCHMARKS),$(foreach c,$(KNOWN_CLASSES),$(if $(filter $(c),$($(b)_CLASSES)),,$(b).$(c))))
TARGETS := $(foreach b,$(KNOWN_BENCHMARKS),$(foreach c,$(filter $(KNOWN_CLASSES),$($(b)_CLASSES)),$(BIN_DIR)/$(b).$(c)))
ALL_TARGETS := $(foreach b,$(ALL_BENCHMARKS),$(foreach c,$($(b)_CLASSES),$(BIN_DIR)/$(b).$(c)))
SETPARAMS := $(BUILD_DIR)/setparams
MAKEDEF := $(BUILD_DIR)/config/make.def
EXPLICIT_TARGETS := $(filter $(ALL_TARGETS),$(MAKECMDGOALS))
DEPENDENCY_PAIRS := $(notdir $(if $(EXPLICIT_TARGETS),$(EXPLICIT_TARGETS),$(TARGETS)))

# Only requested targets contribute included depfiles; clean never regenerates.
ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(wildcard $(addsuffix /generated/instrumentation.d,$(addprefix $(BUILD_DIR)/,$(DEPENDENCY_PAIRS))))
endif

.PHONY: all check clean FORCE
all: check $(TARGETS)

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
$(MAKEDEF): Makefile $(FRAMEWORK)/benchmark/benchmark.mk FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' \
		'CC = $(CC)' 'CLINK = $(CC)' 'C_LIB = -lm' 'C_INC = (none)' \
		'CFLAGS = $(NPB_CFLAGS)' 'CLINKFLAGS = $(NPB_LDFLAGS)' \
		'RAND = randdp' 'CPPFLAGS = $(CPPFLAGS)' 'INSTRUMENT = $(INSTRUMENT)' \
		'CXX = $(CXX)' 'CXXFLAGS = $(NPB_CXXFLAGS)' 'CLANG = $(CLANG)' 'PYTHON = $(PYTHON)' \
		'FRAMEWORK_CPPFLAGS = $(FRAMEWORK_CPPFLAGS)' 'FRAMEWORK_LIBS = $(FRAMEWORK_LIBS)' \
		'FRAMEWORK_CXX_SOURCES = $(FRAMEWORK_CXX_SOURCES)' \
		$(foreach b,$(ALL_BENCHMARKS),'$(b)_SOURCES = $($(b)_SOURCES)') > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

$(BUILD_DIR)/control/%.o: $(FRAMEWORK)/%.cpp $(FRAMEWORK_HEADERS) $(MAKEDEF)
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(NPB_CXXFLAGS) $(FRAMEWORK_CPPFLAGS) -c "$<" -o "$@"

# Each benchmark/class owns its parameters, AST manifest and generated sources.
define BUILD_PAIR
GENERATED_$1_$2 := $(addprefix $(BUILD_DIR)/$1.$2/generated/,$(notdir $($1_SOURCES))) \
	$(addprefix $(BUILD_DIR)/$1.$2/generated/,region_auto.h region_auto.c instrumentation.json instrumentation.d .region-auto-owner.json)

$(BUILD_DIR)/$1.$2/npbparams.h: $(SETPARAMS) $(MAKEDEF) Makefile
	@mkdir -p "$$(@D)"
	@rm -f "$$@"
	@cd "$$(@D)" && ../setparams "$1" "$2"

$$(GENERATED_$1_$2) &: $($1_SOURCES) $($1_HEADERS) $(COMMON_HEADERS) $(BUILD_DIR)/$1.$2/npbparams.h $(MAKEDEF) $(REGION_GENERATOR) Makefile
	$(PYTHON) $(REGION_GENERATOR) $($1_SOURCES) --clang "$(CLANG)" \
		--output "$(BUILD_DIR)/$1.$2/generated" --control '$(if $(filter SP,$1),&sp_control,&benchmark_control)' \
		--depfile "$(BUILD_DIR)/$1.$2/generated/instrumentation.d" -- \
		$(CPPFLAGS) $(NPB_CFLAGS) $(FRAMEWORK_CPPFLAGS) \
		-I"$(BUILD_DIR)/$1.$2" -I"$(NPB_DIR)/common" -I"$(NPB_DIR)/$1/src"

$(BIN_DIR)/$1.$2: $($1_SOURCES) $($1_COMMON) $($1_HEADERS) $(COMMON_HEADERS) $(BUILD_DIR)/$1.$2/npbparams.h $$(GENERATED_$1_$2) $(MAKEDEF) $(CONTROL_OBJECTS) Makefile | check
	@mkdir -p "$$(@D)"
	$(CC) $(CPPFLAGS) $(NPB_CFLAGS) $(FRAMEWORK_CPPFLAGS) \
		-I"$(BUILD_DIR)/$1.$2/generated" -I"$(BUILD_DIR)/$1.$2" \
		-I"$(NPB_DIR)/common" -iquote "$(NPB_DIR)/$1/src" \
		$(addprefix $(BUILD_DIR)/$1.$2/generated/,$(notdir $($1_SOURCES))) \
		$(BUILD_DIR)/$1.$2/generated/region_auto.c $($1_COMMON) $(CONTROL_OBJECTS) \
		-o "$$@" $(NPB_LDFLAGS) $(FRAMEWORK_LIBS) -lm
endef
$(foreach b,$(ALL_BENCHMARKS),$(foreach c,$($(b)_CLASSES),$(eval $(call BUILD_PAIR,$(b),$(c)))))

clean:
	rm -rf "$(BUILD_DIR)" "$(BIN_DIR)"
