CC = clang
CFLAGS ?= -O2 -Wall -Wextra
OPENMP_FLAGS ?= -fopenmp

.PHONY: all demo clean
all: binding_demo

binding_demo: binding_demo.c Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OPENMP_FLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

demo: binding_demo
	bash ./run.sh

clean:
	$(RM) binding_demo
