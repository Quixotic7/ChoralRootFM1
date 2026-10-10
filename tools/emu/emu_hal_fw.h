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
#define FM1_NCOL 11u
#define FM1_KEYMAP emu_keymap
#define fm1_led (emu_hal.led)
#define fm1_led_dim (emu_hal.led_dim)
static volatile uint32_t host_pressed, host_notes;   /* edges since the last take */
static volatile int32_t host_enc[7];
static uint32_t fm1_input_edges(void *released)
{
    (void)released;
    return __atomic_exchange_n(&host_pressed, 0u, __ATOMIC_SEQ_CST);
}
static uint32_t fm1_input_note_edges(void) { return __atomic_exchange_n(&host_notes, 0u, __ATOMIC_SEQ_CST); }
static int32_t fm1_enc_take(uint32_t e) { return __atomic_exchange_n(&host_enc[e % 7u], 0, __ATOMIC_SEQ_CST); }

/* ----------------------------------------------------- display --- */
static void lcd_power(int on);
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            emu_hal.lcd[(y + j) * 240u + x + i] = s;
    emu_hal.lcd_writes++;
    if (emu_hal.lcd_dark)                         /* (lcd.c: main.c's one-shots light it) */
        lcd_power(1);
}
static void lcd_sync(void) {}
/* the backlight (hal/fm1_lcd_hw.h) and the screen off / on (src/lcd.c lcd_power: DISPOFF / DISPON + the backlight):
 * emu_hal.lcd_dark, the panel and the shots show black while it is set */
static void fm1_lcd_backlight(int on) { emu_hal.lcd_dark = (uint8_t)!on; }
static void lcd_power(int on)
{
    if ((!on) == emu_hal.lcd_dark)
        return;
    emu_hal.lcd_power_cmds++;                     /* (DISPON / DISPOFF: the panel's RAM is kept) */
    fm1_lcd_backlight(on);
}
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
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > EMU_FLASH_SIZE || n > EMU_FLASH_SIZE - off)
        return -8;
    memcpy(dst, emu_flash + off, n);
    return 0;
}
/* the device's erase (storage_hw.c st_erase): the audio buffer zeroed and every IRQ off for the 4 KiB erase (typ.
 * ~45 ms on the FM-1's NOR, up to 400 ms): no render, the DMA loops silence. The emulator plays the same hole:
 * EMU_ERASE_MS of zero blocks, the firmware's audio ISR not run meanwhile (emu_fw_audio) */
#define EMU_ERASE_MS 45u
static uint32_t emu_stall_blocks, emu_stalls, emu_stall_blocks_all;
#define st_erases emu_stalls                      /* (storage_hw.c's count: the console, the GEEK OUT view) */
static int st_erase(uint32_t off)
{
    off &= ~0xFFFu;
    if (off >= EMU_FLASH_SIZE)
        return -8;
    emu_stall_blocks += (EMU_ERASE_MS * 44100u / 1000u + HALF_FRAMES - 1u) / HALF_FRAMES;
    emu_stalls++;
    memset(emu_flash + off, 0xFF, 0x1000u);
    emu_flash_sync(off, 0x1000u);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off > EMU_FLASH_SIZE || n > EMU_FLASH_SIZE - off || (off & 0xFFu) + n > 256u)
        return -8;                                /* (a page program must not wrap: the device's rule) */
    for (i = 0; i < n; i++)
        emu_flash[off + i] &= ((const uint8_t *)src)[i];
    emu_flash_sync(off, n);
    return 0;
}
