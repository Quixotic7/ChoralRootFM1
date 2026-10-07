/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Eight independent 16-tone banks. One main-loop cache; audio reads track copies.
 * Each bank commits through storage.c A/B sectors, never over project data.
 * ChoralRoot (docs/CZ1.md): Melodee's, included by upreset.c after fm6_bank.c (FELUCCA_CZ); the banks are storage.c
 * objects OBJ_CZBANK0..+7 on the flash of the user sample slot 1 (0xA2000..0xB1FFF, unused since the all-synth
 * change), the backup's objects 14..21 (cr_backup.c: their only way in and out besides a restore; a bank never saved
 * is not listed, as Melodee's backup). No COMPARE copy (Melodee's CZ TOOLS are not ported). */
#define CZ_BANK_MAGIC 0x42435A43u
#define CZ_BANK_N 8u
#define CZ_BANK_SLOTS 16u
typedef struct { uint32_t magic; uint16_t ver, slots; uint32_t used; char name[16]; cz_patch_t tone[16]; } cz_bank_t;
_Static_assert(sizeof(cz_bank_t)==2332u && sizeof(cz_bank_t)<=3840u,"CZ bank sector");
static cz_bank_t cz_bank_cache __attribute__((section(".pool")));
static uint8_t cz_bank_cached=255,cz_bank_saved;   /* the cached bank was saved (not its default) */
#if !FELUCCA_FLASH
static cz_bank_t cz_banks_host[CZ_BANK_N];
#endif
static int cz_bank_valid(const cz_bank_t *b)
{
    if(b->magic!=CZ_BANK_MAGIC || b->ver!=1 || b->slots!=16 || (b->used>>16) || !b->name[0])return 0;
    for(uint32_t i=0;i<16;i++)if((uint8_t)b->name[i]>126 || (b->name[i] && (uint8_t)b->name[i]<32))return 0;
    for(uint32_t i=0;i<16;i++)if((b->used>>i)&1u)if(!cz_patch_valid(b->tone[i].raw))return 0;
    return 1;
}
static void cz_bank_empty(cz_bank_t *b,uint32_t k)
{
    memset(b,0,sizeof *b);b->magic=CZ_BANK_MAGIC;b->ver=1;b->slots=16;
    memcpy(b->name,"BANK A",6);b->name[5]=(char)('A'+k);
}
/* a bank never saved: BANK A..D Casio's CZ-1 preset tones (A-1 .. H-8, 16 a bank), E..H empty */
static void cz_bank_default(cz_bank_t *b,uint32_t k)
{
    cz_bank_empty(b,k);
    if(k>=CZ_FACTORY_N/CZ_BANK_SLOTS)return;
    memcpy(b->name,"CZ-1 A1-B8",10);b->name[5]=(char)('A'+2u*k);b->name[8]=(char)('B'+2u*k);
    b->used=0xFFFFu;
    for(uint32_t i=0;i<CZ_BANK_SLOTS;i++)memcpy(b->tone[i].raw,CZ_FACTORY[k*CZ_BANK_SLOTS+i],CZ_BYTES);
}
static cz_bank_t *cz_bank_load(uint32_t k)
{
    if(k>=CZ_BANK_N)return 0;
    if(cz_bank_cached!=k){
#if FELUCCA_FLASH
        int n=flash_ok?st_load(OBJ_CZBANK0+k,&cz_bank_cache,sizeof cz_bank_cache):-1;
        cz_bank_saved=n==(int)sizeof cz_bank_cache && cz_bank_valid(&cz_bank_cache);
#else
        cz_bank_cache=cz_banks_host[k];cz_bank_saved=(uint8_t)cz_bank_valid(&cz_bank_cache);
#endif
        if(!cz_bank_saved)cz_bank_default(&cz_bank_cache,k);
        cz_bank_cached=(uint8_t)k;
    }
    return &cz_bank_cache;
}
static int cz_bank_get(uint32_t bank,uint32_t slot,uint8_t *raw)
{
    cz_bank_t *b=cz_bank_load(bank);if(!b || slot>=16 || !((b->used>>slot)&1u))return 1;
    memcpy(raw,b->tone[slot].raw,CZ_BYTES);return 0;
}
static void cz_bank_boot(void){cz_bank_cached=255;cz_user_bank_read=cz_bank_get;}
static void cz_bank_import(uint32_t k,const uint8_t *raw,uint32_t len)
{
#if !FELUCCA_FLASH
    if(len)memcpy(&cz_banks_host[k],raw,sizeof(cz_bank_t));else memset(&cz_banks_host[k],0,sizeof(cz_bank_t));   /* (none: the default) */
#else
    (void)raw;(void)len;
#endif
    cz_bank_cached=255;
    for(uint32_t t=0;t<NTRK;t++)if(trk[t].eng_req==ENGI_CZ && (uint32_t)trk[t].p[P_E0]==k && trk[t].p[P_E1])cz_user_pick[t]=65535u;
}

static void cz_bank_poll(void)
{
    for(uint32_t k=0;k<NTRK;k++){
        track_t *t=&trk[k];uint32_t pick=(uint32_t)t->p[P_E0]*17u+(uint32_t)t->p[P_E1];
        if(t->eng_req!=ENGI_CZ || cz_user_pick[k]==pick)continue;
        uint8_t raw[CZ_BYTES];int ok=1;
        if(!t->p[P_E1])cz_patch_init(raw);
        else ok=cz_user_bank_read && !cz_user_bank_read((uint32_t)t->p[P_E0],(uint32_t)t->p[P_E1]-1u,raw);
        if(ok){fm1_irq_off();memcpy(cz_patch[k].raw,raw,CZ_BYTES);panic_req|=(uint8_t)(1u<<k);fm1_irq_on();}
        cz_user_pick[k]=(uint16_t)pick;
    }
}
