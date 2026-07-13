# Command Line

The main executable is `cpp_knot_indexer` on Unix-like systems and
`cpp_knot_indexer.exe` on Windows.

## Input Forms

Provide a PD code directly:

```sh
build/cpp_knot_indexer --pd-code "[[1,5,2,4],[3,1,4,6],[5,3,6,2]]"
```

Read a PD code from a file:

```sh
build/cpp_knot_indexer --pd-file code.txt
```

Read a PD code from stdin:

```sh
build/cpp_knot_indexer < code.txt
```

## Runtime Options

`--pd-code TEXT`

Read the PD code from the command line.

`--pd-file PATH`

Read the PD code from a file.

`--timeout SEC`

Set the maximum number of seconds for one lookup pipeline. The timeout covers
the original-PD HOMFLY-PT worker, original-PD Khovanov worker, simplification
worker, and any simplified-PD HOMFLY-PT or Khovanov workers that are started
after simplification finishes. The default is `60`. Use `0` to disable worker
timeouts. Negative values are rejected.

`--data-folder PATH`

Use an explicit invariant database folder. A valid data folder must contain:

- `homfly/sorted_HOMFLY-PT.txt`
- `khovanov/sorted_khovanov.txt`
- `knotname-reg/`

When this option is present, the executable does not search default data folder
locations. The folder name itself does not have to be `data`.

`--ban-simplify`

Disable PD-code simplification during lookup. With this option, the executable
computes HOMFLY-PT and Khovanov only on the input PD code and does not start the
simplification worker or any simplified-PD invariant workers.

`--print-invariants`

Print the final HOMFLY-PT and Khovanov invariant strings to stderr. If PD-code
simplification finishes successfully before the timeout, also print the
simplified PD code to stderr.

`--show-time`

Print timing checkpoints to stderr. Times are elapsed seconds from the start of
the lookup pipeline. The emitted checkpoints are:

- HOMFLY-PT result selected
- Khovanov result selected
- simplified PD code computed, when simplification finishes successfully
- simplification terminated, when the simplification worker is cancelled,
  interrupted, or killed by timeout before producing a simplified PD code
- knot-name lookup resolved, after the invariant data files have been queried

`--verbose`

Print worker status, elapsed time, failure details, timeout details,
simplified PD code, selected original/simplified invariant sources, and
successful invariant strings to stderr.

`--help`, `-h`

Print command usage.

## Path Encoding

On Windows, all executables read command line arguments from the native Unicode
command line instead of relying on the narrow `argv` code page. This means paths
passed to `--pd-file`, `--data-folder`, `--input`, and `--output` work whether
the invoking shell is using a GBK-compatible code page or UTF-8.

On Linux and macOS, paths are passed through as the native byte strings provided
by the shell. UTF-8 paths are supported as usual.

## Output Contract

Candidate knot names are written to stdout, one name per line.

Diagnostics, verbose invariant strings, timeout messages, and worker failure
details are written to stderr. Timing checkpoints from `--show-time` are also
written to stderr and never to stdout.

For each invariant type, the original-PD and simplified-PD computations race
when both are available. The first successful HOMFLY-PT result is used and the
other HOMFLY-PT worker is cancelled; Khovanov follows the same rule. If one
invariant type fails or times out but the other succeeds, lookup continues with
the successful invariant. If both invariant types fail or time out, the program
exits with status `2`.

When `--ban-simplify` is present, only the original-PD HOMFLY-PT and Khovanov
workers are started.

Pressing `Ctrl+C` requests a clean shutdown. Active worker processes are
terminated and the program exits with status `130`.

## Default Data Folder Search

If `--data-folder` is not provided, the executable searches for a folder named
`data` in this order:

1. Next to the executable, such as `build/data`
2. One directory above the executable, such as `data` when running
   `build/cpp_knot_indexer`

If neither location is valid, the program exits with an error.

## Internal Worker Options

The executable uses internal worker options when spawning itself:

- `--worker khovanov|homfly`
- `--input PATH`
- `--output PATH`

These options are implementation details and are not intended as the public
user interface.

## Auxiliary Tools

The coordinate conversion tools have separate executables and documentation:

- [che_to_coord](che-to-coord.md)
- [link_pd_code](link-pd-code.md)
