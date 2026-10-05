# aviffy

A small, fast command-line converter that turns `.avif` images into `.jpg`.

- Batch conversion of many files
- Recursive directory scanning, with an output tree that mirrors the input tree
- Multi-threaded conversion (one worker thread per core by default)
- Correct handling of `irot`/`imir` orientation, alpha (composited over white),
  10/12-bit input, and embedded ICC profiles
- Deterministic output: the same input always produces the same JPEG bytes,
  independent of the thread count

## Build

Requires a C++17 compiler, CMake >= 3.16, and `libavif` + `libjpeg` (jpeg-turbo)
discoverable through `pkg-config`.

```sh
# macOS
brew install libavif jpeg-turbo cmake

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary is `build/aviffy`.

## Usage

```
aviffy [OPTIONS] INPUT...

INPUT may be one or more .avif files and/or directories containing .avif files.
With no output option, each .jpg is written next to its source .avif.

Options:
  -o, --output DIR     Output directory. The output tree mirrors the input tree
                       (relative sub-paths are preserved).
  -r, --recursive      Recurse into input directories.
  -q, --quality N      JPEG quality, 1-100 (default 90).
  -j, --threads N      Number of worker threads (default: number of CPU cores).
      --force          Overwrite existing .jpg files.
      --skip-existing  Skip files whose .jpg already exists (default).
      --dry-run        Show planned conversions without writing anything.
  -v, --verbose        Print extra detail.
      --quiet          Print only the final summary (errors still shown).
  -h, --help           Show this help and exit.
  -V, --version        Show version and exit.
```

Exit codes: `0` success, `1` at least one input could not be converted (or a
planning error such as a missing path or an output collision), `2` usage error.

### Examples

```sh
# One file, .jpg written beside it
aviffy photo.avif

# All .avif files in ./in, written to ./out
aviffy -o out in

# Recurse through a tree, mirroring sub-directories under ./out
aviffy -r -o out photos/

# Whole tree into ./jpg, 8 threads, high quality, overwrite existing
aviffy -r -j 8 -q 95 --force -o jpg photos/

# Preview without writing anything
aviffy -r --dry-run -o out photos/
```

Given this input:

```
photos/cover.avif
photos/2024/spring/beach.avif
```

`aviffy -r -o jpg photos/` produces:

```
jpg/cover.jpg
jpg/2024/spring/beach.jpg
```

## Design notes

- **Decoding** uses libavif (AV1 via dav1d). Images are decoded to 8-bit RGB;
  10/12/16-bit sources are depth-rescaled. Alpha, if present, is composited over
  white because JPEG has no alpha channel.
- **Orientation.** libavif 1.4.x decodes `irot` (rotation) and `imir` (mirror)
  boxes but deliberately does *not* apply them to the pixels, so aviffy applies
  them itself, in the HEIF order (rotation, then mirroring).
- **Colour.** If the source carries an ICC profile it is embedded in the output
  JPEG so wide-gamut images keep their appearance.
- **Encoding** uses libjpeg-turbo with 4:2:0 chroma subsampling.
- **Threading.** A fixed pool of worker threads pulls from a shared index over
  the job list; each thread runs its own decoder and encoder, so there is no
  shared mutable state during conversion and no lock contention on the hot path.

## Tests

```sh
tests/run_tests.sh
```

The suite builds its own AVIF fixtures with `avifenc` and checks conversion,
recursion/mirroring, skip/force, threading, orientation, alpha flattening,
quality, dry-run, error handling and output collisions. Requires `avifenc` and
Python 3 with Pillow.

## Limitations

See `AUDIT.md` for the full list. The most important ones:

- Only the first frame of an animated AVIF is converted.
- The `clap` (clean-aperture) crop and Exif `Orientation` are not applied;
  only the AVIF-native `irot`/`imir` are.
- CICP-only wide-gamut / HDR sources without an ICC profile are not tone-mapped.
- `getopt_long` is POSIX/GNU, so this does not build under MSVC out of the box.
