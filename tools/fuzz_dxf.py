#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

"""Feed malformed DXF to the reader and see whether it survives.

The reader parses UNTRUSTED input -- another CAD program's output, a corrupt
download, a file truncated in transit -- so "it must never crash, however
malformed" is a requirement rather than an aspiration. This is how that gets
checked, because reasoning about it has a track record of missing things.

It has paid for itself twice. The first run, in the audit of 2026-08-17, found
nothing at all -- and that was itself the finding: the seed drawing had no BLOCKS
section, so no mutation could reach the block reader, where four use-after-frees
were sitting. gen_sample now emits blocks, a MINSERT and a bulged polyline, and
against that seed the same mutations do reach them.

So the rule this encodes: A CORPUS ONLY TESTS WHAT THE SEED CONTAINS. When an
entity kind or a section is added to the format, add it to the sample drawing or
this script goes quiet about it.

Run it against the SANITIZER build. A release build will shrug off most of what
this produces -- reading freed memory usually just returns something wrong --
which is exactly the failure mode that survived 1188 tests.

    cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \\
          -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    cmake --build build-asan
    tools/fuzz_dxf.py build-asan

Exit status is 0 when every case survived, 1 when any crashed or hung, so it can
be wired into something later. It is deliberately NOT part of ctest: it takes
minutes rather than milliseconds, and a suite people stop running is worse than
one that does less.
"""

import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile

# Values chosen to be individually plausible and collectively hostile: things a
# real broken writer emits, plus the ones that have historically hurt.
BAD_CODES = [b'notanumber', b'', b'-1', b'999999999', b'2147483648',
             b'-2147483649', b'0x10', b' ', b'62', b'70']
BAD_VALUES = [b'99999999999999999999', b'-99999999', b'nan', b'inf', b'-inf',
              b'1e400', b'', b'\x00\x01\x02', b'A' * 4000, b'32768', b'-32768']


def mutate(src, seed):
    """Every case, as (name, bytes). Deterministic for a given seed."""
    rng = random.Random(seed)
    lines = src.split(b'\n')
    out = []

    # Truncation. EOF mid-group, mid-entity, mid-section -- a download cut
    # short, and the shape that found two of the four block use-after-frees.
    for pct in range(1, 100, 3):
        out.append((f'trunc{pct:02d}', src[:len(src) * pct // 100]))

    # A group CODE that is not what the reader expects there.
    for i in range(40):
        ls = list(lines)
        ls[rng.randrange(len(ls))] = rng.choice(BAD_CODES)
        out.append((f'badcode{i:02d}', b'\n'.join(ls)))

    # A VALUE that is junk, absurd, or non-finite.
    for i in range(40):
        ls = list(lines)
        ls[rng.randrange(len(ls))] = rng.choice(BAD_VALUES)
        out.append((f'badvalue{i:02d}', b'\n'.join(ls)))

    # Structural damage: counts stop agreeing with what follows, and the
    # terminators -- ENDBLK, SEQEND, ENDSEC -- stop arriving.
    for i in range(40):
        ls = list(lines)
        for _ in range(rng.randrange(1, 20)):
            if len(ls) > 2:
                del ls[rng.randrange(len(ls))]
        out.append((f'dropped{i:02d}', b'\n'.join(ls)))

    # Bit flips, for everything nobody thought to enumerate.
    for i in range(40):
        b = bytearray(src)
        for _ in range(rng.randrange(1, 12)):
            b[rng.randrange(len(b))] ^= 1 << rng.randrange(8)
        out.append((f'bitflip{i:02d}', bytes(b)))

    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('build', nargs='?', default='build-asan',
                    help='build directory to test (default: build-asan)')
    ap.add_argument('--seed', type=int, default=20260817,
                    help='RNG seed; the default reproduces the audit run')
    ap.add_argument('--timeout', type=float, default=15.0,
                    help='seconds before a case counts as hung')
    ap.add_argument('--keep', metavar='DIR',
                    help='write the generated cases here instead of a temp dir')
    args = ap.parse_args()

    ncad = os.path.join(args.build, 'src/app/ncad')
    gen = os.path.join(args.build, 'tools/ncad_gen_sample')
    for path in (ncad, gen):
        if not os.path.exists(path):
            sys.exit(f'not built: {path}')

    work = args.keep or tempfile.mkdtemp(prefix='ncad_fuzz_')
    os.makedirs(work, exist_ok=True)
    seed_dxf = os.path.join(work, 'seed.dxf')
    subprocess.run([gen, seed_dxf], check=True, stdout=subprocess.DEVNULL)

    with open(seed_dxf, 'rb') as f:
        src = f.read()

    cases = mutate(src, args.seed)
    print(f'{len(cases)} cases from a {len(src)}-byte seed, against {ncad}')

    bad = []
    for name, data in cases:
        path = os.path.join(work, name + '.dxf')
        with open(path, 'wb') as f:
            f.write(data)
        try:
            p = subprocess.run([ncad, '-e', f'(command "DXFIN" "{path}")'],
                               capture_output=True, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            bad.append((name, 'HANG'))
            print(f'  HANG   {name}')
            continue

        err = p.stderr.decode('utf-8', 'replace')
        # A sanitizer report, or death by signal. An ordinary non-zero exit is
        # the reader REFUSING a bad file, which is the correct answer.
        if 'Sanitizer' in err or 'runtime error' in err:
            bad.append((name, 'SANITIZER'))
            print(f'  ERROR  {name}: {err.splitlines()[1] if len(err.splitlines()) > 1 else err[:120]}')
        elif p.returncode < 0:
            bad.append((name, f'signal {-p.returncode}'))
            print(f'  CRASH  {name}: signal {-p.returncode}')

    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)

    print(f'\n{len(cases) - len(bad)} survived, {len(bad)} failed')
    if bad:
        print('rerun a single case with:')
        print(f'  {ncad} -e \'(command "DXFIN" "<case>.dxf")\'')
        print('and keep the cases next time with --keep DIR')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
