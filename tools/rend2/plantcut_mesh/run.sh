#!/bin/sh
# CPU test of the plant saber cut mesh (shared/rd-rend2/tr_plantcut_mesh.h)
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
OUT=${TMPDIR:-/tmp}/test_plantcut_mesh.exe
$CXX -std=c++14 -O1 -Wall -Wextra -o "$OUT" test_plantcut_mesh.cpp
"$OUT"
