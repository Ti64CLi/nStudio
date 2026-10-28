#!/bin/sh
# Integration test: nStudio and nasm, end to end, on the build machine.
#
# Builds nasm's real command line for the host, from a nasm checkout, and
# nStudio's side of the exchange (nstudio_side.c, around nasmlink.c and
# asmdiag.c). Every scenario in scenarios/ is then assembled exactly as the
# editor would assemble it, and what nStudio concludes is compared with the
# scenario's `expect` file. Finally it checks what the two programs share:
# the settings file must survive a round trip through nasm, and the source
# files each keeps a copy of must still be identical.
#
# Usage: sh integration/run.sh [<nasm checkout>]      (default: ./nasm)
#    or: make integration [NASM_DIR=<nasm checkout>]
#
# A scenario is a directory holding the sources and `expect`, plus optionally:
#   source   the file to assemble (default: main.asm.tns)
#   args     the user's extra nasm arguments, as set in Settings
#   answers  how nasm's dialogs are answered (NASM_HOST_ANSWERS, e.g. "0,1")
#   diag     where nasm is asked to write diagnostics (default: nstudio_diag.json)
#   setup    shell commands run in a copy of the scenario first, for files
#            that are better generated than committed
#   no_nasm  run as if nasm could not be started
# To write the `expect` of a new scenario, run it once and read the report it
# prints - then check every line of it by hand before saving it.
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
nasm_arg=${1:-$root/nasm}
if [ ! -f "$nasm_arg/tests/build_host.sh" ]; then
  echo "error: no nasm checkout at '$nasm_arg' (pass its path, or NASM_DIR=)" >&2
  exit 1
fi
nasm_dir=$(cd "$nasm_arg" && pwd)

build=$here/build
rm -rf "$build"
mkdir -p "$build/work"

echo "building nasm (host) from $nasm_dir..."
sh "$nasm_dir/tests/build_host.sh" "$build" nstudio.cfg || exit 1

echo "building nStudio's side..."
cc -Wall -W -O1 -I"$root" -o "$build/nstudio_side" "$here/nstudio_side.c" \
  "$root/nasmlink.c" "$root/asmdiag.c" "$root/optab.c" "$root/util.c" ||
  exit 1
cc -Wall -W -O1 -I"$root" -I"$here/host" '-DSETTINGS_FILE="nstudio.cfg"' \
  -o "$build/settings_side" "$here/settings_side.c" "$here/host_ui.c" \
  "$root/settings.c" "$root/fileio.c" || exit 1

pass=0
fail=0
have_python=0
command -v python3 >/dev/null 2>&1 && have_python=1

# --- scenarios: one assemble each, judged as the editor judges it ----------
for scen in "$here"/scenarios/*/; do
  name=$(basename "$scen")
  work=$build/work/$name
  cp -R "$scen" "$work"

  if [ -f "$work/setup" ] && ! (cd "$work" && sh ./setup); then
    echo "FAIL $name (setup failed)"
    fail=$((fail + 1))
    continue
  fi

  source=main.asm.tns
  [ -f "$work/source" ] && source=$(cat "$work/source")
  args=""
  [ -f "$work/args" ] && args=$(cat "$work/args")
  answers=""
  [ -f "$work/answers" ] && answers=$(cat "$work/answers")
  diag=nstudio_diag.json
  [ -f "$work/diag" ] && diag=$(cat "$work/diag")
  nasm=$build/nasm_cli
  [ -f "$work/no_nasm" ] && nasm=$build/no_such_nasm

  (cd "$work" && NASM_HOST_ANSWERS=$answers "$build/nstudio_side" "$nasm" \
    "$work/$source" "$args" "$work/$diag" >"$work/report" 2>"$work/log")

  if ! diff -u "$scen/expect" "$work/report" >"$work/diff"; then
    echo "FAIL $name (report differs from expect)"
    sed 's/^/    /' "$work/diff"
    fail=$((fail + 1))
    continue
  fi

  # what nasm wrote must follow the documented format, beyond what nStudio's
  # tolerant parser happens to accept
  if [ "$have_python" -eq 1 ] && [ -f "$work/$diag" ] &&
    ! python3 -I "$here/check_json.py" "$work/$diag" >"$work/json"; then
    echo "FAIL $name (diagnostics file breaks the format)"
    sed 's/^/    /' "$work/json"
    fail=$((fail + 1))
    continue
  fi

  pass=$((pass + 1))
done

# --- the settings file, through both programs -------------------------------
# nStudio saves a configuration unlike the defaults in every field (bracket
# colours included, which nasm does not know); nasm then opens a file in a new
# folder and so rewrites the configuration; nStudio must read back exactly what
# it saved, apart from the folder nasm recorded.
cfgdir=$build/settings
mkdir -p "$cfgdir"
(
  cd "$cfgdir" &&
    "$build/settings_side" write &&
    "$build/settings_side" dump >saved &&
    "$build/nasm_host" --set-last-dir /documents/elsewhere &&
    "$build/settings_side" dump >loaded
) || echo "settings round trip did not run" >"$cfgdir/loaded"
sed 's|^last_dir=.*|last_dir=/documents/elsewhere|' "$cfgdir/saved" \
  >"$cfgdir/expected"
if diff -u "$cfgdir/expected" "$cfgdir/loaded" >"$cfgdir/diff"; then
  pass=$((pass + 1))
else
  echo "FAIL settings_round_trip (nStudio lost settings through nasm)"
  sed 's/^/    /' "$cfgdir/diff"
  fail=$((fail + 1))
fi

# --- the files both repositories carry a copy of -----------------------------
for f in optab.c optab.h fileio.c fileio.h; do
  if cmp -s "$root/$f" "$nasm_dir/$f"; then
    pass=$((pass + 1))
  else
    echo "FAIL shared_copy $f (nStudio's and nasm's copies differ)"
    fail=$((fail + 1))
  fi
done

[ "$have_python" -eq 1 ] ||
  echo "note: python3 not found, diagnostics files were not format-checked"
echo "-----------------------------------"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
