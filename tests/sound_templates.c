/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The default user sound records and the .syx fixtures that web/fm1sounds.js and tools/fm1_install.py embed
 * (docs/SOUNDS.md, ".syx export and import"): built on the emulator's firmware, as tests/cr_backup_test.c is.
 *
 *   cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/sound_templates tests/sound_templates.c -lm
 *   ./build/host/sound_templates                 prints the JSON below
 *   ./build/host/sound_templates --check FILE..  exits 1 unless every FILE contains each template's base64 string
 *                                                (tests/run_cr_tests.sh runs it on the two clients)
 *
 * JSON: { "templates": { "fm6": <base64 up_rec_t>, "cz": <base64 up_rec_t> },
 *         "fixtures": { "fm6_blob": <base64 128>, "fm6_vced": <base64 155>, "fm6_fn": <base64 8: the packed
 *                       function defaults, blob bytes 114..121>, "cz_tone": <base64 144> } }
 * A template is a valid record (up_valid) of the engine with every parameter at its default (param_desc_of), np =
 * P_COUNT, ver UP_VER, the name "SYX IMPORT" (the importer replaces it with the voice's name), no pattern. The
 * firmware maps a record's values by count, so a template stays valid when P_COUNT grows (upreset.c). The fixtures:
 * factory voice F1's blob (fm6_blob_get of a part loaded with it) and the same voice as the 155-byte VCED
 * (fm6_blob_read), and Casio's first tone (CZ_FACTORY[0]). */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {{0}};
#define CRB_SEND(p, n) ((void)(p), (void)(n))
#include "../tools/emu/emu_firmware.h"

static void b64(const uint8_t *p, uint32_t n, char *out)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t i, o = 0;
    for (i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0u) | (i + 2 < n ? p[i + 2] : 0u);
        out[o++] = T[v >> 18 & 63];
        out[o++] = T[v >> 12 & 63];
        out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
}

static void template_of(uint32_t engine, up_rec_t *r)
{
    uint32_t i;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->ver = UP_VER;
    r->engine = (uint8_t)engine;
    r->np = P_COUNT;
    memcpy(r->name, "SYX IMPORT", 10);
    for (i = 0; i < P_COUNT; i++)
        up_set_value(r, i, param_desc_of(engine, i)->def);
    if (!up_valid(r)) {
        fprintf(stderr, "template for engine %u is not a valid record\n", (unsigned)engine);
        exit(2);
    }
}

int main(int argc, char **argv)
{
    static char t_fm6[400], t_cz[400], f_blob[200], f_vced[240], f_fn[20], f_tone[200];
    up_rec_t r;
    uint8_t blob[FM6_BLOB], v[FP_SIZE + 1u], fn[FM6_NFN];
    up_boot();                                   /* (as tests/cr_backup_test.c power_on: the mirrors, no UI) */
    cr_settings_boot();
    template_of(ENGI_FM6, &r);
    b64((const uint8_t *)&r, sizeof r, t_fm6);
    template_of(ENGI_CZ, &r);
    b64((const uint8_t *)&r, sizeof r, t_cz);
    /* the fixtures: factory voice F1 on part 0 */
    trk[0].eng_req = ENGI_FM6;
    memcpy(fm6_fn[0], FM6_FNDEF, FM6_NFN);           /* the part's function settings at Dexed's defaults (fm6_init) */
    fm6_load_slot(0, 0);
    fm6_blob_get(&trk[0], blob);
    if (!fm6_blob_read(blob, v, fn)) {
        fprintf(stderr, "F1's blob does not read back\n");
        return 2;
    }
    b64(blob, FM6_BLOB, f_blob);
    b64(v, FP_SIZE, f_vced);
    b64(blob + 114u, 8u, f_fn);
    b64(CZ_FACTORY[0], CZ_BYTES, f_tone);
    if (argc > 2 && !strcmp(argv[1], "--check")) {
        int i, bad = 0;
        for (i = 2; i < argc; i++) {
            FILE *f = fopen(argv[i], "rb");
            char *s;
            long n;
            if (!f) {
                fprintf(stderr, "%s: cannot open\n", argv[i]);
                return 1;
            }
            fseek(f, 0, SEEK_END);
            n = ftell(f);
            fseek(f, 0, SEEK_SET);
            s = calloc((size_t)n + 1u, 1);
            fread(s, 1, (size_t)n, f);
            fclose(f);
            if (!strstr(s, t_fm6) || !strstr(s, t_cz)) {
                fprintf(stderr, "%s: the sound templates differ from the firmware's defaults (run tests/sound_templates and paste)\n", argv[i]);
                bad = 1;
            }
            free(s);
        }
        printf("sound_templates: %s\n", bad ? "FAIL" : "ok");
        return bad;
    }
    printf("{\n \"templates\": {\"fm6\": \"%s\", \"cz\": \"%s\"},\n \"fixtures\": {\"fm6_blob\": \"%s\", \"fm6_vced\": \"%s\", \"fm6_fn\": \"%s\", \"cz_tone\": \"%s\"},\n"
           " \"p_count\": %u, \"fm6_name\": \"%.10s\", \"cz_name\": \"%.16s\"\n}\n",
           t_fm6, t_cz, f_blob, f_vced, f_fn, f_tone, (unsigned)P_COUNT, (const char *)(v + 145), (const char *)(CZ_FACTORY[0] + 128));
    return 0;
}
