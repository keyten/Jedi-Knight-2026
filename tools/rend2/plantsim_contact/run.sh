#!/bin/sh
# usage: run.sh [tr_plantsim.cpp]  - extracts the frond sim types + StepFrond, runs the
# fern contact scenarios (a character standing in / at the edge of / near a fern);
# every rms speed should be a few u/s at most (the fronds following the body sway)
set -e
D=$(dirname "$0")
SRC="${1:-$D/../../../shared/rd-rend2/tr_plantsim.cpp}"
# g++ on PATH (w64devkit)
NS=$(grep -n '^namespace' "$SRC" | head -1 | cut -d: -f1)
NS_END=$(grep -n '^} // namespace' "$SRC" | head -1 | cut -d: -f1)
SF=$(grep -n '^static void StepFrond' "$SRC" | cut -d: -f1)
WB=$(grep -n 'bones of the current state; true' "$SRC" | cut -d: -f1)
sed -n "${NS},${NS_END}p" "$SRC" > "$D/extract.inc"
sed -n "$((SF - 1)),$((WB - 1))p" "$SRC" >> "$D/extract.inc"
g++ -O2 -std=c++17 -Wno-unused-function -o "$D/plantsim_contact_test.exe" "$D/plantsim_contact_test.cpp"
"$D/plantsim_contact_test.exe"
