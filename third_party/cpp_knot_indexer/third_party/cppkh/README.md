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

Windows:

```bat
package.bat
```

PowerShell:

```powershell
.\package.ps1
```

Linux / macOS / MSYS2:

```sh
sh package.sh
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

On the full 8397-case benchmark, both C++ paths matched every bundled JavaKh
result. `cppkh` finished in `61.596s`, `cppkh-interface` batch API finished in
`61.392s` after its executable was already cached, and the patched bundled
JavaKh native multiline runner finished in `466.562s`. That is a `7.575x`
Java/cppkh speedup and a `7.600x` Java/cppkh-interface speedup.

Peak RSS on the same prepared full input was `26.04 MiB` for `cppkh`,
`68.08 MiB` for `cppkh-interface` including Python plus the child executable,
and `453.57 MiB` for patched JavaKh.

![cppkh benchmark runtime and memory chart](docs/assets/benchmark_runtime_memory.png)

## Shared Library

Build a shared library instead of an executable:

```bat
package.bat --shared --name cppkh
```

```sh
sh package.sh --shared --name cppkh
```

This produces `cppkh.dll`, `libcppkh.so`, or `libcppkh.dylib`, depending on the
platform. Any non-system runtime libraries found by the package script are
copied beside it.

## Documentation

- [Build and packaging options](docs/BUILD_AND_PACKAGING.md)
- [Command-line options](docs/CLI_OPTIONS.md)
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
