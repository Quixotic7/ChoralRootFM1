/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Devin Acker
 * Modifications Copyright (c) 2026 Kerem Kilic (Ellic Studio)
 * License: LICENSES/BSD-3-Clause-uPD933.txt
 * Native CZ-1 tone playback. Parameter encoding: Casio MIDI specification.
 * Chip envelope rate, phase functions and logarithmic DCA law follow the
 * independently documented uPD933 model by Devin Acker (MAME, BSD-3-Clause).
 * See docs/CZ1_SYSEX.md for sources and remaining calibration limits. */
static const uint16_t CZ_AMP[128] = {
    0,4,5,5,5,6,6,7,7,8,8,9,9,10,11,12,
    12,13,14,15,16,18,19,20,22,23,25,27,29,31,33,36,
    38,41,44,47,51,54,58,63,67,72,77,83,89,96,103,110,
    118,127,136,146,157,168,180,194,208,223,239,257,275,296,317,340,
    365,392,421,451,484,520,558,598,642,689,739,793,851,914,980,1052,
    1129,1212,1300,1395,1497,1606,1724,1850,1985,2130,2286,2453,2632,2824,3031,3252,
    3490,3745,4019,4313,4628,4966,5329,5718,6136,6585,7066,7582,8136,8731,9369,10054,
    10789,11577,12423,13331,14305,15351,16473,17676,18968,20355,21842,23438,25151,26990,28962,31079
};
static __attribute__((noinline)) uint32_t cz_hw_rate(uint32_t raw, uint32_t e)
{
    uint32_t chip = (8u | (raw & 7u)) << ((raw & 127u) >> 3);
    /* uPD933 at 40 kHz; convert its three different fixed-point domains to
     * our Q24 control tick. DCO covers 128 semitones, DCW 1024, DCA 512. */
    uint32_t shift = e == 0u ? 3u : e == 1u ? 2u : 1u;
    return (uint32_t)(((uint64_t)chip * CTL * 40000u / FS) >> shift);
}
static int32_t cz_hw_target(uint32_t raw, uint32_t e)
{
    raw &= 127u;
    if (e == 0u) return (int32_t)((raw & 63u) << (raw & 64u ? 18 : 13));
    return (int32_t)(raw << 17); /* DCW: 8 units; DCA: 4 units */
}
static __attribute__((noinline)) void cz_native_defs(track_t *t, voice_t *v, cz_env_def_t d[2][3])
{
    const uint8_t *b = cz_patch[(uint32_t)(t - trk) % NTRK].raw;
    for (uint32_t l = 0; l < 2u; l++) for (uint32_t e = 0; e < 3u; e++) {
        cz_env_def_t *p = &d[l][e]; uint32_t base = CZ_ENV_BASE[l][e];
        memset(p, 0, sizeof *p); p->end = b[CZ_ENV_END[l][e]] & 7u; p->sustain = 255;
        for (uint32_t k = 0; k < 8u; k++) {
            uint32_t lev = b[base + 2u*k + 1u];
            p->rate[k] = cz_hw_rate(b[base + 2u*k], e);
            p->level[k] = cz_hw_target(lev, e);
            if ((lev & 128u) && p->sustain == 255u && k < p->end) p->sustain = (uint8_t)k;
            /* Native DCA key-follow is rate scaling; higher keys run faster.
             * Keep raw key-follow and velocity bytes intact for recalibration. */
            if (e == 2u) {
                uint32_t kf = b[16u + l*57u] & 15u;
                uint32_t note = v->note > 36u ? v->note - 36u : 0u;
                p->rate[k] += (uint32_t)(((uint64_t)p->rate[k] * kf * note) / 96u);
            }
        }
    }
}
static __attribute__((noinline)) int32_t cz_native_amp(int32_t level, uint32_t atten, uint32_t sense, uint32_t vel)
{
    /* Interpolate the chip's logarithmic amplitude, not a linear percentage. */
    uint32_t x = (uint32_t)clamp(level, 0, 127<<17), i = x >> 17, f = x & 0x1FFFFu;
    int32_t a = CZ_AMP[i];
    if (i < 127u) a += (int32_t)((uint32_t)(CZ_AMP[i+1u]-CZ_AMP[i]) * f >> 17);
    a = a * (int32_t)(15u-atten) / 15;
    return a * (int32_t)(127u*15u - (127u-vel)*sense) / (127*15);
}
/* Block-prepared slopes of the chip's 11-bit phase functions. */
typedef struct { uint32_t wave[2], window, dcw, pivot, k0, k1; } cz_hw_pd_t;
static __attribute__((noinline)) void cz_native_pd(cz_hw_pd_t *p, uint32_t word, uint32_t dcw)
{
    p->wave[0] = (word >> 13) & 7u;
    p->wave[1] = word & 512u ? (word >> 10) & 7u : p->wave[0];
    p->window = (word >> 6) & 7u; p->dcw = dcw;
    p->pivot = 1024u - dcw;
    p->k0 = (1024u << 16) / p->pivot;
    p->k1 = (1024u << 16) / (2048u - p->pivot);
}
static __attribute__((noinline)) int32_t cz_native_wave(const cz_hw_pd_t *b, uint32_t ph, uint32_t toggle)
{
    uint32_t pos = ph >> 21, pivot = b->pivot, phase = 0, window = 0;
    switch (b->wave[toggle & 1u]) {
    case 0: phase = pos < pivot ? pos*b->k0 >> 16 : 1024u + ((pos-pivot)*b->k1 >> 16); break;
    case 1: phase = (pos & 1023u) < pivot ? ((pos & 1023u)*b->k0 >> 16) : 1023u; phase |= pos & 1024u; break;
    case 2: phase = pos < pivot*2u ? pos*b->k0 >> 16 : 2047u; break;
    case 3: return 0;                                    /* undocumented silent wave */
    case 4: phase = pos < pivot ? pos*b->k0 >> 15 : (pos-pivot)*b->k1 >> 15; break;
    case 5: phase = pos < 1024u ? pos : pos < pivot+1024u ? 1024u+((pos&1023u)*b->k0 >> 16) : 2047u; break;
    case 6: phase = pos + ((pos*b->dcw) >> 6); break;
    default: phase = (pos&1023u) < pivot ? (pos&1023u)*b->k0 >> 16 : 2047u; break;
    }
    phase &= 2047u;
    switch (b->window) {
    case 0: break;
    case 1: window = pos; break;
    case 2: window = (pos&1023u)*2u; if (pos < 1024u) window ^= 2046u; break;
    case 3: if (pos >= 1024u) window = (pos&1023u)*2u; break;
    case 4: window = pos < 1024u ? pos*2u : 2047u; break;
    default: window = (1023u ^ (pos&1023u))*2u; break;
    }
    uint32_t cp = (phase << 5) + (phase >> 6);
    int32_t carrier = (pd_cos(cp) + 32767) >> 1;
    return ((carrier * (int32_t)(2048u-window)) >> 10) - 32767;
}
static __attribute__((noinline)) int32_t cz_native_vibrato(cz_voice_t *c, const uint8_t *b)
{
    uint32_t delay = (uint32_t)b[6] | (uint32_t)b[7] << 8, inc = (uint32_t)b[9] | (uint32_t)b[10] << 8;
    /* Approximate control clock; needs verification against a CZ-1. */
    c->vib_ticks++;
    if (c->vib_ticks < delay * FS / (200u*CTL)) return 0;
    c->vib_phase += inc * (uint32_t)((uint64_t)CTL*200u*65536u/FS);
    int32_t x = (int32_t)(c->vib_phase >> 16), wave;
    if (b[4] & 32u) wave = 32767-x;
    else if (b[4] & 8u) wave = x < 32768 ? x*2-32768 : 98303-x*2;
    else if (b[4] & 4u) wave = x-32768;
    else wave = x < 32768 ? -32768 : 32767;
    uint32_t depth = (uint32_t)b[12] | (uint32_t)b[13] << 8;
    /* 1/16-semitone pitch, toward zero: DEPTH 0 is machine depth 1 (Casio p. 85), which must not bend
     * the negative half of the wave down a step */
    return (wave * (int32_t)depth) / (1 << 19);
}
static __attribute__((noinline)) void cz_native_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const uint8_t *b = cz_patch[(uint32_t)(t-trk)%NTRK].raw;
    cz_voice_t *c = cz_voice(t, v); cz_env_def_t defs[2][3]; cz_hw_pd_t pd[2];
    uint32_t inc[2], ph[2] = {v->ph[0],v->ph[1]}, tg[2] = {(uint32_t)v->s[0],(uint32_t)v->s[1]};
    int32_t amp[2][2], dc[2] = {v->s[2],v->s[3]}, step[2];
    uint32_t ls = b[0]&3u, first = ls == 1u ? 1u : 0u, last = ls >= 2u ? 1u : first;
    int32_t vib = cz_native_vibrato(c,b), oct = (b[0]>>2) == 1u ? 192 : (b[0]>>2) == 2u ? -192 : 0;
    cz_native_defs(t,v,defs);
    for (uint32_t l=0;l<2u;l++) {
        uint32_t src = ls == 2u ? 0u : l, off = src*57u;
        if (src != l) memcpy(defs[l],defs[src],sizeof defs[l]);
        uint32_t av = 15u-(b[20u+off]>>4), al = b[16u+off]>>4;
        amp[l][0] = cz_native_amp(c->eg[l][2].level,al,av,v->vel);
        int32_t pitch = cz_env_tick(&c->eg[l][0],&defs[l][0],v->gate);
        int32_t depth = cz_env_tick(&c->eg[l][1],&defs[l][1],v->gate);
        cz_env_tick(&c->eg[l][2],&defs[l][2],v->gate);
        amp[l][1] = cz_native_amp(c->eg[l][2].level,al,av,v->vel);
        step[l] = (amp[l][1]-amp[l][0])*256/(int32_t)n;
        uint32_t pv = 15u-(b[54u+off]>>4), wv = 15u-(b[37u+off]>>4);
        pitch = mulq16(pitch, (127u*15u-(127u-v->vel)*pv)*65536u/(127u*15u));
        depth = mulq16(depth, (127u*15u-(127u-v->vel)*wv)*65536u/(127u*15u));
        uint32_t kf = b[18u+off], kfnote = v->note > 36u ? v->note-36u : 0u;
        depth = depth*96/(int32_t)(96u+kf*kfnote);
        int32_t det = l ? ((int32_t)b[3]*16+(b[2]>>2)*16/64)*(b[1] ? -1 : 1) : 0;
        int32_t nt = clamp(m->pitch16+oct+vib+(pitch>>13)+det,0,2047);
        inc[l] = pitch_inc((uint32_t)nt);
        inc[l] += (uint32_t)((int32_t)(inc[l]>>12)*m->fine);
        uint32_t word = (uint32_t)b[14u+off]<<8 | b[15u+off];
        uint32_t dep = (uint32_t)clamp((depth>>14)+((m->cutoff+m->shape-(64<<8))>>5),0,1023);
        cz_native_pd(&pd[l],word,dep);
    }
    v->s[4] = c->eg[first][2].level >> 9;
    uint32_t modulation = (b[15]>>3)&7u;
    for (uint32_t i=0;i<n;i++) {
        int32_t line[2] = {0,0};
        for (uint32_t l=first;l<=last;l++) {
            uint32_t old=ph[l], delta=inc[l];
            if (l && (modulation==3u)) {
                c->noise ^= c->noise<<13; c->noise ^= c->noise>>17; c->noise ^= c->noise<<5;
                if (c->noise&1u) delta = delta > 0x1428A2F9u ? 0x7FFFFFFFu : (delta>>8)*1625u;
            }
            int32_t raw=cz_native_wave(&pd[l],ph[l],tg[l]); ph[l]+=delta;
            if (ph[l]<old) tg[l]^=1u;
            dc[l]+=raw-(dc[l]>>10); raw=(raw-(dc[l]>>10))>>1;
            line[l]=mulq15(raw,amp[l][0]+((step[l]*(int32_t)i)>>8));
        }
        int32_t sample=first==last ? line[first] : (modulation==4u) ? mulq15(line[0],line[1])*2 : (line[0]+line[1])>>1;
        out[i]+=voice_amp(sample,m,i)*4;
    }
    v->ph[0]=ph[0];v->ph[1]=ph[1];v->s[0]=(int32_t)tg[0];v->s[1]=(int32_t)tg[1];v->s[2]=dc[0];v->s[3]=dc[1];
}
