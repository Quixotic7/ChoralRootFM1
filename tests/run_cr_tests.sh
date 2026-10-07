#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the ChoralRoot engine host tests (firmware/src/cr_engine.c), no make needed:
#   sh tests/run_cr_tests.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_engine_test tests/cr_engine_test.c firmware/src/cr_engine.c
./build/host/cr_engine_test
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_loop_test tests/cr_loop_test.c firmware/src/cr_loop.c firmware/src/cr_engine.c
./build/host/cr_loop_test
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_settings_test tests/cr_settings_test.c firmware/src/cr_settings.c firmware/src/cr_engine.c
./build/host/cr_settings_test
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -Ibuild/gen -o build/host/cr_va_test tests/cr_va_test.c -lm
./build/host/cr_va_test
cc -std=c99 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_trans_test tests/cr_trans_test.c -lm
./build/host/cr_trans_test
# backup / restore SysEx (cr_backup.c) on the emulator's firmware build (tools/emu/emu_firmware.h; macOS: os_unfair_lock)
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_backup_test tests/cr_backup_test.c -lm
./build/host/cr_backup_test
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_midi_test tests/cr_midi_test.c
exec ./build/host/cr_midi_test
