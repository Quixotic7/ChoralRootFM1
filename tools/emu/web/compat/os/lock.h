/* SPDX-License-Identifier: GPL-3.0-only */
/* <os/lock.h> for the browser build (tools/emu/web): the wasm is single-threaded (the AudioWorklet runs the timer,
 * the UI frame and the audio ISR one after the other), so emu_hal_fw.h's CPU lock has nothing to exclude. */
#pragma once
typedef int os_unfair_lock;
#define OS_UNFAIR_LOCK_INIT 0
static inline void os_unfair_lock_lock(os_unfair_lock *l) { (void)l; }
static inline void os_unfair_lock_unlock(os_unfair_lock *l) { (void)l; }
static inline _Bool os_unfair_lock_trylock(os_unfair_lock *l) { (void)l; return 1; }
