# Shared CPU build. Applications own measurement/step markers; region hooks
# and tables are generated from their source, using the common tuner runtime.
.DEFAULT_GOAL := all
BUILD_DIR ?= build
INSTRUMENT ?= 1
CFLAGS ?= -O3 -fopenmp
CXXFLAGS ?= -O3 -fopenmp
LDFLAGS ?= -fopenmp
LDLIBS ?= -lm
FRAMEWORK ?= ../../framework
FRAMEWORK := $(abspath $(FRAMEWORK))
include $(FRAMEWORK)/benchmark/benchmark.mk
APP_LANGUAGE_FLAGS = $(if $(filter %.cpp,$($(firstword $(PROGRAMS))_SOURCE)),$(CXXFLAGS),$(CFLAGS))
INSTRUMENT_FLAGS ?= $(CPPFLAGS) $(filter -D% -U% -I% -std=%,$(APP_LANGUAGE_FLAGS))
BUILD_CONFIG := $(BUILD_DIR)/build.config
BUILD_MAKEFILES := $(MAKEFILE_LIST)
FRAMEWORK_OBJECTS := $(addprefix $(BUILD_DIR)/framework/,$(notdir $(FRAMEWORK_C_SOURCES:.c=.o) $(FRAMEWORK_CXX_SOURCES:.cpp=.o)))

.PHONY: all clean check FORCE
all: $(PROGRAMS)
check:
	@case "$(INSTRUMENT)" in 0|1) ;; *) echo "INSTRUMENT must be 0 or 1"; exit 2 ;; esac
FORCE:

$(BUILD_CONFIG): $(BUILD_MAKEFILES) FORCE | check
	@mkdir -p "$(@D)"
	@printf '%s\n' 'CC=$(CC)' 'CXX=$(CXX)' 'CPPFLAGS=$(CPPFLAGS) $(FRAMEWORK_CPPFLAGS)' \
		'CFLAGS=$(CFLAGS)' 'CXXFLAGS=$(CXXFLAGS)' 'LDFLAGS=$(LDFLAGS)' 'LDLIBS=$(LDLIBS) $(FRAMEWORK_LIBS)' \
		'CLANG=$(CLANG)' 'PYTHON=$(PYTHON)' 'INSTRUMENT=$(INSTRUMENT)' \
		'INSTRUMENT_FLAGS=$(INSTRUMENT_FLAGS)' 'PROGRAMS=$(PROGRAMS)' \
		'$(foreach program,$(PROGRAMS),$(program)_SOURCE=$($(program)_SOURCE))' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" && rm "$@.tmp" || mv "$@.tmp" "$@"

ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(wildcard $(addsuffix /generated/instrumentation.d,$(addprefix $(BUILD_DIR)/,$(PROGRAMS))))
endif

define FRAMEWORK_C_RULE
$(BUILD_DIR)/framework/$(notdir $(1:.c=.o)): $(1) $(FRAMEWORK_HEADERS) $(BUILD_CONFIG)
	@mkdir -p "$$(@D)"
	$(CC) $(CFLAGS) $(CPPFLAGS) $(FRAMEWORK_CPPFLAGS) -c "$$<" -o "$$@"
endef
$(foreach source,$(FRAMEWORK_C_SOURCES),$(eval $(call FRAMEWORK_C_RULE,$(source))))
define FRAMEWORK_CXX_RULE
$(BUILD_DIR)/framework/$(notdir $(1:.cpp=.o)): $(1) $(FRAMEWORK_HEADERS) $(BUILD_CONFIG)
	@mkdir -p "$$(@D)"
	$(CXX) $(CXXFLAGS) -std=c++17 $(CPPFLAGS) $(FRAMEWORK_CPPFLAGS) -c "$$<" -o "$$@"
endef
$(foreach source,$(FRAMEWORK_CXX_SOURCES),$(eval $(call FRAMEWORK_CXX_RULE,$(source))))

define PROGRAM_RULES
$(1)_GENERATED := $(addprefix $(BUILD_DIR)/$(1)/generated/,$($(1)_SOURCE) region_auto.h region_auto.c instrumentation.json instrumentation.d .region-auto-owner.json)
$(1)_OBJECTS := $(BUILD_DIR)/$(1)/program.o $(BUILD_DIR)/$(1)/region_auto.o

$$($(1)_GENERATED) &: $$($(1)_SOURCE) $(REGION_GENERATOR) $(FRAMEWORK_HEADERS) $(BUILD_CONFIG)
	$(PYTHON) "$(REGION_GENERATOR)" "$$($(1)_SOURCE)" --clang "$(CLANG)" \
		--output "$(BUILD_DIR)/$(1)/generated" --control '&benchmark_control' \
		--depfile "$(BUILD_DIR)/$(1)/generated/instrumentation.d" -- $(FRAMEWORK_CPPFLAGS) $(INSTRUMENT_FLAGS)

$(BUILD_DIR)/$(1)/program.o: $$($(1)_GENERATED) $(FRAMEWORK_HEADERS) $(BUILD_CONFIG)
	$(if $(filter %.cpp,$($(1)_SOURCE)),$(CXX) $(CXXFLAGS),$(CC) $(CFLAGS)) $(CPPFLAGS) $(FRAMEWORK_CPPFLAGS) \
		-I"$(BUILD_DIR)/$(1)/generated" -iquote "$(CURDIR)" -c "$(BUILD_DIR)/$(1)/generated/$$($(1)_SOURCE)" -o "$$@"

$(BUILD_DIR)/$(1)/region_auto.o: $$($(1)_GENERATED) $(FRAMEWORK_HEADERS) $(BUILD_CONFIG)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(FRAMEWORK_CPPFLAGS) -I"$(BUILD_DIR)/$(1)/generated" \
		-c "$(BUILD_DIR)/$(1)/generated/region_auto.c" -o "$$@"

$(1): $$($(1)_OBJECTS) $(FRAMEWORK_OBJECTS) $(BUILD_CONFIG)
	$(CXX) $$($(1)_OBJECTS) $(FRAMEWORK_OBJECTS) $(LDFLAGS) $(LDLIBS) $(FRAMEWORK_LIBS) -o "$$@"
endef
$(foreach program,$(PROGRAMS),$(eval $(call PROGRAM_RULES,$(program))))

clean:
	rm -rf "$(BUILD_DIR)"
	rm -f $(PROGRAMS)
