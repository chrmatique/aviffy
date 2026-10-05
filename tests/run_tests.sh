#!/usr/bin/env bash
# End-to-end test suite for aviffy. Requires: avifenc, python3 + Pillow.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${AVIFFY_BIN:-$ROOT/build/aviffy}"
WORK="$ROOT/build/test-work"
VAL="$ROOT/tests/validate.py"
FIX="$WORK/src"

PASS=0
FAIL=0

pass() { PASS=$((PASS + 1)); printf '  PASS  %s\n' "$1"; }
failtest() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1"; }

assert_eq() { # name expected actual
    if [ "$2" = "$3" ]; then pass "$1"; else failtest "$1 (expected '$3', got '$2')"; fi
}

assert_true() { # name condition-result(0/1)
    if [ "$2" -eq 0 ]; then pass "$1"; else failtest "$1"; fi
}

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "missing required tool: $1"; exit 1; }
}

need avifenc
need python3

if [ ! -x "$BIN" ]; then
    echo "building aviffy..."
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null || exit 1
    cmake --build "$ROOT/build" -j >/dev/null || exit 1
fi

rm -rf "$WORK"
mkdir -p "$FIX"
python3 "$ROOT/tests/gen_fixtures.py" "$FIX" >/dev/null || exit 1

# --- build AVIF fixtures -----------------------------------------------------
avifenc -l "$FIX/base.png"   "$FIX/base.avif"   >/dev/null 2>&1 || exit 1
avifenc -l "$FIX/solid.png"  "$FIX/solid.avif"  >/dev/null 2>&1 || exit 1
avifenc -l "$FIX/odd.png"    "$FIX/odd.avif"    >/dev/null 2>&1 || exit 1
avifenc -l "$FIX/alpha.png"  "$FIX/alpha.avif"  >/dev/null 2>&1 || exit 1
avifenc -l "$FIX/orient.png" "$FIX/orient.avif" >/dev/null 2>&1 || exit 1
avifenc -l --irot 1 "$FIX/orient.png" "$FIX/rot.avif" >/dev/null 2>&1 || exit 1
avifenc -l --imir 1 "$FIX/orient.png" "$FIX/mir.avif" >/dev/null 2>&1 || exit 1

echo "== aviffy test suite =="

# --- T1: help / version ------------------------------------------------------
"$BIN" --help >/dev/null 2>&1; assert_eq "T1.1 --help exits 0" 0 $?
"$BIN" --version | grep -q "aviffy "; assert_eq "T1.2 --version prints name" 0 $?
"$BIN" >/dev/null 2>&1; assert_eq "T1.3 no input exits 2" 2 $?

# --- T2: single file, output next to source ----------------------------------
mkdir -p "$WORK/t2"
cp "$FIX/base.avif" "$WORK/t2/base.avif"
"$BIN" "$WORK/t2/base.avif" >/dev/null 2>&1
assert_true "T2.1 output written beside source" "$([ -f "$WORK/t2/base.jpg" ]; echo $?)"
assert_eq "T2.2 dimensions preserved" "120x80" "$(python3 "$VAL" dims "$WORK/t2/base.jpg")"

# --- T3: batch directory, non-recursive, mirrored output ---------------------
mkdir -p "$WORK/t3/in/nested"
cp "$FIX/base.avif"  "$WORK/t3/in/base.avif"
cp "$FIX/solid.avif" "$WORK/t3/in/solid.avif"
cp "$FIX/odd.avif"   "$WORK/t3/in/odd.avif"
cp "$FIX/base.avif"  "$WORK/t3/in/nested/skipme.avif"
"$BIN" -o "$WORK/t3/out" "$WORK/t3/in" >/dev/null 2>&1
n=$(ls "$WORK/t3/out"/*.jpg 2>/dev/null | wc -l | tr -d ' ')
assert_eq "T3.1 top-level files converted" "3" "$n"
assert_true "T3.2 nested dir ignored without -r" "$([ ! -e "$WORK/t3/out/nested" ]; echo $?)"

# --- T4: recursive, mirrored tree -------------------------------------------
mkdir -p "$WORK/t4/in/sub1" "$WORK/t4/in/sub2/deep"
cp "$FIX/base.avif"  "$WORK/t4/in/root.avif"
cp "$FIX/solid.avif" "$WORK/t4/in/sub1/d.avif"
cp "$FIX/odd.avif"   "$WORK/t4/in/sub2/deep/e.avif"
"$BIN" -r -o "$WORK/t4/out" "$WORK/t4/in" >/dev/null 2>&1
ok=0
for f in root.jpg sub1/d.jpg sub2/deep/e.jpg; do
    [ -f "$WORK/t4/out/$f" ] || { ok=1; echo "       missing $f"; }
done
assert_true "T4.1 recursive tree mirrored under output dir" "$ok"

# --- T5: skip-existing default + --force ------------------------------------
out5=$("$BIN" -o "$WORK/t3/out" "$WORK/t3/in" 2>&1)
echo "$out5" | grep -q "0 converted, 3 skipped"; assert_eq "T5.1 existing files skipped by default" 0 $?
out5f=$("$BIN" --force -o "$WORK/t3/out" "$WORK/t3/in" 2>&1)
echo "$out5f" | grep -q "3 converted, 0 skipped"; assert_eq "T5.2 --force overwrites" 0 $?

# --- T6: explicit thread count ----------------------------------------------
mkdir -p "$WORK/t6/in"
cp "$FIX/base.avif" "$WORK/t6/in/a.avif"
cp "$FIX/solid.avif" "$WORK/t6/in/b.avif"
cp "$FIX/odd.avif" "$WORK/t6/in/c.avif"
"$BIN" -j 4 -o "$WORK/t6/out" "$WORK/t6/in" >/dev/null 2>&1
n=$(ls "$WORK/t6/out"/*.jpg 2>/dev/null | wc -l | tr -d ' ')
assert_eq "T6.1 -j 4 converts all files" "3" "$n"

# --- T7: corrupt file is tolerated, exit code 1 -----------------------------
mkdir -p "$WORK/t7/in"
cp "$FIX/solid.avif" "$WORK/t7/in/good.avif"
head -c 512 /dev/urandom > "$WORK/t7/in/corrupt.avif"
"$BIN" -o "$WORK/t7/out" "$WORK/t7/in" >/dev/null 2>&1
assert_eq "T7.1 exit code 1 when a file fails" 1 $?
assert_true "T7.2 good file still converted" "$([ -f "$WORK/t7/out/good.jpg" ]; echo $?)"
assert_true "T7.3 no output for corrupt file" "$([ ! -f "$WORK/t7/out/corrupt.jpg" ]; echo $?)"

# --- T8: dry run writes nothing ---------------------------------------------
mkdir -p "$WORK/t8/in"
cp "$FIX/base.avif" "$WORK/t8/in/a.avif"
"$BIN" -o "$WORK/t8/out" --dry-run "$WORK/t8/in" >/dev/null 2>&1
assert_true "T8.1 dry-run creates no output dir" "$([ ! -e "$WORK/t8/out" ]; echo $?)"

# --- T9: quality changes file size ------------------------------------------
mkdir -p "$WORK/t9"
cp "$FIX/base.avif" "$WORK/t9/a.avif"
"$BIN" -q 30 -o "$WORK/t9/lo" "$WORK/t9/a.avif" >/dev/null 2>&1
"$BIN" -q 95 -o "$WORK/t9/hi" "$WORK/t9/a.avif" >/dev/null 2>&1
lo=$(stat -f%z "$WORK/t9/lo/a.jpg"); hi=$(stat -f%z "$WORK/t9/hi/a.jpg")
assert_true "T9.1 higher quality => larger file ($lo < $hi)" "$([ "$lo" -lt "$hi" ]; echo $?)"

# --- T10: irot=1 (90 deg CCW) swaps dimensions and rotates content ----------
mkdir -p "$WORK/t10"
cp "$FIX/rot.avif" "$WORK/t10/rot.avif"
"$BIN" -o "$WORK/t10/out" "$WORK/t10/rot.avif" >/dev/null 2>&1
assert_eq "T10.1 irot: dimensions swapped 40x20 -> 20x40" "20x40" "$(python3 "$VAL" dims "$WORK/t10/out/rot.jpg")"
python3 "$VAL" near "$WORK/t10/out/rot.jpg" 3 32 255 0 0 40; assert_eq "T10.2 irot=1 moves red block to bottom-left" 0 $?
python3 "$VAL" near "$WORK/t10/out/rot.jpg" 3 3 0 0 255 40;  assert_eq "T10.3 irot=1 leaves top-left blue" 0 $?

# --- T11: imir=1 (left-right mirror) ----------------------------------------
mkdir -p "$WORK/t11"
cp "$FIX/mir.avif" "$WORK/t11/mir.avif"
"$BIN" -o "$WORK/t11/out" "$WORK/t11/mir.avif" >/dev/null 2>&1
python3 "$VAL" near "$WORK/t11/out/mir.jpg" 32 3 255 0 0 40; assert_eq "T11.1 imir=1 moves red block to top-right" 0 $?

# --- T12: alpha composited over white ---------------------------------------
mkdir -p "$WORK/t12"
cp "$FIX/alpha.avif" "$WORK/t12/alpha.avif"
"$BIN" -o "$WORK/t12/out" "$WORK/t12/alpha.avif" >/dev/null 2>&1
python3 "$VAL" near "$WORK/t12/out/alpha.jpg" 5 20 255 0 0 40;   assert_eq "T12.1 opaque red preserved" 0 $?
python3 "$VAL" near "$WORK/t12/out/alpha.jpg" 55 20 255 255 255 20; assert_eq "T12.2 transparent area -> white" 0 $?

# --- T13: non-avif input rejected -------------------------------------------
mkdir -p "$WORK/t13"
cp "$FIX/base.png" "$WORK/t13/notavif.png"
"$BIN" "$WORK/t13/notavif.png" >/dev/null 2>&1; assert_eq "T13.1 non-avif input exits 1" 1 $?
echo "junk" > "$WORK/t13/bogus.jpg"
"$BIN" "$WORK/t13/bogus.jpg" >/dev/null 2>&1; assert_eq "T13.2 .jpg input exits 1" 1 $?

# --- T14: outputs are valid JPEGs -------------------------------------------
python3 "$VAL" is_jpeg "$WORK/t2/base.jpg"; assert_eq "T14.1 output has JPEG SOI marker" 0 $?

# --- T15: output collision between two inputs is refused --------------------
mkdir -p "$WORK/t15/a" "$WORK/t15/b"
cp "$FIX/base.avif"  "$WORK/t15/a/same.avif"
cp "$FIX/solid.avif" "$WORK/t15/b/same.avif"
out15=$("$BIN" -o "$WORK/t15/out" "$WORK/t15/a" "$WORK/t15/b" 2>&1)
assert_eq "T15.1 colliding outputs exit 1" 1 $?
echo "$out15" | grep -qi "collision"; assert_eq "T15.2 collision reported" 0 $?
n=$(ls "$WORK/t15/out"/*.jpg 2>/dev/null | wc -l | tr -d ' ')
assert_eq "T15.3 only one colliding file written" "1" "$n"

# --- T16: missing input path is reported and fails --------------------------
"$BIN" "$WORK/does-not-exist.avif" >/dev/null 2>&1; assert_eq "T16.1 missing input exits 1" 1 $?

echo
echo "== $PASS passed, $FAIL failed =="
[ "$FAIL" -eq 0 ]
