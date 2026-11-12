#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The bass part's job on a real second thread (tests/cr_c1_thread_test.c; docs/DUALCORE.md "Two threads on the host";
# HISTORY 3y45), with ALL 200 P5 programs:
#   sh tests/run_c1_thread_test.sh           (tests/run_cr_tests.sh runs it)
# 1. the output with the job on a thread is the inline job's, every sample, for twenty-two pairs of engines; with P5,
#    (3y177) the VA or (3y187) FM6 on the chord part also the unsplit render's and those of two splits of the test's own;
# 2. built with -fsanitize=thread the same phrases give NO report (a word both threads touch without a lock between
#    them): P5 on both parts, and the engines that ran on core 1 before it (FM6, VA, CZ-1, FM TONE);
# 3. the proof that 2 can fail: with a word planted that both parts' P5 renders write (-DC1_PLANT) the sanitizer
#    reports it. A host whose compiler has no ThreadSanitizer skips 2 and 3 and says so.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host/p5_c1
python3 tools/gen_prophet_factory.py build/host/p5_c1/melodee_prophet_factory.h all > /dev/null
I="-Ibuild/host/p5_c1 -Ibuild/gen -Ifirmware/src -Itests"
sh tests/hostcc.sh cc -std=gnu11 -O2 -w $I -o build/host/cr_c1_thread_test tests/cr_c1_thread_test.c -lm
./build/host/cr_c1_thread_test > build/host/cr_c1_thread_test.log || { cat build/host/cr_c1_thread_test.log; exit 1; }
echo "c1 thread: $(grep -c '^  ok ' build/host/cr_c1_thread_test.log) pairs of engines render the inline job's samples with the job on a second thread"
echo "c1 thread: P5's chord voices on both cores: $(sed -n 's/^split: //p' build/host/cr_c1_thread_test.log)"
echo "c1 thread: the VA's chord voices on both cores: $(sed -n 's/^va split: //p' build/host/cr_c1_thread_test.log)"
echo "c1 thread: FM6's chord voices on both cores: $(sed -n 's/^fm6 split: //p' build/host/cr_c1_thread_test.log)"
echo "c1 thread: FM TONE's chord voices on both cores: $(sed -n 's/^quad split: //p' build/host/cr_c1_thread_test.log)"
echo "c1 thread: $(sed -n "s/^effects: //p" build/host/cr_c1_thread_test.log)"
echo "c1 thread: $(sed -n "s/^drums: //p" build/host/cr_c1_thread_test.log)"
if ! sh tests/hostcc.sh cc -std=gnu11 -O1 -g -w -fsanitize=thread $I -o build/host/cr_c1_thread_tsan tests/cr_c1_thread_test.c -lm 2> build/host/cr_c1_thread_tsan.cc.log; then
    echo "c1 thread: skip: this compiler has no -fsanitize=thread (the sanitizer's run and its proof are not done)"
    exit 0
fi
sh tests/hostcc.sh cc -std=gnu11 -O1 -g -w -fsanitize=thread -DC1_PLANT $I -o build/host/cr_c1_thread_plant tests/cr_c1_thread_test.c -lm
TSAN_OPTIONS="exitcode=66 halt_on_error=0 abort_on_error=0" ./build/host/cr_c1_thread_tsan --tsan > build/host/cr_c1_thread_tsan.log 2>&1 || {
    echo "run_c1_thread_test.sh: the sanitizer reports a word both threads touch:"
    grep -a -A12 "WARNING: ThreadSanitizer" build/host/cr_c1_thread_tsan.log | head -60
    grep -a "FAIL" build/host/cr_c1_thread_tsan.log | head
    exit 1
}
n=$(grep -ac "WARNING: ThreadSanitizer" build/host/cr_c1_thread_tsan.log || true)
[ "$n" = 0 ] || { echo "run_c1_thread_test.sh: $n sanitizer reports"; exit 1; }
echo "c1 thread: ThreadSanitizer: 0 reports over $(grep -ac '^  ran ' build/host/cr_c1_thread_tsan.log) pairs (P5's chord voices on both cores with a P5 bass, a UNISON bass, and FM6, the VA, CZ-1, FM TONE and WHEEL as the bass; the VA's and FM6's with a P5, VA, FM6 and CZ-1 bass; CZ-1, FM TONE)"
(TSAN_OPTIONS="exitcode=66 halt_on_error=0 abort_on_error=0" ./build/host/cr_c1_thread_plant --tsan > build/host/cr_c1_thread_plant.log 2>&1) 2> /dev/null && {
    echo "run_c1_thread_test.sh: a planted shared word gives no report: the sanitizer's run proves nothing"; exit 1; }
grep -aq "WARNING: ThreadSanitizer: data race" build/host/cr_c1_thread_plant.log && grep -aq "c1_plant" build/host/cr_c1_thread_plant.log || {
    echo "run_c1_thread_test.sh: the planted word's run failed without naming it:"; head -20 build/host/cr_c1_thread_plant.log; exit 1; }
echo "c1 thread: a planted word that both parts write is reported (the sanitizer sees the two threads)"
