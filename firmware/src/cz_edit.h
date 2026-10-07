/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MELODEE_CZ_EDIT_H
#define MELODEE_CZ_EDIT_H
/* On-device editing of a native CZ-1 tone (the SC_CZ1 pages). The track keeps
 * Casio's 144 bytes; a page shows them as panel values (cz_legacy.h's flat
 * layout) and an edit writes back only the bytes whose encoding changed, so an
 * imported tone keeps its exact rates and levels until that value is turned.
 * Function settings (bend, wheel, aftertouch, portamento, glide, chorus) are
 * not part of a Casio tone and have no page here. */
#include "cz_legacy.h"
#define CZ_NAME_LEN 16u
static const char *const CZ_LINES[]={"LINE1","LINE2","1+2","1+1"};
static const char *const CZ_MODES[]={"OFF","RING","NOISE"};
static const char *const CZ_VWAVES[]={"TRI","SAW UP","SAW DN","SQR"};
static const char *const CZ_STAGES[]={"1","2","3","4","5","6","7","8","-"};
static const char *const CZ_CARRIERS[]={"SAW","SQUARE","PULSE","NULL","DBL SINE","SAW-PULS","MULTISIN","PULSE2"};
static const char *const CZ_CARRIERS2[]={"OFF","SAW","SQUARE","PULSE","NULL","DBL SINE","SAW-PULS","MULTISIN","PULSE2"};
static const char *const CZ_WINDOWS[]={"OFF","SAW DN","TRIANGLE","TRAPEZ","HALF SAW","2 SAW UP","2 SAW 6","2 SAW 7"};
#define CZ_ED_LINE {"WAVE",F_ENUM,0,7,0,CZ_CARRIERS,0},{"WAVE2",F_ENUM,0,8,0,CZ_CARRIERS2,0}, \
    {"W.KEY",F_INT,0,9,0,0,0},{"A.KEY",F_INT,0,9,0,0,0},{"LEVEL",F_INT,1,15,15,0,0}, \
    {"V.PIT",F_INT,0,15,0,0,0},{"V.WAV",F_INT,0,15,0,0,0},{"V.AMP",F_INT,0,15,0,0,0}
#define CZ_ED_ENV {"R1",F_INT,0,99,70,0,0},{"R2",F_INT,0,99,70,0,0},{"R3",F_INT,0,99,70,0,0}, \
    {"R4",F_INT,0,99,70,0,0},{"R5",F_INT,0,99,70,0,0},{"R6",F_INT,0,99,70,0,0},{"R7",F_INT,0,99,70,0,0}, \
    {"R8",F_INT,0,99,70,0,0},{"L1",F_INT,0,99,0,0,0},{"L2",F_INT,0,99,0,0,0},{"L3",F_INT,0,99,0,0,0}, \
    {"L4",F_INT,0,99,0,0,0},{"L5",F_INT,0,99,0,0,0},{"L6",F_INT,0,99,0,0,0},{"L7",F_INT,0,99,0,0,0}, \
    {"L8",F_INT,0,99,0,0,0},{"SUS",F_ENUM,0,8,1,CZ_STAGES,0},{"END",F_ENUM,0,7,2,CZ_STAGES,0}
static const param_desc_t CZ_PD[LCZ_NP]={
    [LCZ_LINE]={"LINE",F_ENUM,0,3,2,CZ_LINES,0},[LCZ_MOD]={"MOD",F_ENUM,0,2,0,CZ_MODES,0},
    [LCZ_OCT]={"OCT",F_INT,0,2,1,0,0},[LCZ_SIGN]={"SIGN",F_ONOFF,0,1,0,0,0},
    [LCZ_DOCT]={"OCT",F_INT,0,3,0,0,0},[LCZ_NOTE]={"NOTE",F_INT,0,11,0,0,0},[LCZ_FINE]={"FINE",F_INT,0,60,0,0,0},
    [LCZ_VWAVE]={"WAVE",F_ENUM,0,3,0,CZ_VWAVES,0},[LCZ_VRATE]={"RATE",F_INT,0,99,50,0,0},
    [LCZ_VDEP]={"DEPTH",F_INT,0,99,0,0,0},[LCZ_VDELAY]={"DELAY",F_INT,0,99,0,0,0},
    [LCZ_GLOBALS]=CZ_ED_LINE,CZ_ED_LINE,CZ_ED_ENV,CZ_ED_ENV,CZ_ED_ENV,CZ_ED_ENV,CZ_ED_ENV,CZ_ED_ENV,
    [LCZ_WIN(0)]={"LINE1",F_ENUM,0,7,0,CZ_WINDOWS,0},[LCZ_WIN(1)]={"LINE2",F_ENUM,0,7,0,CZ_WINDOWS,0},
};
/* (ChoralRoot: Melodee's CZ TOOLS actions are not ported: its editor pages hold values, docs/CZ1.md) */

/* Casio bytes -> panel values (the inverse of lcz_sx_encode), clamped to the
 * panel's ranges. SUS follows the first marked point before END, as the
 * renderer does; END's own level shows 0 (the CZ ignores it). */
static __attribute__((noinline)) void cz_ed_decode(uint8_t *p,const uint8_t *d)
{
    uint32_t line=d[0]&3u,oct=(d[0]>>2)&3u,fine=d[2]>>2,mb=(d[15]>>3)&7u;
    memset(p,0,LCZ_PACKED);
    p[LCZ_LINE]=(uint8_t)(line==2u?3u:line==3u?2u:line);p[LCZ_OCT]=(uint8_t)(oct==2u?0u:oct==1u?2u:1u);
    p[LCZ_SIGN]=d[1]&1u;p[LCZ_FINE]=(uint8_t)(fine-(fine>>4));p[LCZ_DOCT]=d[3]/12u;p[LCZ_NOTE]=d[3]%12u;
    p[LCZ_VWAVE]=(uint8_t)(d[4]==4u?1u:d[4]==32u?2u:d[4]==2u?3u:0u);
    p[LCZ_VDELAY]=d[5];p[LCZ_VRATE]=d[8];p[LCZ_VDEP]=d[11];p[LCZ_MOD]=(uint8_t)(mb==3u?2u:mb==4u?1u:0u);
    for(uint32_t l=0;l<2u;l++){
        uint32_t b=LCZ_LBASE(l),o=l?71u:14u;
        p[b+LCZ_W1]=d[o]>>5;p[b+LCZ_W2]=(uint8_t)((d[o]&2u)?1u+((d[o]>>2)&7u):0u);
        p[LCZ_WIN(l)]=(uint8_t)(((d[o]&1u)<<2)|(d[o+1u]>>6));
        p[b+LCZ_KA]=d[o+2u]&15u;p[b+LCZ_KW]=d[o+4u]&15u;p[b+LCZ_LEVEL]=(uint8_t)(15u-(d[o+2u]>>4));
        for(uint32_t e=0;e<3u;e++){
            uint8_t *ep=p+LCZ_EBASE(l,e);uint32_t off=o+(e==2u?6u:e==1u?23u:40u);
            ep[17]=d[off]&7u;ep[16]=8;
            p[b+(e==0u?LCZ_VP:e==1u?LCZ_VW:LCZ_VA)]=(uint8_t)(15u-(d[off]>>4));
            for(uint32_t j=0;j<8u;j++){
                ep[j]=(uint8_t)lcz_sx_unrate(e,d[off+1u+2u*j]);ep[8u+j]=(uint8_t)lcz_sx_unlevel(e,d[off+2u+2u*j]);
                if((d[off+2u+2u*j]&128u) && ep[16]==8u && j<ep[17])ep[16]=(uint8_t)j;
            }
            ep[8u+ep[17]]=0;
        }
    }
    for(uint32_t j=0;j<LCZ_NP;j++)if(p[j]>LCZ_MAX[j])p[j]=LCZ_MAX[j];
    for(uint32_t l=0;l<2u;l++)if(!p[LCZ_LBASE(l)+LCZ_LEVEL])p[LCZ_LBASE(l)+LCZ_LEVEL]=1;
    memcpy(p+LCZ_NP,d+128,CZ_NAME_LEN);
}

/* 0: panel value id has no effect on the tone d as it is (cz_native_render reads it so): its card is drawn
 * dim (ui_draw.c). Line 2's own values sound in LINE2 and 1+2' (1+1' copies line 1), line 1's in all but
 * LINE2; DETUNE and MOD act on line 2; SIGN on a detune; vibrato WAVE RATE DELAY need a DEPTH. An envelope
 * runs its steps up to END: rates to END's (the release to 0), levels before END's, a SUS before END */
static int cz_ed_active(const uint8_t *d, uint32_t id)
{
    uint32_t ls = d[0] & 3u, line, k, e, end;
    if (id == LCZ_SIGN)
        return ls && ((d[2] >> 2) || d[3]);
    if ((id >= LCZ_DOCT && id <= LCZ_FINE) || id == LCZ_MOD)
        return ls != 0u;
    if (id == LCZ_VWAVE || id == LCZ_VRATE || id == LCZ_VDELAY)
        return d[11] != 0u;
    if (id < LCZ_GLOBALS || id >= LCZ_NP)
        return 1;
    line = id >= LCZ_WIN(0) ? id - LCZ_WIN(0) : id < LCZ_EBASE(0, 0) ? (id - LCZ_GLOBALS) / 8u : (id - LCZ_EBASE(0, 0)) / 54u;
    if (line ? ls != 1u && ls != 3u : ls == 1u)
        return 0;
    if (id < LCZ_EBASE(0, 0) || id >= LCZ_OLD_NP)
        return 1;
    k = (id - LCZ_EBASE(0, 0)) % 18u;
    e = (id - LCZ_EBASE(0, 0)) / 18u % 3u;
    end = d[(line ? 71u : 14u) + (e == 2u ? 6u : e == 1u ? 23u : 40u)] & 7u;
    return k < 8u ? k <= end : k < 16u ? k - 8u < end : k == 16u ? end != 0u : 1;
}

/* (ChoralRoot: Melodee's cz_ed_desc / cz_ed_cell, its page system's copies, are eng_cz.c's deep pages here) */

/* One panel value into track tr's tone, as raw: the new synthesis bytes (0 =
 * no change). Untouched bytes stay verbatim; a byte whose only change is its
 * flag bit (a rate's direction, a level's SUS mark) keeps its value bits.
 * The audio code reads the tone: the caller writes it with that off.
 * SUS sounds only before END: turning it up past END gives "-", down from
 * "-" gives the point before END; a new END drops a SUS at or after it. */
static int cz_ed_put(uint32_t tr,uint32_t id,uint32_t v,uint8_t *raw)
{
    uint8_t p[LCZ_PACKED],base[CZ_BYTES],next[CZ_BYTES];
    if(id>=LCZ_NP || !CZ_PD[id].label)return 0;
    memcpy(raw,cz_patch[tr].raw,CZ_BYTES);
    cz_ed_decode(p,raw);lcz_sx_encode(p,base,1);
    if(id>=LCZ_EBASE(0,0) && id<LCZ_OLD_NP && (id-LCZ_EBASE(0,0))%18u==16u){
        uint32_t end=p[id+1u];
        if(v!=8u && v>=end)v=v>p[id] || !end ? 8u : end-1u;
    }
    p[id]=(uint8_t)v;
    for(uint32_t l=0;l<2u;l++)for(uint32_t e=0;e<3u;e++){
        uint8_t *ep=p+LCZ_EBASE(l,e);
        ep[8u+ep[17]]=0;if(ep[16]!=8u && ep[16]>=ep[17])ep[16]=8;
    }
    lcz_sx_encode(p,next,1);
    for(uint32_t i=0;i<128u;i++){
        uint32_t x=base[i]^next[i];
        if(x==128u)raw[i]=(uint8_t)((raw[i]&127u)|(next[i]&128u));
        else if(x)raw[i]=next[i];
    }
    return cz_patch_valid(raw) && memcmp(raw,cz_patch[tr].raw,128u);
}
#endif
