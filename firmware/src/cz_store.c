/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Casio CZ-1 SysEx over USB-MIDI (Melodee's, docs/CZ1.md): the bytes collected by usb.c sysex_byte -> eng_cz.c
 * cz_sx_byte, served in the UI frame by cz_service (cr_ui.c, beside fm6_service). Casio's low nibble first, no
 * checksum; any channel n:
 *   F0 44 00 00 7n 10 60 F7 / 11 60 (send request: 128 / 144 bytes)   -> 7n 30 header, the handshake, the part's tone
 *   F0 44 00 00 7n 20 60 .. / 21 60 .. (receive request + the tone)   -> the part's tone
 *   F0 44 00 00 7n 30 <256 | 288 nibbles> F7 (a tone dump)            -> the part's tone
 * A 128-byte (CZ-101 / 1000) tone gets the CZ-1 line levels, velocity and name it lacks (and the CZ-1 DCW key-follow
 * table), a 144-byte one is kept verbatim. Melodee's banks travel in its backup, not in Casio SysEx (no Casio bank
 * dump exists): ChoralRoot's backup objects 14..21 (cr_backup.c).
 *
 * ChoralRoot: the CZ-1 part is the part the sound editor shows when it plays CZ-1, else the chord part's, else the
 * bass part's (as fm6_store.c: the channel does not choose); a tone received marks that sound edited (SAVE keeps it in
 * a user slot, cz_ustore.c). The frames out go through CZ_SEND (the device: ota_wire_send; the host tests capture
 * them). Included after cr_ui.c. */
#if FELUCCA_CZ
#ifndef CZ_SEND
#if FELUCCA_OTA
#define CZ_SEND(p, n) ota_wire_send((p), (n))
#else
#define CZ_SEND(p, n) ((void)(p), (void)(n))         /* (the emulator: no SysEx transport) */
#endif
#endif

static uint8_t cz_tx[295] __attribute__((section(".pool"))),cz_handshake;
static uint32_t cz_wait_at;
static int cz_is(uint32_t k) { return k < NPART && trk[k].eng_req == ENGI_CZ; }
static int cz_sx_track(uint32_t ch)
{
    (void)ch;
    if (cu.page == PG_EDIT && cz_is(ce.part ? CR_PART_BASS : CR_PART_CHORD))
        return (int)(ce.part ? CR_PART_BASS : CR_PART_CHORD);
    if (cz_is(CR_PART_CHORD))
        return (int)CR_PART_CHORD;
    return cz_is(CR_PART_BASS) ? (int)CR_PART_BASS : -1;
}
static void cz_sx_header(uint8_t ch){cz_tx[0]=0xf0;cz_tx[1]=0x44;cz_tx[2]=cz_tx[3]=0;cz_tx[4]=ch;cz_tx[5]=0x30;}
/* the part's tone as a 7n 30 dump in cz_tx (native: 144 bytes, else the 128 of a CZ-101 / 1000): its length */
static uint32_t cz_sx_build(uint32_t tr,uint8_t ch,int native)
{
    uint8_t data[144];uint32_t n=native?144:128;memcpy(data,cz_patch[tr%NTRK].raw,CZ_BYTES);if(!native){static const uint8_t kw[]={0,25,51,78,106,134,163,193,223,255};for(uint32_t l=0;l<2;l++){uint32_t o=l*57u;data[16+o]&=15;data[19+o]=kw[data[18+o]];for(uint32_t j=20;j<=54;j+=17)data[j+o]&=15;}}cz_sx_header(ch);
    for(uint32_t j=0;j<n;j++){cz_tx[6+2*j]=data[j]&15;cz_tx[7+2*j]=data[j]>>4;}
    cz_tx[6+2*n]=0xf7;return 2*n+7;
}
static void cz_sx_send(uint32_t tr,uint8_t ch,int native,int continuation)
{
    uint32_t len=cz_sx_build(tr,ch,native);
    CZ_SEND(cz_tx+(continuation?6:0),len-(continuation?6:0));
}
static int cz_sx_import(uint32_t tr,const uint8_t *b,uint32_t n)
{
    uint8_t data[CZ_BYTES];if(n!=256&&n!=288)return 0;
    for(uint32_t j=0;j<n;j++)if(b[j]>15)return 0;
    memcpy(data,cz_patch[tr].raw,CZ_BYTES);
    for(uint32_t j=0;j<n/2;j++)data[j]=(uint8_t)(b[j*2]|b[j*2+1]<<4);
    if(n==256){static const uint8_t kw[]={0,31,44,57,70,83,96,111,146,255};for(uint32_t l=0;l<2;l++){uint32_t o=l*57u;if((data[18+o]&15)>9)return 0;data[16+o]&=15;data[19+o]=kw[data[18+o]&15];for(uint32_t j=20;j<=54;j+=17)data[j+o]|=240;}}
    if(!cz_patch_valid(data))return 0;
    track_t *t=&trk[tr];load_begin(t,UNDO_SOUND);panic_req|=(uint8_t)(1u<<tr);
    fm1_irq_off();memcpy(cz_patch[tr].raw,data,CZ_BYTES);t->p[P_E0]=t->p[P_E1]=0;t->p[P_E7]=CZ_NATIVE;cz_track_accept(t);fm1_irq_on();load_end(t);sync_reload=1;
    if(tr<2)cu_edited(tr);                       /* (ChoralRoot: the part's sound, SAVE keeps the tone) */
    ui_message("CZ TONE LOADED");ui.force=1;return 1;
}
static void cz_service(void)
{
    if(cz_rx_abort || (cz_handshake && (fm1_ms-cz_wait_at>3000u || !usb.config))){
        if(cz_handshake&&usb.config){cz_tx[0]=0xf7;CZ_SEND(cz_tx,1);}
        fm1_irq_off();cz_rx_on=cz_rx_req=cz_rx_go=cz_rx_ready=cz_rx_abort=0;fm1_irq_on();cz_handshake=0;return;
    }
    if(!cz_rx_req&&!cz_rx_ready&&!cz_rx_go)return;
    RING_PUBLISH();uint32_t n=cz_rx_n;uint8_t cmd=cz_rx[5],ch=cz_rx[4];int tr=cz_sx_track(ch&15);
    if(n<7||cz_rx[2]||cz_rx[3]||(ch&0xf0)!=0x70||tr<0){if(tr<0&&cz_rx_ready)ui_message("CZ: NO CZ-1 PART");goto done;}
    if(cz_rx_ready){
        if(cmd==0x20||cmd==0x21){
            int ok=((cmd==0x20&&n==264)||(cmd==0x21&&n==296))&&cz_sx_import((uint32_t)tr,cz_rx+7,n-8);
            if(ok||cz_handshake){
                if(cz_handshake){cz_tx[0]=0xf7;CZ_SEND(cz_tx,1);}else{cz_sx_header(ch);cz_tx[6]=0xf7;CZ_SEND(cz_tx,7);}
            }
            if(!ok)ui_message("CZ INVALID TONE");
        }else if(cmd==0x30&&(n==263||n==295)){if(!cz_sx_import((uint32_t)tr,cz_rx+6,n-7))ui_message("CZ INVALID TONE");}
        else if((cmd==0x10||cmd==0x11)&&n==8&&cz_rx[6]==0x60)cz_sx_send((uint32_t)tr,ch,cmd==0x11,0);

        cz_handshake=0;goto done;
    }
    if(cz_rx_go){cz_sx_send((uint32_t)tr,ch,cmd==0x11,1);cz_rx_go=0;cz_rx_req=0;return;}
    if(cz_rx_req){
        if((cmd==0x10||cmd==0x11||cmd==0x20||cmd==0x21)&&cz_rx[6]==0x60){
            cz_sx_header(ch);CZ_SEND(cz_tx,6);
            cz_handshake=1;cz_wait_at=fm1_ms;
        }
        cz_rx_req=0;return;
    }
    return;
done:cz_rx_req=cz_rx_go=0;RING_PUBLISH();cz_rx_ready=0;
}
#endif /* FELUCCA_CZ */
