# cppkh

`cppkh` is a standalone C++14 port of the integer JavaKh Khovanov homology
computation path.

## Quick Start

Python package:

```sh
pip install cppkh-interface
```

```python
import cppkh_interface

pd_code = [[1, 5, 2, 4], [3, 1, 4, 6], [5, 3, 6, 2]]

print(cppkh_interface.solve_khovanov(pd_code))
print(cppkh_interface.solve_many_khovanov([pd_code, pd_code]))
```

The Python package compiles and caches the local `cppkh` executable on first
use. Set `CXX` before running Python when you want to choose a specific C++
compiler.

Build the fastest executable that the current machine can support:

```sh
python build.py
```

Run one PD code:

```bat
dist\windows\cppkh.exe --pd-code "PD[X[1,5,2,4],X[3,1,4,6],X[5,3,6,2]]"
```

On Linux the default output is `dist/linux/cppkh`; on macOS it is
`dist/macos/cppkh`.

Run a file or directory:

```bat
dist\windows\cppkh.exe --pd-file path\to\codes.txt
dist\windows\cppkh.exe --pd-dir path\to\pdcode_directory
```

R1-move removal and then nugatory-crossing removal are enabled by default.

## Performance Snapshot

On the full 8397-case benchmark, `cppkh` and the patched bundled JavaKh
matched every result. `cppkh` finished in `64.185s`, `cppkh-interface` batch
API finished in `65.406s` after its executable was already cached, and the
patched bundled JavaKh native multiline runner finished in `298.453s`. The
PyPI `javakh-interface` package was checked on a deterministic random
100-case sample and averaged `0.586s` per PD code.

Peak RSS on the same prepared full input was `26.05 MiB` for `cppkh`,
`60.23 MiB` for `cppkh-interface` as a Python batch API call, and
`491.55 MiB` for patched JavaKh. The previous PyPI `javakh-interface`
50-case memory sample peaked at `161.19 MiB`.

![cppkh benchmark runtime and memory chart](docs/assets/benchmark_runtime_memory.png)

## Shared Library

Build a shared library instead of an executable:

```sh
python build.py --shared --name cppkh
```

This produces `cppkh.dll`, `libcppkh.so`, or `libcppkh.dylib`, depending on the
platform. Any non-system runtime libraries found by `build.py` are
copied beside it.

## Documentation

- [Build and packaging options](docs/BUILD_AND_PACKAGING.md)
- [Command-line options](docs/CLI_OPTIONS.md)
- [Algorithm notes](docs/ALGORITHM.md)
- [Python ctypes interface](docs/PYTHON_CTYPES.md)
- [cppkh-interface Python package](docs/PYTHON_PACKAGE.md)
- [Testing against JavaKh](docs/TEST.md)
- [Bundled JavaKh reference](docs/JAVAKH_REFERENCE.md)
- [Benchmark results](docs/BENCHMARKS.md)

## References

- Knot Atlas: [Planar Diagrams](https://katlas.org/wiki/Planar_Diagrams)
- Knot Atlas: [Khovanov Homology](https://katlas.org/wiki/Khovanov_Homology)

## Original JavaKh

`cppkh` follows the integer JavaKh computation path. The original JavaKh-v2
project is available at [geometer/JavaKh-v2](https://github.com/geometer/JavaKh-v2).

## Citation

If you use `cppkh` in academic work, please cite this repository:

```bibtex
@software{cppkh_2026,
  author  = {{GGN\_2015}},
  title   = {{cppkh}: A C++ implementation of the JavaKh Khovanov homology computation path},
  year    = {2026},
  url     = {https://github.com/GGN-2015/cppkh},
  version = {0.1.1}
}
```
