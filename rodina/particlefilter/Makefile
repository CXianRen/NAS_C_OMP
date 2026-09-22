PROGRAMS := particle_filter
particle_filter_SOURCE := ex_particle_OPENMP_seq.c
CFLAGS ?= -O3 -ffast-math -fopenmp
include ../mk/benchmark.mk

.PHONY: openmp
openmp: particle_filter
