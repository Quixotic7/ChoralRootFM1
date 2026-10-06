/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define __attribute__(x)
#define NENGINES 9u
#define UP_SLOTS 32u
static uint8_t fx_lowcut;
static void fm1_led_key(unsigned k, int on) { (void)k; (void)on; }
static int fm1_enc_take(unsigned k) { (void)k; return 0; }
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
static void settings_save(void) {}
#if __has_include("../firmware/src/favorites.c")
#include "../firmware/src/favorites.c"
#endif
#include "../firmware/src/settings_persist.c"
int main(void)
{
    persist_t original = {0}, p;
    original.magic = PERSIST_MAGIC;
    original.palette = 4; original.bold = original.lowcut = original.zoom = 1;   /* old id 4: MONO */
    original.panel = PANEL_DEFAULT; original.panel.enc[0] = 3;
    original.favorites.factory[8][0] = 1;
    original.favorites.user = 1u << 31; original.favorites.filter = 1;
    p = original;
    assert(settings_import(&p, sizeof p) == 1); settings_init();
    assert(settings.palette == UI_MONO_INDEX && fx_lowcut && settings.zoom && panel.enc[0] == 3);
    assert(p.palette == palette_to_stored(UI_MONO_INDEX));        /* migrated in place */
    {   /* every old id maps to a palette; tagged ids round trip; anything else is MONO */
        persist_t q = original;
        for (uint32_t i = 0; i < 20u; i++) assert(palette_from_stored(i) < NPALETTES);
        assert(palette_from_stored(13) == 6u && palette_from_stored(11) == 7u && palette_from_stored(19) == 7u);
        for (uint32_t i = 0; i < NPALETTES; i++) assert(palette_from_stored(palette_to_stored(i)) == i);
        assert(palette_from_stored(40) == UI_MONO_INDEX && !palette_stored_ok(40) && palette_stored_ok(3));
        q.palette = palette_to_stored(5);
        assert(settings_import(&q, sizeof q) == 1 && settings.palette == 5u);
        settings.magic = SETTINGS_MAGIC_OLD; settings.palette = 13; settings_init();   /* retained SET3 */
        assert(settings.magic == SETTINGS_MAGIC && settings.palette == 6u);
        p = original; assert(settings_import(&p, sizeof p) == 1); settings_init();
    }
#ifdef FELUCCA_FAVORITES
    assert(favorite_has(8, 0) && favorite_has(NENGINES, 31) && favorites.filter);
#endif
    settings.lowcut = 0;
    settings_export(&p);
    assert(!p.lowcut && p.bold == 1 && p.favorites.user == (1u << 31));   /* bold: kept as saved */
    assert(p.favorites.factory[8][0] == 1 && p.favorites.filter == 1);
    assert(p.panel.enc[0] == 3); /* saving one feature preserves the other */
    p = original; p.magic = 0x50455233u;
    assert(settings_import(&p, sizeof p - sizeof p.cr - sizeof p.favorites) == 2);
    assert(p.bold == 1 && !p.favorites.user && !p.favorites.filter);
    settings_export(&p); assert(p.bold == 1); /* favorites-only preserves PER3 font */
    p = original; p.magic = 0x50455232u;
    assert(settings_import(&p, sizeof p - sizeof p.cr - sizeof p.favorites - sizeof p.bold) == 2);
    assert(!p.bold && !p.favorites.user && settings.zoom && panel.enc[0] == 3);
    p = original; p.magic = 0x50455231u;
    memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
    assert(settings_import(&p, 8 + sizeof(panel_t)) == 2);
    assert(!p.bold && !p.zoom && !p.lowcut && !p.favorites.user && panel.magic == PANEL_MAGIC);
    {   /* HOLD: in the retired bold field; older records (bold 0 / 1) are 0.4 s, and stay as saved */
        p = original; p.bold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF && HOLD_MS[settings_hold] == 400u);
        p = original;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        settings_hold = 3;
        settings_export(&p);
        assert(hold_stored_ok(p.bold) && p.bold != 1u);
        settings_hold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == 3u && HOLD_MS[settings_hold] == 600u);
        settings_hold = HOLD_DEF;
        settings_export(&p);
        assert(p.bold == 0u && settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        assert(!hold_stored_ok(2u) && !hold_stored_ok(HOLD_TAG + 4u) && hold_stored_ok(HOLD_TAG | 2u));
        p = original; p.magic = 0x50455231u;
        memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
        settings_hold = 2;
        assert(settings_import(&p, 8 + sizeof(panel_t)) == 2 && settings_hold == HOLD_DEF);
    }
    {   /* LEDS: in the retired zoom field; older records (zoom 0 / 1) are DIM, and stay as saved; idempotent */
        persist_t q;
        p = original;                                   /* zoom 1: an older record's large readout */
        assert(settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_DIM);
        q = p; settings_export(&q); assert(q.zoom == 1u);                  /* DIM: kept as saved */
        settings_leds = LEDS_INV;
        settings_export(&p);
        assert(p.zoom == (LEDS_TAG | LEDS_INV) && leds_stored_ok(p.zoom));
        q = p; settings_export(&q); assert(!memcmp(&q, &p, sizeof q));   /* a second save: the same record */
        settings_leds = LEDS_DIM;
        assert(settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_INV);
        q = p; assert(settings_import(&q, sizeof q) == 1 && !memcmp(&q, &p, sizeof q));   /* import twice: the same */
        settings_leds = LEDS_DIM;
        settings_export(&p);
        assert(p.zoom == 0u && settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_DIM);
        assert(leds_stored_ok(0u) && leds_stored_ok(1u) && !leds_stored_ok(2u) && !leds_stored_ok(LEDS_TAG | 2u) &&
               !leds_stored_ok(LEDS_TAG + 4u) && leds_from_stored(LEDS_TAG | 3u) == LEDS_DIM && leds_from_stored(7u) == LEDS_DIM);
        p = original; p.magic = 0x50455231u;           /* PER1: no zoom field, DIM */
        memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
        settings_leds = LEDS_INV;
        assert(settings_import(&p, 8 + sizeof(panel_t)) == 2 && settings_leds == LEDS_DIM);
    }
    assert(settings_import(&p, 3) == 0 && settings_import(&p, -1) == 0);
    assert(settings_import(&p, sizeof p - 1) == 0);
    p = original; p.magic = 0x50455234u; p.cr.magic = 0x12345678u;   /* PER4: favorites kept, the ChoralRoot block zeroed */
    assert(settings_import(&p, sizeof p - sizeof p.cr) == 2 && p.favorites.user == (1u << 31) && !p.cr.magic);
    puts("Settings: PER1/PER2/PER3/PER4 migration, palette ids, calibration, HOLD, LEDS and independent feature preservation passed.");
}
