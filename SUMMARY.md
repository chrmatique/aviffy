# aviffy - summary

## What was built

`aviffy`, a C++17 command-line tool that converts `.avif` images to `.jpg`,
built against the system `libavif` (1.4.2) and `jpeg-turbo` (3.2.0) via
pkg-config and CMake.

Delivered features:

- Batch conversion of files and/or directories.
- Recursive scanning (`-r`); with `-o`, the output tree mirrors the input tree
  and preserves relative sub-paths.
- Without `-o`, each `.jpg` is written next to its source `.avif`.
- Multi-threaded conversion, defaulting to one worker per CPU core; `-j N` sets
  the count.
- Existing outputs are skipped by default; `--force` overwrites.
- JPEG quality `-q 1..100` (default 90).
- `--dry-run`, `--quiet`, `--verbose`, `--help`, `--version`.
- Correct `irot`/`imir` orientation, alpha composited over white, 10/12-bit
  down-scaling, embedded ICC preserved, per-file error isolation.

## Files

```
CMakeLists.txt        build (pkg-config + CMake, C++17)
src/main.cpp          CLI parsing, thread pool, orchestration
src/convert.{hpp,cpp} single-file decode -> transform -> JPEG encode
src/scan.{hpp,cpp}    input expansion, recursive walk, output-path mapping
tests/run_tests.sh    end-to-end suite (self-generating fixtures)
tests/gen_fixtures.py PNG fixtures
tests/validate.py     JPEG pixel/dimension assertions
README.md             usage and design notes
AUDIT.md              Critical / Warnings / Suggestions / Looks good
```

## Verification

- `tests/run_tests.sh`: 29 assertions, all passing. Covers single/batch/recursive
  conversion, tree mirroring, skip/force, threading, orientation (dimension swap
  and pixel placement for `irot` and `imir`), alpha flattening, quality->size,
  dry-run, corrupt input, non-avif input, missing paths, and output collisions.
- Multithreading benchmark, 120 images at 1000x700:
  `-j 1` = 1.28 s, `-j 8` = 0.23 s (~5.6x on 10 cores). `-j 1` and `-j 8`
  outputs are byte-identical.
- Clean build with `-Wall -Wextra -Wpedantic`, no warnings.

## Notable decisions

- Orientation is applied manually: libavif 1.4.x does not apply `irot`/`imir` to
  decoded pixels (per its own header comment), so aviffy rotates/mirrors the RGB
  buffer in the HEIF order.
- Two distinct inputs mapping to the same output path are refused rather than
  silently overwritten; this and other planning errors force exit code 1.
- JPEG is always 4:2:0; libjpeg-turbo's default subsampling keeps batch encoding
  fast. See AUDIT.md for the full list of known limitations (animated AVIF first
  frame only, no `clap`/Exif orientation, no CICP HDR tone mapping).
