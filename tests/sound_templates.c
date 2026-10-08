/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The default user sound records and the .syx fixtures that web/fm1sounds.js and tools/fm1_install.py embed
 * (docs/SOUNDS.md, ".syx export and import"): built on the emulator's firmware, as tests/cr_backup_test.c is.
 *
 *   cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/sound_templates tests/sound_templates.c -lm
 *   ./build/host/sound_templates                 prints the JSON below
 *   ./build/host/sound_templates --check FILE..  exits 1 unless every FILE contains each template's base64 string
 *                                                and the two factory-table strings below, verbatim, and
 *                                                every selectable engine's name in quotes ("FM TONE": the
 *                                                clients' ENGINE_NAMES)
 *                                                (tests/run_cr_tests.sh runs it on the two clients)
 *
 * JSON: { "templates": { "fm6": <base64 up_rec_t>, "cz": <base64 up_rec_t>, "quad": <base64 up_rec_t> },
 *         "fixtures": { "fm6_blob": <base64 128>, "fm6_vced": <base64 155>, "fm6_fn": <base64 8: the packed
 *                       function defaults, blob bytes 114..121>, "cz_tone": <base64 144>, "quad_blob": <base64 80:
 *                       QUAD's first factory preset, EP> } }
 * (QUAD has no .syx: its template and blob are the test fixtures of the "quad" patch kind, not pinned in the clients) 
 * A template is a valid record (up_valid) of the engine with every parameter at its default (param_desc_of), np =
 * P_COUNT, ver UP_VER, the name "SYX IMPORT" (the importer replaces it with the voice's name), no pattern. The
 * firmware maps a record's values by count, so a template stays valid when P_COUNT grows (upreset.c). The fixtures:
 * factory voice F1's blob (fm6_blob_get of a part loaded with it) and the same voice as the 155-byte VCED
 * (fm6_blob_read), and Casio's first tone (CZ_FACTORY[0]).
 *
 * "factory": {"<engine>": [name, ..]}: ENGINES[e]->presets[k].name for every engine with eng_ok, and
 * "factory_first": {"<engine>": n}: the first of them in the engine's pool (cr_bank.c pool_f0: the CZ-1's preset 0,
 * INIT TONE, is the pool's INIT). Both printed compact on one line each; a client embeds the two strings verbatim (a
 * JSON object of strings and numbers is a JS and a Python literal) and names a binding from them (docs/SOUNDS.md). */
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

/* the factory preset names of every engine with eng_ok, and each pool's first factory preset (cr_bank.c pool_f0) */
static void factory_json(char *fac, size_t nf, char *first, size_t n1)
{
    size_t o = 0, o1 = 0;
    uint32_t e, k;
    const char *c;
    o += (size_t)snprintf(fac + o, nf - o, "{");
    o1 += (size_t)snprintf(first + o1, n1 - o1, "{");
    for (e = 0; e < NENGINES; e++) {
        if (!eng_ok(e))
            continue;
        o += (size_t)snprintf(fac + o, nf - o, "%s\"%u\":[", o > 1 ? "," : "", (unsigned)e);
        o1 += (size_t)snprintf(first + o1, n1 - o1, "%s\"%u\":%u", o1 > 1 ? "," : "", (unsigned)e, (unsigned)pool_f0(e));
        for (k = 0; k < ENGINES[e]->npresets; k++) {
            o += (size_t)snprintf(fac + o, nf - o, "%s\"", k ? "," : "");
            for (c = ENGINES[e]->presets[k].name; *c && o + 4 < nf; c++) {
                if (*c == '"' || *c == '\\')
                    fac[o++] = '\\';
                fac[o++] = *c;
            }
            o += (size_t)snprintf(fac + o, nf - o, "\"");
        }
        o += (size_t)snprintf(fac + o, nf - o, "]");
        if (o + 64 > nf) {
            fprintf(stderr, "the factory table does not fit\n");
            exit(2);
        }
    }
    snprintf(fac + o, nf - o, "}");
    snprintf(first + o1, n1 - o1, "}");
}

int main(int argc, char **argv)
{
    static char t_fm6[400], t_cz[400], t_quad[400], f_blob[200], f_vced[240], f_fn[20], f_tone[200], f_quad[200],
        fac[16384], first[512];
    up_rec_t r;
    uint8_t blob[FM6_BLOB], v[FP_SIZE + 1u], fn[FM6_NFN], qb[QUAD_BLOB];
    up_boot();                                   /* (as tests/cr_backup_test.c power_on: the mirrors, no UI) */
    cr_settings_boot();
    template_of(ENGI_FM6, &r);
    b64((const uint8_t *)&r, sizeof r, t_fm6);
    template_of(ENGI_CZ, &r);
    b64((const uint8_t *)&r, sizeof r, t_cz);
    template_of(ENGI_QUAD, &r);
    b64((const uint8_t *)&r, sizeof r, t_quad);
    trk[1].eng_req = ENGI_QUAD;                      /* (QUAD's EP on part 1: its blob) */
    quad_blob_preset(&trk[1], 0);
    quad_blob_get(&trk[1], qb);
    if (!quad_blob_ok(qb)) {
        fprintf(stderr, "QUAD's EP blob is not valid\n");
        return 2;
    }
    b64(qb, QUAD_BLOB, f_quad);
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
    factory_json(fac, sizeof fac, first, sizeof first);
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
            {
                uint32_t e;
                char q[16];
                for (e = 0; e < NENGINES; e++) {
                    if (!eng_ok(e))
                        continue;
                    snprintf(q, sizeof q, "\"%s\"", ENGINES[e]->name);
                    if (!strstr(s, q)) {
                        fprintf(stderr, "%s: engine %u's name %s is not in its engine names\n", argv[i], (unsigned)e, q);
                        bad = 1;
                    }
                }
            }
            if (!strstr(s, fac) || !strstr(s, first)) {
                fprintf(stderr, "%s: the factory preset table differs from the firmware's (run tests/sound_templates and paste \"factory\" / \"factory_first\")\n", argv[i]);
                bad = 1;
            }
            free(s);
        }
        printf("sound_templates: %s\n", bad ? "FAIL" : "ok");
        return bad;
    }
    printf("{\n \"templates\": {\"fm6\": \"%s\", \"cz\": \"%s\", \"quad\": \"%s\"},\n \"fixtures\": {\"fm6_blob\": \"%s\", \"fm6_vced\": \"%s\", \"fm6_fn\": \"%s\", \"cz_tone\": \"%s\", \"quad_blob\": \"%s\"},\n"
           " \"p_count\": %u, \"fm6_name\": \"%.10s\", \"cz_name\": \"%.16s\",\n \"factory\": %s,\n \"factory_first\": %s\n}\n",
           t_fm6, t_cz, t_quad, f_blob, f_vced, f_fn, f_tone, f_quad, (unsigned)P_COUNT, (const char *)(v + 145),
           (const char *)(CZ_FACTORY[0] + 128), fac, first);
    return 0;
}
