PROGRAMS := euler3d_cpu euler3d_cpu_double pre_euler3d_cpu pre_euler3d_cpu_double
$(foreach program,$(PROGRAMS),$(eval $(program)_SOURCE := $(program).cpp))

# Legacy make variable names the padding width, independently of the runtime ICV.
OMP_NUM_THREADS ?= 8
override CPPFLAGS += -Dblock_length=$(OMP_NUM_THREADS)
include ../mk/benchmark.mk
