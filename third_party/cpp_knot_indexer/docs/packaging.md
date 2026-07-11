# Packaging

The project intentionally uses a Python build wrapper instead of CMake. The
wrapper detects a g++-style compiler, probes optimization flags, compiles the
pure C++ executables, and refreshes the runtime data folder next to the main
knot indexer binary.

Most reusable project code is implemented in inline `.hpp` headers. The build
therefore compiles the command line entrypoint plus vendored computation
backends; compatibility `.cpp` files in `src/*` are not required by the default
executable build.

## Build Script

Run the default release build. This builds all three executables:
`cpp_knot_indexer`, `che_to_coord`, and `link_pd_code`.

```sh
python build.py
```

Build one executable:

```sh
python build.py --target knot_indexer
python build.py --target che_to_coord
python build.py --target link_pd_code
```

The auxiliary modules also provide convenience wrappers:

```sh
python src/che_to_coord/build.py
python src/link_pd_code/build.py
```

Select a compiler:

```sh
python build.py --cxx g++
python build.py --cxx clang++
python build.py --cxx /opt/homebrew/bin/g++-15
```

The compiler is resolved in this order:

1. `--cxx`
2. `CXX`
3. common compiler names on `PATH`, such as `g++`, `clang++`, and `c++`

## Build Options

`--build-dir PATH`

Set the build output directory. The default is `build`.

`--target all|knot_indexer|che_to_coord|link_pd_code`

Select the executable to build. The default is `all`.

`--output PATH`

Set the exact executable output path. This is only valid when building one
target.

`--debug`

Build with `-O0 -g` instead of release optimization.

`--portable`, `--no-native`

Disable native CPU tuning flags such as `-march=native` and `-mtune=native`.
Use this for binaries intended to run on other machines.

`--no-lto`

Disable link-time optimization.

`--static-runtime`

On Windows, link libstdc++ and libgcc statically.

`--extra-cxxflag FLAG`

Append an extra compiler flag. This option can be repeated.

`--extra-ldflag FLAG`

Append an extra linker flag. This option can be repeated.

`--show-command`

Print the full compiler command before building.

`--clean`

Remove the build directory before compiling.

## Default Release Flags

The release build starts with:

- `-std=c++17`
- `-O3`
- `-DNDEBUG`
- `-DCPPKH_SHARED_LIBRARY`
- `-DHKI_WITH_SQLITE`
- `-DSQLITE_THREADSAFE=1`
- `-DSQLITE_OMIT_LOAD_EXTENSION`

The script then probes platform support before adding optional flags:

- `-pipe`
- `-march=native`
- `-mtune=native`
- `-flto`
- `-fuse-linker-plugin`
- `-fno-plt` on supported non-Windows, non-macOS platforms

On Windows, the build defines:

- `KH_THREAD_BACKEND_WIN32`
- `NOMINMAX`
- links `ws2_32` for SQLite's Windows runtime dependencies

On other platforms, the build defines:

- `KH_THREAD_BACKEND_STD`
- `-pthread`

The script also probes whether `std::filesystem` needs `-lstdc++fs`.

The main executable also includes
`third_party/cpp-pd-code-simplify/include` so the simplification backend can be
used without building a separate library.

## Data Folder Packaging

After a successful `knot_indexer` build, `build.py` copies the repository
`data` folder next to the `cpp_knot_indexer` executable. If a previous `data`
folder already exists beside the output binary, it is removed first and then
copied again.

This keeps the executable independent from hard-coded embedded data. Users can
replace the runtime database by passing `--data-folder PATH`, by passing
`--sqlite-db PATH`, or by replacing the copied `data` folder beside the
executable.

## Test Script

Run:

```sh
python test.py --rebuild
```

The test script builds the executable if needed, runs smoke and regression
checks, verifies data folder behavior, checks timeout argument handling,
compiles the auxiliary coordinate modules as standalone C++17 headers, and
checks that the default build creates all three executables.

Useful options:

- `--exe PATH`
- `--cxx COMPILER`
- `--rebuild`
- `--no-build`
- `--portable`
