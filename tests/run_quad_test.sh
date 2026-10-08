#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# QUAD alone (docs/QUAD.md): regenerate the tables header and the goldens from the reference model, run the host test
# (sanitized: an int32 wrap aborts), then the CPU figure on an unsanitized build (macOS: host instructions a sample,
# device estimate = instructions x 128 / 259 us of the 2902 us half, docs/INTEGRATION.md):
#   sh tests/run_quad_test.sh            (QUAD_NOREF=1: keep the committed header and goldens)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
[ -n "$QUAD_NOREF" ] || python3 tests/quad_ref.py
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -DFELUCCA_QUAD=1 -Ibuild/gen -o build/host/cr_quad_test tests/cr_quad_test.c -lm
./build/host/cr_quad_test
cc -std=c99 -O2 -w -DFELUCCA_QUAD=1 -Ibuild/gen -o build/host/cr_quad_cpu tests/cr_quad_test.c -lm
./build/host/cr_quad_cpu cpu | grep -v '^cr_quad_test:'
