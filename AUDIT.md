# aviffy - audit

Scope: the C++ sources in `src/`, build in `CMakeLists.txt`, and the automated
suite in `tests/`. Reviewed against libavif 1.4.2 and jpeg-turbo 3.2.0.

## Critical

None found. No memory-safety defect, unhandled fatal path, or data-loss path
was identified in review or under the test suite (including a 120-image batch
and malformed-input cases).

## Warnings

1. **Animated AVIF loses all but the first frame.** `convertFile()` calls
   `avifDecoderNextImage()` exactly once and never checks `imageCount`. A
   multi-frame (animated) AVIF is silently reduced to frame 0. This is the
   intended behavior for a still-image converter, but it is a silent reduction,
   so it is called out here and in the README.

2. **`clap` and Exif `Orientation` are not honored.** Only `irot`/`imir` are
   applied. A file that carries rotation as a clean-aperture crop or as an Exif
   Orientation tag will be converted with its pixels unrotated. These cases are
   rare for AVIF compared with `irot`/`imir`, but they are real.

3. **No colour management for CICP-only wide-gamut / HDR.** An ICC profile, when
   present, is embedded and appearance is preserved. When colour is signalled
   only through CICP (`nclx`) - e.g. BT.2020 + PQ/HLG - there is no tone mapping
   or primaries conversion, so such images can come out with shifted colour.
   SDR `sRGB`/BT.709 CICP is fine.

4. **Non-atomic writes.** The JPEG is written directly to the target path;
   on a libjpeg error the partial file is removed, but a process crash or power
   loss mid-write could leave a truncated `.jpg`. A temp-file + `rename` would
   make writes atomic.

5. **Collision detection compares path strings, not canonical paths.** Two
   inputs are considered colliding only if their computed destination strings are
   identical. Paths that are equal after canonicalization but spelled
   differently would not be detected. In practice destinations are built from a
   common root, so spellings stay consistent.

## Suggestions

1. Expose chroma subsampling (`4:4:4` for higher quality at q>=95) and an
   option to choose the alpha flatten colour instead of hard-coded white.
2. Support a loss-free JPEG path (`-q 100` with `4:4:4`) or a `--format png`
   alternative for pixel-exact transcoding.
3. Add a `--jobs` "auto" alongside the numeric form, plus a `--max-threads` cap
   to bound memory on very large batches (each worker holds one decoded RGB
   image).
4. Honor Exif `Orientation` and apply `clap` via `avifCropRectFromCleanApertureBox`.
5. Emit a machine-readable `--json` summary for scripting.
6. Add a CMake `enable_testing()` target wrapping `tests/run_tests.sh`.

## Looks good

- **Orientation correctness is verified, not assumed.** The header comment in
  libavif 1.4.x states transforms are not applied on decode; aviffy applies them
  and T10/T11 assert the actual pixel destinations for `irot` and `imir`,
  including the dimension swap.
- **Deterministic and thread-safe by construction.** Each job owns a distinct
  output path (collisions are refused up front) and each worker owns its decoder
  and encoder; the only shared state is atomic counters and an I/O mutex.
  Verified: `-j 1` and `-j 8` produce byte-identical output.
- **Errors are contained.** A malformed file fails its own job, is reported on
  stderr, and does not abort the batch; the process still exits non-zero (T7).
- **Planning errors affect the exit code.** Missing paths and output collisions
  both yield exit 1 even when other files convert successfully (T15, T16).
- **libjpeg longjmp path is safe.** `jpeg_compress_struct` is zero-initialised
  before `setjmp`, so `jpeg_destroy_compress` on the error path is well-defined,
  and the partial output file is removed.
- **Resource ownership is RAII-managed** for the libavif decoder and RGB buffer,
  including all early-return paths.
- **Boundary inputs covered by tests:** grayscale-safe RGB path, odd dimensions,
  alpha, non-avif files, corrupt files, missing paths, empty results, dry-run.

## Test status

29/29 assertions pass (`tests/run_tests.sh`). Multithreading measured at ~5.6x
throughput on 10 cores over a 120-image batch (1.28 s -> 0.23 s).
