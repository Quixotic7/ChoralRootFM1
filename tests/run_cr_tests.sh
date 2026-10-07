#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the ChoralRoot engine host tests (firmware/src/cr_engine.c), no make needed:
#   sh tests/run_cr_tests.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
# the boot guard's decision (firmware/src/cr_bootguard.h, main.c fm1_cstart)
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_bootguard_test tests/cr_bootguard_test.c
./build/host/cr_bootguard_test
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
# FM6 (Melodee's engine, docs/FM6.md): patches, the blob, DX7 SysEx, the bank, the deep pages, the patch store, voices
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fm6_test tests/cr_fm6_test.c -lm
./build/host/cr_fm6_test > build/host/cr_fm6_test.log || { cat build/host/cr_fm6_test.log; exit 1; }
tail -1 build/host/cr_fm6_test.log
# CZ-1 (Melodee's engine, docs/CZ1.md): Casio's tones, the blob, Casio SysEx, the banks, the deep pages, the tone store
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_cz_test tests/cr_cz_test.c -lm
./build/host/cr_cz_test > build/host/cr_cz_test.log || { cat build/host/cr_cz_test.log; exit 1; }
tail -1 build/host/cr_cz_test.log
# Felucca #61 (1.0.3.1): DIGITAL -> FM6 conversion without a divide by zero (the JieLi compiler hoists a guarded
# divide; a 0.9 ORGAN preset trapped at boot -> UBOOT). Felucca's own test, built with the divide-by-zero sanitizer.
cc -O1 -w -fsanitize=integer-divide-by-zero -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/fm4_div0_test tests/fm4_div0_test.c -lm
./build/host/fm4_div0_test > build/host/fm4_div0_test.log || { cat build/host/fm4_div0_test.log; exit 1; }
grep -c ' ok$' build/host/fm4_div0_test.log | sed 's/^/fm4_div0_test: /; s/$/ checks ok/'
# USB audio recording (Melodee's, docs/USB-AUDIO.md): usb.c's descriptors in each presentation (USB Record on, the
# console), the capture routing, the PCM packing, the ring against clock drift and the host stopping (usb_audio_stream.c)
cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio_test tests/cr_usbaudio_test.c
./build/host/cr_usbaudio_test > build/host/cr_usbaudio_test.log || { cat build/host/cr_usbaudio_test.log; exit 1; }
tail -1 build/host/cr_usbaudio_test.log
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_midi_test tests/cr_midi_test.c
exec ./build/host/cr_midi_test
