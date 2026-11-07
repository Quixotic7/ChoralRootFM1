#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Adapted for ChoralRoot (HISTORY 3y236): Melodee 3fe570d's tests/ble_tests.py, the stack's own cases (Melodee's
# settings, editor, UI and integration cases stay with Melodee; ChoralRoot's glue: tests/cr_ble_test.c).
"""The BLE stack's protocol, encryption, pairing and radio-driver fixtures (firmware/src/ble/, the fake radio
tests/ble_fake/fm1_ble.h), and the radio program's replay against the pinned Optimist interpreter. No radio needed.
  python3 tests/ble_tests.py"""
import concurrent.futures
import os
import shlex
import subprocess
import sys
from pathlib import Path

os.chdir(Path(__file__).resolve().parents[1])
OUT = Path('build/ble-tests')
CASES = [
    ('ble_prim_test', []),
    ('ble_stack_test', []),
    ('ble_stack_test', ['-DBLE_LL_ENC=1', '-DBLE_SMP_LEGACY=1']),
    ('ble_driver_test', ['-Itests/ble_fake', '-DBLE_DIAG=0']),
    ('ble_driver_test', ['-Itests/ble_fake', '-DBLE_DIAG=0', '-DBLE_LL_ENC=1', '-DBLE_SMP_LEGACY=1']),
    ('ble_central_test', []),
    ('ble_central_test', ['-DBLE_MIDI_INITIAL_READ=1']),
    ('ble_scan_test', []),
    ('ble_scan_driver_test', ['-Itests/ble_fake']),
    ('ble_central_driver_test', ['-Itests/ble_fake']),
    ('ble_vm_test', []),
]


def run(case):
    i, (name, flags) = case
    exe = OUT / f'{name}-{i}'
    cmd = shlex.split(os.environ.get('CC', 'cc')) + ['-O1', '-w', '-fsanitize=address,undefined',
                                                     '-fno-sanitize-recover=all', f'tests/{name}.c', *flags,
                                                     '-o', str(exe)]
    build = subprocess.run(cmd, capture_output=True, text=True)
    result = build if build.returncode else subprocess.run([str(exe)], capture_output=True, text=True)
    exe.with_suffix('.log').write_text(result.stdout + result.stderr)
    return f'{name} {" ".join(flags)}'.strip(), result.returncode, exe.with_suffix('.log')


if __name__ == '__main__':
    subprocess.run([sys.executable, 'tests/ble_rf_replay_test.py'], check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(run, enumerate(CASES)))
    for name, rc, log in results:
        if rc:
            print(f'{name}: FAILED ({log})')
            print(log.read_text()[-3500:])
    bad = sum(1 for _, rc, _ in results if rc)
    print(f'BLE stack: {len(results) - bad} of {len(results)} fixtures passed')
    raise SystemExit(bad != 0)
