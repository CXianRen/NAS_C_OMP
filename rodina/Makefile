.PHONY: all clean instrument-test
SUBDIRS := hotspot streamcluster cfd particlefilter

all clean:
	@set -e; for benchmark in $(SUBDIRS); do $(MAKE) -C "$$benchmark" $@; done

instrument-test:
	python3 tests/test_instrumented_build.py
