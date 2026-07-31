#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes
#
# Builds the conformance drawing and writes it under a SERIAL-NUMBERED name.
#
# The serial exists because two rounds were spent on an error that turned out
# to be AutoCAD reading a stale copy: the reported line number was identical
# across a fix, which reads exactly like "the fix did nothing" and is equally
# consistent with "the file never arrived". A name that changes every time
# makes the two distinguishable at a glance, and the md5 settles it outright.
#
# Usage:  tests/acad/gen_conformance.sh [outdir]
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-/tmp}
NCAD=$ROOT/build/src/app/ncad
LSP=$ROOT/tests/acad/r2000_conformance.lsp

[ -x "$NCAD" ] || { echo "no ncad at $NCAD -- build first"; exit 1; }

SERIAL_FILE=$OUT/.cf_serial
SERIAL=$(( $(cat "$SERIAL_FILE" 2>/dev/null || echo 0) + 1 ))
echo "$SERIAL" > "$SERIAL_FILE"

HASH=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo nogit)
STAMP=$(printf '%03d' "$SERIAL")
R2000=$OUT/cf_${STAMP}_${HASH}_r2000.dxf
R12=$OUT/cf_${STAMP}_${HASH}_r12.dxf

"$NCAD" "$LSP" -e '(conform)' -e "(progn
     (setvar \"DXFVERSION\" \"R2000\") (command \"SAVEAS\" \"$R2000\")
     (setvar \"DXFVERSION\" \"R12\")   (command \"SAVEAS\" \"$R12\"))" > /dev/null

for f in "$R2000" "$R12"; do
    printf '%s\n  %s bytes  md5 %s\n' \
        "$f" \
        "$(wc -c < "$f" | tr -d ' ')" \
        "$(md5sum "$f" 2>/dev/null | cut -d' ' -f1 || md5 -q "$f")"
done
