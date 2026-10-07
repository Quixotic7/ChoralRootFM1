/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MELODEE_CZ1_PATCH_H
#define MELODEE_CZ1_PATCH_H

#define LCZ_NAME 16u
/* Flat, byte-sized patch IDs: all native controls are available on device. */
enum { LCZ_LINE, LCZ_MOD, LCZ_OCT, LCZ_SIGN, LCZ_DOCT, LCZ_NOTE, LCZ_FINE,
       LCZ_VWAVE, LCZ_VRATE, LCZ_VDEP, LCZ_VDELAY, LCZ_BEND, LCZ_WHEEL, LCZ_ATV, LCZ_ATA,
       LCZ_MODON, LCZ_PORTON, LCZ_PORTMODE, LCZ_PORTTIME, LCZ_GLIDEON, LCZ_GLIDENOTE, LCZ_GLIDETIME,
       LCZ_CHOR, LCZ_GLOBALS };
#define LCZ_LBASE(l) (LCZ_GLOBALS + (l)*8u)
enum { LCZ_W1, LCZ_W2, LCZ_KW, LCZ_KA, LCZ_LEVEL, LCZ_VP, LCZ_VW, LCZ_VA };
#define LCZ_EBASE(l,e) (LCZ_GLOBALS+16u+((l)*3u+(e))*18u)
#define LCZ_OLD_NP 147u
#define LCZ_OLD_PACKED 163u
#define LCZ_WIN(l) (LCZ_OLD_NP+(l))
#define LCZ_NP (LCZ_OLD_NP+2u)
#define LCZ_PACKED (LCZ_NP+LCZ_NAME)
static const uint8_t LCZ_MAX[LCZ_NP]={3,2,2,1,3,11,60,3,99,99,99,12,99,99,15,1,1,1,99,1,48,99,1,7,8,9,9,15,15,15,15,7,8,9,9,15,15,15,15,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,7,7};
static const int8_t LCZ_PRESET_MIN[92]={0,0,0,0,0,-64,-64,-64,-64,0,0,0,0,-64,-64,-64,0,0,0,1,1,0,0,0,0,0,0,0,-24,1,0,0,1,0,0,0,0,0,0,-64,0,0,0,0,0,0,1,0,0,0,0,-64,0,0,-64,0,0,-64,0,0,-64,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,-64,-64,-64,-64,-64,-64,-64,0};
static const uint8_t LCZ_PRESET_WIDTH[92]={7,7,7,7,7,7,7,7,7,7,3,7,7,7,7,7,7,3,4,2,7,7,7,1,1,4,4,3,6,6,4,7,7,7,7,7,7,2,7,7,1,1,2,1,7,2,4,3,7,4,5,7,4,5,7,4,5,7,4,5,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,4,3,4,7,7,7,7,7,7,7,4};
static const int8_t LCZ_PRESET_MAX[92]={127,127,127,127,127,63,63,63,63,127,4,127,127,63,63,63,127,6,9,4,127,100,127,1,1,11,15,4,24,64,9,100,127,127,127,127,127,3,127,63,1,1,2,1,127,2,16,5,127,8,19,63,8,19,63,8,19,63,8,19,63,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,9,4,12,63,63,63,63,63,63,63,8};
/* 871 native control bits + 16 seven-bit name characters. */
#define LCZ_COMPACT_BITS 983u
static int lcz_valid(const uint8_t *p)
{
    for(uint32_t j=0;j<LCZ_NP;j++)if(p[j]>LCZ_MAX[j])return 0;
    for(uint32_t l=0;l<2;l++){
        uint32_t b=LCZ_LBASE(l);
        if(!p[b+LCZ_LEVEL])return 0;
        for(uint32_t e=0;e<3;e++){b=LCZ_EBASE(l,e);if(p[b+16]!=8 && p[b+16]>p[b+17])return 0;if(p[b+8+p[b+17]])return 0;}
    }
    for(uint32_t j=LCZ_NP;j<LCZ_PACKED;j++)if(p[j]<32||p[j]>126)return 0;
    return 1;
}
/* Version 1 used panel wave numbers and an implicit resonance window.
 * Read it into the native carrier/window model without changing its sound. */
static int lcz_upgrade(uint8_t *p,const uint8_t *old)
{
    static const uint8_t carrier[]={0,1,2,4,5,6,6,6};
    memcpy(p,old,LCZ_OLD_NP);memcpy(p+LCZ_NP,old+LCZ_OLD_NP,LCZ_NAME);
    for(uint32_t l=0;l<2;l++){
        uint32_t b=LCZ_LBASE(l),w1=old[b],w2=old[b+1];
        if(w1>7||w2>8||(w1>=5&&w2>=6))return 0;
        p[LCZ_WIN(l)]=(uint8_t)(w1>=5?w1-4:w2>=6?w2-5:0);
        p[b]=carrier[w1];p[b+1]=(uint8_t)(w2?carrier[w2-1]+1:0);
    }
    return lcz_valid(p);
}
static uint32_t lcz_sx_rate(uint32_t e,uint32_t a){return e==0?a*127/99:e==1?8+a*119/99:a*119/99;}
static uint32_t lcz_sx_level(uint32_t e,uint32_t a){return e==0?a+(a>63?4:0):e==1?a*127/99:a?a+28:0;}
static uint32_t lcz_sx_unrate(uint32_t e,uint32_t b){b&=127;if(e==1)b=b>8?b-8:0;return b?(b*99/(e==0?127:119)+1>99?99:b*99/(e==0?127:119)+1):0;}
static uint32_t lcz_sx_unlevel(uint32_t e,uint32_t b){b&=127;uint32_t a=e==0?(b>63?b-4:b):e==1?(b==127?99:b?b*99/127+1:0):b>28?b-28:0;return a>99?99:a;}
static uint32_t lcz_sx_vdata(uint32_t n,uint32_t kind)
{
    uint32_t group=n<32?0:(n-16)/16,step=1u<<group;
    if(kind==1){static const uint16_t start[]={0x20,0x460,0x8e0,0x11e0,0x23e0,0x47e0};return start[group]+32*step*(n-(group?16+16*group:0));}
    uint32_t v=group?((n-16-16*group)*step+((17u<<group)-1u)):n;
    return kind==2?(n==99?0x300:v+step):v;
}
static void lcz_sx_setwave(uint8_t *d,uint32_t w1,uint32_t w2,uint32_t window,uint32_t mod)
{
    d[0]=(uint8_t)(w1<<5 | (w2?(w2-1)<<2|2:0) | window>>2);
    d[1]=(uint8_t)((window&3)<<6 | (mod==1?4:mod==2?3:0)<<3);
}
static void lcz_sx_encode(const uint8_t *p,uint8_t *d,int native)
{
    memset(d,0,144);uint32_t line=p[LCZ_LINE];
    d[0]=(uint8_t)((line==2?3:line==3?2:line) | (p[LCZ_OCT]==0?2:p[LCZ_OCT]==2?1:0)<<2);
    d[1]=p[LCZ_SIGN];/* Casio p. 84: convert the display value to six bits, then put
     * those bits in positions 7..2 of PDETL (not positions 5..0). */
    d[2]=(uint8_t)((p[LCZ_FINE]+(p[LCZ_FINE]?((p[LCZ_FINE]-1)/15):0))<<2);d[3]=(uint8_t)(p[LCZ_DOCT]*12+p[LCZ_NOTE]);
    static const uint8_t vw[]={8,4,32,2};d[4]=vw[p[LCZ_VWAVE]];
    /* The display byte precedes the machine value's low byte, then high byte
     * (Casio p. 85; e.g. RATE 50 is 32 e0 09 on the wire). */
    for(uint32_t j=0;j<3;j++){uint32_t val=p[j==0?LCZ_VDELAY:j==1?LCZ_VRATE:LCZ_VDEP],v=lcz_sx_vdata(val,j);d[5+j*3]=(uint8_t)val;d[6+j*3]=(uint8_t)v;d[7+j*3]=(uint8_t)(v>>8);}
    static const uint8_t ka[]={0,8,17,26,36,47,58,69,82,95},kw[]={0,31,44,57,70,83,96,111,146,255};
    for(uint32_t l=0;l<2;l++){
        uint32_t b=LCZ_LBASE(l),o=l?71:14;lcz_sx_setwave(d+o,p[b+LCZ_W1],p[b+LCZ_W2],p[LCZ_WIN(l)],l?0:p[LCZ_MOD]);
        d[o+2]=(uint8_t)(p[b+LCZ_KA] | (native?(15-p[b+LCZ_LEVEL])<<4:0));d[o+3]=ka[p[b+LCZ_KA]];
        d[o+4]=p[b+LCZ_KW];d[o+5]=kw[p[b+LCZ_KW]];
        for(uint32_t e=0;e<3;e++){
            const uint8_t *ep=p+LCZ_EBASE(l,e);uint32_t off=o+(e==2?6:e==1?23:40);
            d[off]=(uint8_t)(ep[17] | (native?(15-p[b+(e==0?LCZ_VP:e==1?LCZ_VW:LCZ_VA)])<<4:0));
            uint32_t prev=0;
            for(uint32_t j=0;j<8;j++){uint32_t val=ep[8+j];d[off+1+2*j]=(uint8_t)(lcz_sx_rate(e,ep[j]) | (val<prev?128:0));d[off+2+2*j]=(uint8_t)(lcz_sx_level(e,val) | (ep[16]==j?128:0));prev=val;}
        }
    }
    if(native)memcpy(d+128,p+LCZ_NP,16);
}

static int cz_legacy_tone(uint8_t *out,const uint8_t *p,uint32_t n)
{
    uint8_t upgraded[LCZ_PACKED];
    if(n==LCZ_OLD_PACKED){if(!lcz_upgrade(upgraded,p))return 0;p=upgraded;}
    else if(n!=LCZ_PACKED || !lcz_valid(p))return 0;
    lcz_sx_encode(p,out,1);return cz_patch_valid(out);
}
#endif
