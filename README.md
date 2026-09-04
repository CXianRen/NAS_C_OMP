# NAS Parallel Benchmarks

This repository contains the OpenMP C implementation of the [NAS Parallel
Benchmarks][1]. It is based on the [compor/SNU_NPB repository][2], with the
serial implementation removed.

The benchmark sources remain organized under `NPB3.3-OMP-C/<BENCHMARK>/src`.
A standalone GNU Makefile is provided inside `NPB3.3-OMP-C`, so an external
build harness is no longer required.

## Build

Build every benchmark for Classes S, W, A, and B:

```sh
make -C NPB3.3-OMP-C -j
```

Select benchmarks and classes explicitly:

```sh
make -C NPB3.3-OMP-C -j BENCHMARKS="CG MG" CLASSES="S A C D"
make -C NPB3.3-OMP-C BENCHMARKS=CG CLASS=D
```

Binaries are written to `NPB3.3-OMP-C/bin/` as `<BENCHMARK>.<CLASS>`, for
example `NPB3.3-OMP-C/bin/CG.S`. Unsupported benchmark/class combinations are
rejected. Remove all generated files with `./clean.sh`.

[1]: www.nas.nasa.gov/publications/npb.html

[2]: https://github.com/compor/SNU_NPB/tree/master
