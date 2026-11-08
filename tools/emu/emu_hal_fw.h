/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 HAL on the emulator (firmware side; emu_firmware.h includes it after hostsim.c).
 * As the stubs of tests/ui_test.c, but live: the display writes emu_hal.lcd, the LEDs are emu_hal.led /
 * led_dim, the input edges and encoder steps come from emu_fw_tick (the 1 ms timer, as fm1_input_tick
 * delivers them on the device), the interrupt-off sections take the emulator's CPU lock. */

/* -------------------------------------------------------- time --- */
#define FM1_TICKS_PER_US 1u                       /* fm1_ticks: microseconds */
static uint64_t emu_t0_ns;
static volatile uint32_t emu_sim_us;              /* --bench: simulated time */
static uint8_t emu_sim;
static uint32_t fm1_ticks(void)
{
    struct timespec ts;
    if (emu_sim)
        return emu_sim_us;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec - emu_t0_ns) / 1000u);
}
static void fm1_wdt_feed(void) {}

/* ------------------------------------------- the CPU lock (ISRs) --- */
/* the audio ISR, the timer ISR and the UI's interrupt-off sections exclude each other */
/* os_unfair_lock: the waiting audio thread lends its priority to the holder (no priority inversion);
 * made recursive (the UI frame holds it and the firmware's own fm1_irq_off nests inside) */
static os_unfair_lock emu_cpu = OS_UNFAIR_LOCK_INIT;
static pthread_t emu_cpu_owner;
static uint32_t emu_cpu_depth;
static int emu_cpu_mine(void)
{
    return pthread_equal(__atomic_load_n(&emu_cpu_owner, __ATOMIC_ACQUIRE), pthread_self());
}
static void fm1_irq_off(void)
{
    if (!emu_cpu_mine()) {
        os_unfair_lock_lock(&emu_cpu);
        __atomic_store_n(&emu_cpu_owner, pthread_self(), __ATOMIC_RELEASE);
    }
    emu_cpu_depth++;
}
static int emu_cpu_try(void)                      /* fm1_irq_off without waiting; 0: busy */
{
    if (!emu_cpu_mine()) {
        if (!os_unfair_lock_trylock(&emu_cpu))
            return 0;
        __atomic_store_n(&emu_cpu_owner, pthread_self(), __ATOMIC_RELEASE);
    }
    emu_cpu_depth++;
    return 1;
}
static void fm1_irq_on(void)
{
    if (--emu_cpu_depth == 0) {
        __atomic_store_n(&emu_cpu_owner, (pthread_t)0, __ATOMIC_RELEASE);
        os_unfair_lock_unlock(&emu_cpu);
    }
}

/* --------------------------------------------- keys, LEDs, knobs --- */
#if EMU_SMK37                                     /* the SMK-37: its own input layer on a modelled panel */
#include "emu_smk_hal.h"
#else
#define FM1_NCOL 11u
#define FM1_KEYMAP emu_keymap
#define fm1_led (emu_hal.led)
#define fm1_led_dim (emu_hal.led_dim)
#define fm1_led_dim2 (emu_hal.led_mid)
#define fm1_led_faint (emu_hal.led_faint)
#define KN_QUICK() (emu_hal.knob_quick)            /* cr_pages.c kn_turn: a script's "knobs quick" (emu_hooks.h) */
static volatile uint32_t host_pressed, host_notes;   /* edges since the last take */
static volatile int32_t host_enc[7];
static uint32_t fm1_input_edges(void *released)
{
    (void)released;
    return __atomic_exchange_n(&host_pressed, 0u, __ATOMIC_SEQ_CST);
}
static uint32_t fm1_input_note_edges(void) { return __atomic_exchange_n(&host_notes, 0u, __ATOMIC_SEQ_CST); }
static int32_t fm1_enc_take(uint32_t e) { return __atomic_exchange_n(&host_enc[e % 7u], 0, __ATOMIC_SEQ_CST); }
/* the keys while a frame draws (firmware/src/cr_ui.c cr_ui_pump, cr_out.c cr_klat): the tick that saw key k go down
 * (hal/fm1_input.h note_t0), the edges not taken yet, and the pump's entry: EMU_PUMP=0: no pump; emu.c's mark (the
 * longest stretch with no key read, EMU_UI_GAP=1), then the CPU lock as emu_fw_idle takes it. A strip composed and
 * sent (cr_draw.c CR_HOOK_STRIP): the emulator's clock (emu.c: a strip's time passes, EMU_STRIP_US) */
static uint32_t emu_key_t0[27];
#define CU_KEY_T0(k) emu_key_t0[(k) % 27u]
#define CU_BTN_EDGES() host_pressed
#define CU_KEY_EDGES() host_notes
static int emu_pump_begin(void)
{
    if (emu_hal.pump_off)
        return 0;
    if (emu_hal.pump_mark)
        emu_hal.pump_mark();
    fm1_irq_off();
    return 1;
}
#define CU_PUMP_BEGIN() emu_pump_begin()
#define CU_PUMP_END() fm1_irq_on()
#define CR_HOOK_STRIP() do { if (emu_hal.strip_done) emu_hal.strip_done(); } while (0)
#endif /* EMU_SMK37 */

/* ----------------------------------------------------- display --- */
/* src/lcd.c's fill and blit on the panel's RAM (emu_hal.lcd): the panel is on and lit from power-on, as on the device
 * (no display or backlight off: the screen "off" of cr_ui.c is a black frame through lcd_fill, so the shots show
 * what the firmware wrote). The backlight's pin, PA2, is emu_hal.pa2 (emu_hooks.h): nothing here raises it, as
 * nothing in hal/fm1_lcd_hw.h does; a stand-in for HAL code that ever drives PA2 high must set it, and the emulator
 * then goes as deaf and dark as the unit */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            emu_hal.lcd[(y + j) * 240u + x + i] = s;
    emu_hal.lcd_writes++;
}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            emu_hal.lcd[(y + j) * 240u + x + i] = p[j * w + i];
    emu_hal.lcd_writes++;
}

/* ------------------------------------------- audio (for audio.c) --- */
#define FM1_AUDIO_HALF 1u
static uint32_t emu_half;                         /* the half buffer the "DMA" gives the ISR */
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return emu_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), int prio) { (void)b; (void)n; (void)isr; (void)prio; }
void isr_alnk0(void) {}

/* ------------------------------------------------ flash (storage.c's hooks) --- */
/* The FM-1's 1 MiB SPI NOR as a RAM image backed by a file (docs/SETTINGS.md): loaded at power-on, every erase /
 * program written through to the file at once, so what the firmware saves (settings, later user sounds and loops)
 * survives a quit, a crash or a kill. The NOR's rules hold: an erase sets a 4 KiB sector to 0xFF, a program can
 * only clear bits. emu_fw.c builds Felucca's storage.c (FELUCCA_FLASH 1) on these three hooks.
 *   --flash PATH   the file (default build/emu/flash.bin; headless runs default to no file: a fresh flash each run,
 *                  so the scripted checks stay deterministic)
 *   --no-flash     no file (RAM only, erased at power-on) */
#define EMU_FLASH_SIZE 0x100000u
static uint8_t emu_flash[EMU_FLASH_SIZE];
static char emu_flash_path[1024];                 /* "": RAM only */
static FILE *emu_flash_f;
static uint32_t emu_flash_writes;                 /* erases + programs (diagnostics) */
static uint8_t flash_ok;                          /* storage_hw.c's: the part answered (here: always) */

static void emu_flash_open(void)
{
    memset(emu_flash, 0xFF, sizeof emu_flash);
    flash_ok = 1;
    if (!emu_flash_path[0])
        return;
    emu_flash_f = fopen(emu_flash_path, "r+b");
    if (emu_flash_f) {
        size_t n = fread(emu_flash, 1, sizeof emu_flash, emu_flash_f);
        if (n < sizeof emu_flash)                 /* short (or new): pad the file to the full image */
            memset(emu_flash + n, 0xFF, sizeof emu_flash - n);
    } else {
        emu_flash_f = fopen(emu_flash_path, "w+b");
    }
    if (!emu_flash_f) {
        fprintf(stderr, "emu: flash file %s: cannot open (RAM only)\n", emu_flash_path);
        return;
    }
    fseek(emu_flash_f, 0, SEEK_SET);
    fwrite(emu_flash, 1, sizeof emu_flash, emu_flash_f);
    fflush(emu_flash_f);
}
static void emu_flash_sync(uint32_t off, uint32_t n)
{
    emu_flash_writes++;
    if (!emu_flash_f)
        return;
    fseek(emu_flash_f, (long)off, SEEK_SET);
    fwrite(emu_flash + off, 1, n, emu_flash_f);
    fflush(emu_flash_f);
}
#define ST_READS_COUNTED 1
static uint32_t st_reads, st_read_bytes;          /* (storage_hw.c's: every flash read, calls and bytes) */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    st_reads++;
    st_read_bytes += n;
    if (off > EMU_FLASH_SIZE || n > EMU_FLASH_SIZE - off)
        return -8;
    memcpy(dst, emu_flash + off, n);
    return 0;
}
/* the device's erase (storage_hw.c st_erase): the audio buffer zeroed and every IRQ off for the 4 KiB erase (typ.
 * ~45 ms on the FM-1's NOR, up to 400 ms): no render, the DMA loops silence. The emulator plays the same hole:
 * EMU_ERASE_MS of zero blocks, the firmware's audio ISR not run meanwhile (emu_fw_audio).
 * --erase-ms N (3y264): a slower part's erase, 1..2000 ms. Past 500 the erase is late: done, 0, and counted, as the
 * device's driver does since 3y265 (hal/fm1_flash_late.h; st_late_ops / st_late_max_us, 78 PEAK); at 2 s the device
 * resets the part and the chip, which the emulator does not model */
static uint32_t emu_erase_ms_set = 45u;
#define EMU_ERASE_MS emu_erase_ms_set
#define EMU_ERASE_LATE_MS 500u                    /* fm1_flash.h fl_erase4k_ram's normal bound */
#define EMU_PROG_US 1000u                         /* a page program's masked time in the model (docs/FX.md: ~1 ms) */
static uint32_t emu_stall_blocks, emu_stalls, emu_stall_blocks_all;
static int audio_loud(void);                      /* audio.c (included after this): the output above -60 dBFS */
static uint32_t st_holes;                         /* .. erases that cut it (audio.c's count on the device) */
/* storage_hw.c's counters (78 PEAK, the console, the GEEK OUT view): every erase (emu_stalls: those with audio to
 * stall, i.e. not the power-on's erase ahead), the automatic ones near playing, the longest page program; and the
 * model's own: the masked time in all, when the last erase started (fm1_ms) */
static uint32_t st_erases, st_near, st_prog_us, emu_masked_us, emu_erase_ms;
static uint32_t st_late_ops, st_late_max_us;      /* (storage_hw.c's: the driver's fl_late_ops / fl_late_max_us) */
static uint8_t st_auto;
static uint8_t st_ahead_ok[4];                    /* storage.c's ([3]: the power-on's erase ahead runs) */
static int cr_erase_near(void);                   /* cr_ui.c */
static int st_erase(uint32_t off)
{
    off &= ~0xFFFu;
    if (off >= EMU_FLASH_SIZE)
        return -8;
    st_erases++;
    emu_masked_us += EMU_ERASE_MS * 1000u;
    emu_erase_ms = fm1_ms;
    if (!st_ahead_ok[3]) {                        /* (power-on: the audio has not started, nothing stalls) */
        emu_stall_blocks += (EMU_ERASE_MS * 44100u / 1000u + HALF_FRAMES - 1u) / HALF_FRAMES;
        emu_stalls++;
        st_holes += (uint32_t)audio_loud();
        if (st_auto && cr_erase_near())
            st_near++;
    }
    memset(emu_flash + off, 0xFF, 0x1000u);
    emu_flash_sync(off, 0x1000u);
    if (EMU_ERASE_MS > EMU_ERASE_LATE_MS) {        /* late: done, counted (fm1_flash_late.h fl_late_ok) */
        st_late_ops++;
        if (EMU_ERASE_MS * 1000u > st_late_max_us)
            st_late_max_us = EMU_ERASE_MS * 1000u;
    }
    return 0;
}
static int emu_prog_left = -1;                    /* the host tests' power cut: programs that still reach the flash (-1:
                                                   * no cut), then every program fails (tests/cr_pool_test.c) */
static void emu_flash_fail_after(int n) { emu_prog_left = n; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off > EMU_FLASH_SIZE || n > EMU_FLASH_SIZE - off || (off & 0xFFu) + n > 256u)
        return -8;                                /* (a page program must not wrap: the device's rule) */
    if (emu_prog_left == 0)
        return -9;
    if (emu_prog_left > 0)
        emu_prog_left--;
    for (i = 0; i < n; i++)
        emu_flash[off + i] &= ((const uint8_t *)src)[i];
    emu_flash_sync(off, n);
    emu_masked_us += EMU_PROG_US;
    st_prog_us = EMU_PROG_US;
    return 0;
}
