/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the WL82 baseband driver (firmware/src/ble/ble_hw_wl82.c) with the whole stack, against a fake engine
 * that follows the TX contract of docs/BLE-HW-FACTS.md §8.2 and the RX the FM-1 measured (§8.1):
 *   - advertising: the CONNECT_IND lands in the buffer RXTOG then points to, RXTOG moves past it, RXBUFnCNTL stays 0;
 *     advertising leaves TXTOG bit0 = 1;
 *   - connection RX: the engine fills RXTOG's buffer, sets its RXBUFnCNTL bit0, moves RXTOG; RXDHDRn carries the
 *     central's NESN (bit2) and SN (bit3), Core layout; a repeated SN is dropped by the engine;
 *   - connection TX (§8.2): TXBUFnCNTL bit0 = 1 is "empty", 0 "handed to the engine". The engine transmits only a
 *     buffer with bit0 = 0, the TXTOG buffer first (else the other one, TXTOG moving to it); with neither, an empty PDU
 *     of its own. It keeps SN / NESN itself (Core Vol 6 Part B 4.5.9), retransmits its copy of an unacknowledged PDU,
 *     and when the central acknowledges a PDU from a buffer it sets that buffer's bit0 = 1 and moves TXTOG bit0 to the
 *     other buffer. Optionally the FM-1's first-event quirk (§8.1 blell4, §8.2 point 2): at its first transmission
 *     the engine clears bit0 of the TXTOG buffer with nothing loaded and sends set-up's empty PDU from it;
 *   - the Mac: VERSION_IND, FEATURE_REQ two events later, LL_LENGTH_REQ once it has our FEATURE_RSP, then ATT as macOS
 *     ran it on the FM-1 (blell10-12): Exchange MTU, primary services, the GATT service's characteristics and
 *     descriptors, its Service Changed CCCD on, Read By Type 0x2B2A; a Service Changed indication there stalls it, as it
 *     did on the FM-1; then CoreMIDI's part (mac_att): the MIDI characteristic, its CCCD, a Read (empty), the CCCD on,
 *     our parameter request answered, MIDI both ways; with BLE_MIDI_NEED_ENC, LE legacy pairing on Insufficient
 *     Authentication and the LL encryption through the driver; it terminates (0x13) 249 events (7.5 s) after a
 *     LENGTH_REQ nobody answered, as it did after blell8's;
 *   - the slot clock (columns 0 / 14) steps back 267 slots now and then, as the FM-1's did.
 * The fake also watches the driver: after set-up it must never write bit0 = 1, never write TXTOG, never change TXDHDR
 * bit2, never touch the header or payload of a buffer the engine holds (MD excepted), and the event interrupt must
 * never load a buffer. Checks: the whole exchange, once each and in order, the link past 60 s, with packet loss both
 * ways, with a central that waits for our PERIPHERAL_FEATURE_REQ, TIMER4 wrapping, the 40 s timeout when the central
 * never answers, and that the previous drivers' polarity (bit0 = 1 written on a loaded buffer, 5008663 .. 51792b7)
 * stalls against this engine as it did on the FM-1. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "../firmware/src/ble/ble_stack.c"
#include "../firmware/src/ble/ble_diag.c"
/* the diagnostics' counters (ble_dg) exist unless the stack is built with -DBLE_DIAG=0, which this test also runs:
 * DG(expr) is the check on a counter, true without them; DGV(expr) is a counter to print, 0 without them */
#if BLE_DIAG
#define DG(x) (x)
#define DGV(x) (x)
#else
#define DG(x) 1
#define DGV(x) 0u
#endif

static int fails;
static void check(const char *what, int ok)
{
    printf("%-118s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static struct {
    int on;                             /* the app has a MIDI event for us */
    uint32_t popped, midi_in;
} app;

static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t)
{
    if (!app.on)
        return 0;
    *pkt = 0x7F3C9009u;                 /* note on, C4, 127 (USB-MIDI CIN 9) */
    *t = 100;
    return 1;
}
static void ble_app_midi_pop(void)
{
    app.on = 0;
    app.popped++;
}
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last)
{
    app.midi_in += pkt == (0x09u | 0x90u << 8 | 0x3Cu << 16 | 0x64u << 24);
    (void)ts;
    (void)last;
}
static void ble_app_state(void) {}
#if BLE_SMP_LEGACY
static int bond_n;
static void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16])
{
    (void)rand, (void)ediv, (void)ltk;
    bond_n++;
}
static int peer_id_n;
static void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t addr_rand)
{
    (void)irk, (void)addr, (void)addr_rand;
    peer_id_n++;
}
#endif
#if BLE_CENTRAL
static void ble_app_central_keys(const struct ble_keys *k) { (void)k; }
#endif

#define IV 24u                          /* the Mac's CONNECT_IND: 30 ms, timeout 72 (720 ms), hop 13, sca 1 */
#define TICKS_PER_EVT (IV * 1250u * FM1_TICKS_PER_US)
#define MAC_LEN_WAIT 249u               /* the Mac's patience with its LENGTH_REQ (blell8: 7.47 s) */
#define MIDI_EVT 300u                   /* the app's note (after the Mac's discovery, and its pairing) */

enum { C_WAITS, C_MAC, C_SILENT };      /* the central: answers our FEATURE_REQ / asks first (blell8) / never answers */
enum { O_QUIRK = 1, O_LOSS = 2, O_OLDPOL = 4 };   /* run options */

static struct {
    int kind, opt;                      /* C_*, O_* */
    uint8_t q[8][32], qlen[8], qllid[8];  /* the central's PDUs to send */
    int qn;
    uint8_t cur[32], cur_len, cur_llid; /* the PDU in flight (until acknowledged) */
    int cur_ok, sn, nesn;
    int ver_rx, feat_req_rx, feat_rsp_rx, len_rsp_rx, len_req_rx, mtu_rsp_rx, group_rsp_rx, write_rsp_rx, notif_rx;
    int data_rx, empties, dups, order_bad;
    uint32_t e, len_req_evt, notif_evt, mtu_req_evt;
    int len_req_out, terminated;
    /* the Mac's GATT client (mac_att): where it is, what it found */
    int st, sc_ind, hash_err, midi_read_empty, cpup_rsp, att_bad, auth_err, paired, enc_on, keys_rx, midi_in_n, refused;
    uint16_t next, svc_s[4], svc_e[4], sc_val, sc_ccc, midi_s, midi_e, midi_val, midi_ccc;
    uint8_t midi_props;
    int nsvc;
    /* SMP and the LL encryption (BLE_SMP_LEGACY builds) */
    uint8_t preq[7], pres[7], mrand[16], mconf[16], stk[16], skdm[8], ivm[4];
    int tx_enc, rx_enc;
#if BLE_LL_ENC
    struct ble_ccm ctx, crx;
#endif
} cen;

static struct {                         /* the fake engine's TX state */
    int tsn, nesn, sent_any;
    int last_b;                         /* the buffer the PDU in flight came from; -1: an empty PDU of the engine's */
    int in_flight;                      /* a PDU sent and not yet acknowledged (retransmitted from eng.last) */
    uint8_t last[40];                   /* that PDU (header 2 + payload) */
    uint32_t sends[2];                  /* data PDUs sent from each buffer (not counting retransmissions) */
    /* the watch on the driver */
    uint8_t cntl[2];                    /* TXBUFnCNTL as the engine last left it */
    uint16_t tog, dhdr[2];
    uint8_t pay[2][32];
    uint32_t sw_set1, sw_tog, sw_bit2, sw_touch, evt_load, old_writes;
} eng;

static void cen_pdu(uint8_t llid, const uint8_t *p, int n)
{
    if (cen.qn >= 8 || n + 4 > 32) {
        cen.att_bad++;                                                  /* (the fake central's own limits) */
        return;
    }
    memcpy(cen.q[cen.qn], p, (size_t)n);
#if BLE_LL_ENC
    if (cen.tx_enc && n) {                                              /* encrypted as queued: a resend is the same */
        ble_ccm_encrypt(&cen.ctx, llid, cen.q[cen.qn], (uint8_t)n);
        n += 4;
    }
#endif
    cen.qlen[cen.qn] = (uint8_t)n;
    cen.qllid[cen.qn++] = llid;
}

static void cen_l2(uint16_t cid, const uint8_t *p, int n)              /* one L2CAP frame in one PDU */
{
    uint8_t f[32];
    f[0] = (uint8_t)n, f[1] = 0, f[2] = (uint8_t)cid, f[3] = 0;
    memcpy(f + 4, p, (size_t)n);
    cen_pdu(2, f, n + 4);
}

static void cen_att(const uint8_t *p, int n) { cen_l2(4, p, n); }

static void cen_req7(uint8_t op, uint16_t s, uint16_t e, uint16_t uuid)   /* Read By (Group) Type, 16-bit UUID */
{
    uint8_t q[7] = {op, (uint8_t)s, (uint8_t)(s >> 8), (uint8_t)e, (uint8_t)(e >> 8), (uint8_t)uuid, (uint8_t)(uuid >> 8)};
    cen_att(q, op == 0x04 ? 5 : 7);
}
static void cen_ctrl(const uint8_t *p, int n) { cen_pdu(3, p, n); }

static void cind(void)                  /* the CONNECT_IND, as the FM-1 stored it */
{
    uint32_t b = CB->rxtog & 1u;
    uint8_t *p = bb.rx[b].buf + HW_SWHDR;
    static const uint8_t init_a[6] = {1, 2, 3, 4, 5, 0x46};
    memcpy(p, init_a, 6);
    memcpy(p + 6, drv.adv.adv + 2, 6);
    p[12] = 0xDB, p[13] = 0xAC, p[14] = 0x9A, p[15] = 0xAF;           /* AA AF9AACDB */
    p[16] = 0xA4, p[17] = 0xEF, p[18] = 0x2D;                          /* CRC init */
    p[19] = 3;                                                          /* WinSize */
    p[20] = 22, p[21] = 0;                                              /* WinOffset */
    p[22] = IV, p[23] = 0;
    p[24] = 0, p[25] = 0;
    p[26] = 72, p[27] = 0;
    p[28] = 0xFF, p[29] = 0xFF, p[30] = 0xFF, p[31] = 0xFF, p[32] = 0x1F;
    p[33] = (uint8_t)(13u | 1u << 5);
    CB->rxahdr[b] = 0x2285u;                                            /* blell3 rxsnap: ahdr 2285, dhdr 2200 */
    CB->rxdhdr[b] = 0x2200u;
    CB->rxstat[b] = 0x9401u;
    CB->rxtog ^= 1u;                                                    /* moved past; RXBUFnCNTL stays 0 */
    CB->txtog = 1u;                                                     /* advertising left TXTOG on buffer 1 (blell4) */
    ble_wl82_rx_irq();
}

/* the central takes a new PDU of ours (header h, payload q) */
/* ---- the Mac's GATT client. What the FM-1 saw macOS do (blell10-12, 2026-10-09): MTU, primary services, the GATT
 * service's characteristics and descriptors, its Service Changed CCCD on, a Read By Type (the Database Hash 0x2B2A,
 * as Core 5.1 robust caching reads it [I]); an indication of Service Changed is confirmed and then nothing more comes
 * (st M_STALLED: the stall the hardware showed). Then what CoreMIDI needs for "Connected": the MIDI service's
 * characteristics and descriptors, a Read of MIDI I/O (empty, BLE-MIDI 1.0), its CCCD on, then MIDI both ways. A CCCD
 * write refused with Insufficient Authentication (0x05) starts LE legacy pairing (BLE_SMP_LEGACY builds) and the write
 * is made again once the link is encrypted. */
enum { M_MTU, M_SVC, M_GATT_CHR, M_GATT_DSC, M_SC_ON, M_HASH, M_MIDI_CHR, M_MIDI_DSC, M_MIDI_READ, M_MIDI_ON, M_PAIR,
       M_DONE, M_STALLED };
#define HASH_UUID 0x2B2Au
static const uint8_t MIDI_IO_UUID[16] = {0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1,
                                         0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77};
static const uint8_t MAC_INITA[6] = {1, 2, 3, 4, 5, 0x46};               /* (cind(): public, TxAdd 0) */
static const uint8_t OUR_ADDR[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};   /* (run(): random static) */

static void mac_midi_on(void)
{
    uint8_t w[5] = {0x12, (uint8_t)cen.midi_ccc, 0, 1, 0};
    cen.st = M_MIDI_ON;
    cen_att(w, 5);
}

static void mac_pair(void);

static void mac_att(const uint8_t *a, int n)
{
    int i;
    if (a[0] == 0x1D) {                                                 /* an indication: confirmed, then the stall */
        static const uint8_t cf[1] = {0x1E};
        cen.sc_ind++;
        cen_att(cf, 1);
        cen.st = M_STALLED;
        return;
    }
    if (a[0] == 0x1B) {
        if (n >= 3 && (a[1] | a[2] << 8) == cen.midi_val) {
            cen.notif_rx++;
            cen.notif_evt = cen.e;
        }
        return;
    }
    switch (cen.st) {
    case M_MTU:
        if (a[0] != 0x03) break;
        cen.mtu_rsp_rx++;
        cen.st = M_SVC;
        cen_req7(0x10, 1, 0xFFFF, 0x2800);
        return;
    case M_SVC:
        if (a[0] == 0x11) {
            cen.group_rsp_rx++;
            for (i = 2; i + a[1] <= n && cen.nsvc < 4; i += a[1]) {
                uint16_t s = (uint16_t)(a[i] | a[i + 1] << 8), e = (uint16_t)(a[i + 2] | a[i + 3] << 8);
                cen.svc_s[cen.nsvc] = s, cen.svc_e[cen.nsvc++] = e;
                if (a[1] == 6 && a[i + 4] == 0x01 && a[i + 5] == 0x18)
                    cen.next = s;                                       /* (the GATT service 0x1801) */
                if (a[1] == 20 && !memcmp(a + i + 4, BLE_UUID_MIDI_SVC, 16))
                    cen.midi_s = s, cen.midi_e = e;
            }
            cen_req7(0x10, (uint16_t)(cen.svc_e[cen.nsvc - 1] + 1), 0xFFFF, 0x2800);
            return;
        }
        if (a[0] != 0x01 || a[4] != 0x0A) break;
        for (i = 0; i < cen.nsvc && cen.svc_s[i] != cen.next; i++)
            ;
        cen.st = M_GATT_CHR;
        cen.svc_s[3] = cen.svc_e[i];                                    /* (the GATT service's end) */
        cen_req7(0x08, cen.next, cen.svc_s[3], 0x2803);
        return;
    case M_GATT_CHR:
    case M_MIDI_CHR: {
        int gatt = cen.st == M_GATT_CHR;
        uint16_t end = gatt ? cen.svc_s[3] : cen.midi_e, last = 0;
        if (a[0] == 0x09) {
            for (i = 2; i + a[1] <= n; i += a[1]) {
                uint16_t val = (uint16_t)(a[i + 3] | a[i + 4] << 8);
                if (gatt && a[1] == 7 && a[i + 5] == 0x05 && a[i + 6] == 0x2A)
                    cen.sc_val = val;
                if (!gatt && a[1] == 21 && !memcmp(a + i + 5, MIDI_IO_UUID, 16))
                    cen.midi_val = val, cen.midi_props = a[i + 2];
                last = val;
            }
            cen_req7(0x08, (uint16_t)(last + 1), end, 0x2803);
            return;
        }
        if (a[0] != 0x01 || a[4] != 0x0A) break;
        cen.st = gatt ? M_GATT_DSC : M_MIDI_DSC;
        cen_req7(0x04, (uint16_t)((gatt ? cen.sc_val : cen.midi_val) + 1), end, 0);
        return;
    }
    case M_GATT_DSC:
    case M_MIDI_DSC:
        if (a[0] != 0x05 || a[1] != 1 || n < 6 || a[4] != 0x02 || a[5] != 0x29) break;
        if (cen.st == M_GATT_DSC) {
            uint8_t w[5] = {0x12, a[2], a[3], 2, 0};                    /* Service Changed: indications on */
            cen.sc_ccc = (uint16_t)(a[2] | a[3] << 8);
            cen.st = M_SC_ON;
            cen_att(w, 5);
        } else {
            uint8_t r[3] = {0x0A, (uint8_t)cen.midi_val, (uint8_t)(cen.midi_val >> 8)};
            cen.midi_ccc = (uint16_t)(a[2] | a[3] << 8);
            cen.st = M_MIDI_READ;
            cen_att(r, 3);
        }
        return;
    case M_SC_ON:
        if (a[0] != 0x13) break;
        cen.st = M_HASH;
        cen_req7(0x08, 1, 0xFFFF, HASH_UUID);
        return;
    case M_HASH:
        if (a[0] != 0x01 || a[1] != 0x08) break;
        cen.hash_err = a[4];
        cen.st = M_MIDI_CHR;                                            /* (CoreMIDI from here) */
        cen_req7(0x08, cen.midi_s, cen.midi_e, 0x2803);
        return;
    case M_MIDI_READ:
        if (a[0] == 0x01 && a[4] == 0x05) {
            cen.auth_err++;
            mac_pair();
            return;
        }
        if (a[0] != 0x0B) break;
        cen.midi_read_empty = n == 1;
        mac_midi_on();
        return;
    case M_MIDI_ON:
        if (a[0] == 0x01 && a[4] == 0x05) {                             /* Insufficient Authentication: pair */
            cen.auth_err++;
            mac_pair();
            return;
        }
        if (a[0] != 0x13) break;
        cen.write_rsp_rx++;
        cen.st = M_DONE;
        {
            static const uint8_t note[7] = {0x52, 0, 0, 0x80, 0x80, 0x90, 0x3C};
            uint8_t w[8];
            memcpy(w, note, 7);
            w[1] = (uint8_t)cen.midi_val;
            w[7] = 0x64;
            cen_att(w, 8);                                              /* a note from the Mac (Write Command) */
        }
        return;
    default:
        break;
    }
    cen.att_bad++;                                                      /* (not what the Mac expected) */
}

#if BLE_SMP_LEGACY
/* LE legacy pairing as the Mac starts it: DisplayYesNo, bonding + MITM + Secure Connections asked, keys offered;
 * NoInputNoOutput on our side leaves Just Works (TK 0) */
static void mac_pair(void)
{
    static const uint8_t preq[7] = {0x01, 0x01, 0x00, 0x2D, 0x10, 0x02, 0x01};
    int i;
    memcpy(cen.preq, preq, 7);
    for (i = 0; i < 16; i++)
        cen.mrand[i] = (uint8_t)(0xA0 + 3 * i);
    cen.refused = cen.st;                                               /* (asked again once encrypted) */
    cen.st = M_PAIR;
    cen_l2(6, preq, 7);
}

static void mac_smp(const uint8_t *s, int n)
{
    static const uint8_t zero[16] = {0};
    uint8_t m[17];
    if (cen.st != M_PAIR)
        return;
    if (s[0] == 0x02 && n == 7) {
        memcpy(cen.pres, s, 7);
        ble_smp_c1(zero, cen.mrand, cen.preq, cen.pres, 0, MAC_INITA, 1, OUR_ADDR, cen.mconf);
        m[0] = 0x03;
        memcpy(m + 1, cen.mconf, 16);
        cen_l2(6, m, 17);
    } else if (s[0] == 0x03 && n == 17) {
        memcpy(cen.mconf, s + 1, 16);                                   /* (Sconfirm, checked with Srand) */
        m[0] = 0x04;
        memcpy(m + 1, cen.mrand, 16);
        cen_l2(6, m, 17);
    } else if (s[0] == 0x04 && n == 17) {
        uint8_t c[16], d[23] = {LL_ENC_REQ};
        ble_smp_c1(zero, s + 1, cen.preq, cen.pres, 0, MAC_INITA, 1, OUR_ADDR, c);
        if (memcmp(c, cen.mconf, 16)) {
            cen.att_bad++;
            return;
        }
        ble_smp_s1(zero, s + 1, cen.mrand, cen.stk);
        for (int i = 0; i < 8; i++)
            cen.skdm[i] = (uint8_t)(0x10 + i);
        memcpy(cen.ivm, "\x24\xAB\xDC\xBA", 4);
        memcpy(d + 11, cen.skdm, 8);                                    /* EDIV 0, Rand 0: the STK */
        memcpy(d + 19, cen.ivm, 4);
        cen_ctrl(d, 23);
    } else if (s[0] >= 0x06 && s[0] <= 0x07) {
        cen.keys_rx++;
        if (s[0] == 0x07) {                                             /* ours done: the Mac's IRK and address */
            uint8_t k[17] = {0x08}, ad[8] = {0x09, 0, 1, 2, 3, 4, 5, 0x46};
            cen_l2(6, k, 17);
            cen_l2(6, ad, 8);
            cen.paired = 1;
            if (cen.refused == M_MIDI_READ) {                           /* the refused request, again */
                uint8_t r[3] = {0x0A, (uint8_t)cen.midi_val, (uint8_t)(cen.midi_val >> 8)};
                cen.st = M_MIDI_READ;
                cen_att(r, 3);
            } else
                mac_midi_on();
        }
    } else if (s[0] == 0x05)
        cen.att_bad++;
}

/* the Mac's side of the LL encryption: the session key from our ENC_RSP, both directions from START_ENC_REQ on */
static void mac_enc_ctrl(const uint8_t *c)
{
    static uint8_t skds[8], ivs[4];
    if (c[0] == LL_ENC_RSP) {
        memcpy(skds, c + 1, 8);
        memcpy(ivs, c + 9, 4);
    } else if (c[0] == LL_START_ENC_REQ) {
        uint8_t k[16], skd[16];
        static const uint8_t rsp[1] = {LL_START_ENC_RSP};
        for (int i = 0; i < 8; i++) {
            skd[i] = skds[7 - i];
            skd[8 + i] = cen.skdm[7 - i];
        }
        for (int i = 0; i < 16; i++)
            k[i] = cen.stk[15 - i];
        ble_aes128(k, skd, cen.ctx.key);
        memcpy(cen.ctx.iv, cen.ivm, 4);
        memcpy(cen.ctx.iv + 4, ivs, 4);
        cen.ctx.ctr = 0, cen.ctx.ctr_hi = 0;
        cen.crx = cen.ctx;
        cen.ctx.dir = 1;
        cen.crx.dir = 0;
        cen.tx_enc = cen.rx_enc = 1;
        cen_ctrl(rsp, 1);
    } else if (c[0] == LL_START_ENC_RSP)
        cen.enc_on = 1;
}
#else
static void mac_pair(void) { cen.st = M_STALLED; }
static void mac_smp(const uint8_t *s, int n) { (void)s, (void)n; }
#endif

static void cen_rx(uint16_t h, const uint8_t *q)
{
    uint8_t n = (uint8_t)(h >> 8);
#if BLE_LL_ENC
    static uint8_t plain[260];
#endif
    if (!n) {
        cen.empties++;
        return;
    }
#if BLE_LL_ENC
    if (cen.rx_enc) {
        memcpy(plain, q, n);
        if (!ble_ccm_decrypt(&cen.crx, (uint8_t)h, plain, n)) {
            cen.att_bad += 100;                                         /* (a MIC failure) */
            return;
        }
        n = (uint8_t)(n - 4u);
        h = (uint16_t)((h & 0xFFu) | (uint32_t)n << 8);
        q = plain;
    }
#endif
    if ((h & 3u) == 3u) {
#if BLE_SMP_LEGACY
        mac_enc_ctrl(q);
#endif
        switch (q[0]) {
        case LL_VERSION_IND:
            cen.ver_rx++;
            break;
        case LL_FEATURE_RSP:
            cen.feat_rsp_rx++;
            if (cen.kind == C_MAC && !cen.len_req_out) {                /* the Mac: DLE next (blell8) */
                static const uint8_t req[9] = {LL_LENGTH_REQ, 251, 0, 0x48, 0x08, 251, 0, 0x48, 0x08};
                cen_ctrl(req, 9);
                cen.len_req_out = 1;
            }
            break;
        case LL_LENGTH_RSP:
            cen.len_rsp_rx++;
            if (!cen.feat_rsp_rx)
                cen.order_bad++;                                        /* (before our FEATURE_RSP) */
            if (cen.kind == C_MAC) {
                static const uint8_t mtu[7] = {3, 0, 4, 0, 0x02, 0x05, 0x01};   /* Exchange MTU, 517 */
                cen_pdu(2, mtu, 7);
                cen.mtu_req_evt = cen.e;
            }
            break;
        case LL_LENGTH_REQ: {
            static const uint8_t rsp[9] = {LL_LENGTH_RSP, 251, 0, 0x48, 0x08, 251, 0, 0x48, 0x08};
            cen.len_req_rx++;
            cen_ctrl(rsp, 9);
            break;
        }
        case LL_PERIPHERAL_FEATURE_REQ: {
            static const uint8_t rsp[9] = {LL_FEATURE_RSP, 0x01};
            cen.feat_req_rx++;
            if (cen.kind == C_WAITS)
                cen_ctrl(rsp, 9);
            break;
        }
        default:
            break;
        }
        return;
    }
    cen.data_rx++;
    if ((h & 3u) != 2u || n < 5u || q[0] + 4u != n || q[3])
        return;                                                         /* (every frame of ours fits one PDU here) */
    if (q[2] == 4u)
        mac_att(q + 4, q[0]);
    else if (q[2] == 5u && q[4] == 0x12u) {                             /* our Connection Parameter Update Request */
        uint8_t rsp[6] = {0x13, q[5], 2, 0, 0, 0};                      /* accepted (the Mac then updates) */
        cen.cpup_rsp++;
        cen_l2(5, rsp, 6);
    } else if (q[2] == 6u)
        mac_smp(q + 4, q[0]);
}

/* the engine's view of the TX registers, for the watch */
static void eng_save(void)
{
    uint32_t b;
    for (b = 0; b < 2u; b++) {
        eng.cntl[b] = CB->txbufcntl[b];
        eng.dhdr[b] = CB->txdhdr[b];
        memcpy(eng.pay[b], bb.tx[b].buf + HW_SWHDR, sizeof eng.pay[b]);
    }
    eng.tog = CB->txtog;
}

/* what the driver did since eng_save (evt: in the event interrupt) */
static void eng_watch(int evt)
{
    uint32_t b;
    if (drv.state != HW_CONN)
        return;                                                         /* (closed: advertising rewrites the buffers) */
    if (CB->txtog != eng.tog)
        eng.sw_tog++;
    for (b = 0; b < 2u; b++) {
        if ((CB->txbufcntl[b] & 1u) && !(eng.cntl[b] & 1u))
            eng.sw_set1++;
        if ((CB->txdhdr[b] ^ eng.dhdr[b]) & 4u)
            eng.sw_bit2++;
        if (!(eng.cntl[b] & 1u) && (((CB->txdhdr[b] ^ eng.dhdr[b]) & ~8u) ||
                                    memcmp(eng.pay[b], bb.tx[b].buf + HW_SWHDR, sizeof eng.pay[b])))
            eng.sw_touch++;
        if (evt && (eng.cntl[b] & 1u) && !(CB->txbufcntl[b] & 1u))
            eng.evt_load++;
    }
}

/* the previous drivers (5008663 .. 51792b7): a loaded buffer marked with bit0 = 1 ("loaded" in their polarity) */
static void old_polarity(void)
{
    uint32_t b;
    for (b = 0; b < 2u; b++)
        if (drv.tx_rec[b] && !(CB->txbufcntl[b] & 1u)) {
            CB->txbufcntl[b] |= 1u;
            eng.old_writes++;
        }
}

/* the engine's packet T_IFS after the central's: its copy again while unacknowledged, else a buffer with bit0 = 0
 * (TXTOG's first), else an empty PDU of its own */
static void eng_send(void)
{
    uint32_t t = CB->txtog & 1u, b;
    if (eng.in_flight)
        return;                                                         /* (eng.last is resent) */
    if ((cen.opt & O_QUIRK) && !eng.sent_any && (CB->txbufcntl[t] & 1u))
        CB->txbufcntl[t] &= (uint8_t)~1u;                               /* §8.1: set-up's empty PDU, bit0 cleared */
    b = !(CB->txbufcntl[t] & 1u) ? t : !(CB->txbufcntl[t ^ 1u] & 1u) ? t ^ 1u : 2u;
    if (b < 2u) {
        if (b != t)
            CB->txtog = (uint16_t)((CB->txtog & ~1u) | b);
        eng.last[0] = (uint8_t)((CB->txdhdr[b] & 3u) | (CB->txdhdr[b] & 8u) << 1);
        eng.last[1] = (uint8_t)(CB->txdhdr[b] >> 8);
        memcpy(eng.last + 2, bb.tx[b].buf + HW_SWHDR, eng.last[1] < 38u ? eng.last[1] : 38u);
        eng.last_b = (int)b;
        eng.sends[b] += eng.last[1] != 0;               /* (data PDUs only) */
    } else {
        eng.last[0] = 1, eng.last[1] = 0;
        eng.last_b = -1;
    }
    eng.in_flight = 1;
    if (!eng.sent_any)
        CB->txtog |= 6u;                                                /* 7 after the first packet (blell4) */
    eng.sent_any = 1;
}

/* one connection event: the central's packet (its NESN acknowledges our last, its SN is new or a repeat), the
 * engine's answer, the central reads it */
static void eng_event(uint32_t rb, int c_lost, int p_lost)
{
    uint8_t llid = cen.cur_ok ? cen.cur_llid : 1u, n = cen.cur_ok ? cen.cur_len : 0u;
    uint16_t h;
    const uint8_t *q;
    if (c_lost)
        return;                                                         /* the engine heard nothing: no answer */
    if (eng.in_flight && cen.nesn != eng.tsn) {                         /* our last acknowledged */
        if (eng.last_b >= 0) {
            CB->txbufcntl[eng.last_b] |= 1u;                            /* finished: empty again */
            CB->txtog = (uint16_t)((CB->txtog & ~1u) | (uint32_t)(eng.last_b ^ 1));
        }
        eng.tsn ^= 1;
        eng.in_flight = 0;
    }
    if (cen.sn == eng.nesn) {                                           /* new: into RXTOG's buffer */
        uint8_t *p = bb.rx[rb].buf + HW_SWHDR;
        eng.nesn ^= 1;
        memcpy(p, cen.cur, n);
        CB->rxdhdr[rb] = (uint16_t)(n << 8 | (uint32_t)cen.sn << 3 | (uint32_t)cen.nesn << 2 | llid);
        CB->rxstat[rb] = 0x9401u;
        CB->rxbufcntl[rb] |= 1u;
        CB->rxtog ^= 1u;
    }                                                                   /* (a repeat: dropped by the engine) */
    eng_send();
    if (p_lost)
        return;
    h = (uint16_t)(eng.last[1] << 8 | (uint32_t)eng.tsn << 3 | (uint32_t)eng.nesn << 2 | (eng.last[0] & 0x13u));
    q = eng.last + 2;
    if ((h >> 3 & 1u) == (uint32_t)cen.nesn) {                          /* the central: new from us */
        cen.nesn ^= 1;
        cen_rx(h, q);
    } else if (eng.last[1])
        cen.dups++;                                                     /* (a retransmission: the central drops it) */
    if ((h >> 2 & 1u) != (uint32_t)cen.sn) {                            /* ours acknowledged its packet */
        cen.sn ^= 1;
        cen.cur_ok = 0;
    }
}

static void cen_next(void)              /* the central's next PDU, once the last one was acknowledged */
{
    if (cen.cur_ok || !cen.qn)
        return;
    memcpy(cen.cur, cen.q[0], cen.qlen[0]);
    cen.cur_len = cen.qlen[0];
    cen.cur_llid = cen.qllid[0];
    cen.cur_ok = 1;
    memmove(cen.q, cen.q + 1, sizeof cen.q[0] * 7);
    memmove(cen.qlen, cen.qlen + 1, 7);
    memmove(cen.qllid, cen.qllid + 1, 7);
    cen.qn--;
}

/* one connection event: the central's packet, the engine's answer, the RX IRQ, the event IRQ */
static void event(uint32_t e)
{
    uint32_t b = CB->rxtog & 1u;
    int loss = (cen.opt & O_LOSS) != 0;
    cen.e = e;
    if (cen.kind == C_MAC && cen.len_req_out == 1 && !cen.len_rsp_rx && !cen.terminated) {
        if (!cen.len_req_evt)
            cen.len_req_evt = e;
        else if (e - cen.len_req_evt >= MAC_LEN_WAIT) {                /* blell8: TERMINATE_IND, 0x13 */
            static const uint8_t term[2] = {LL_TERMINATE_IND, 0x13};
            cen_ctrl(term, 2);
            cen.terminated = 1;
        }
    }
    if (cen.kind == C_MAC && e == MIDI_EVT)
        app.on = 1;
    cen_next();
    eng_event(b, loss && e % 7u == 3u, loss && e % 11u == 5u);
    eng_save();
    ble_wl82_rx_irq();
    eng_watch(0);
    if (cen.opt & O_OLDPOL)
        old_polarity();
    fk.slots += 2u * IV;
    if (e % 97u == 96u)
        fk.slots -= 267u;                                               /* the FM-1's backward step */
    fk.ticks += TICKS_PER_EVT;
    fk.col3 = e + 1u;
    eng_save();
    ble_wl82_event_irq();
    eng_watch(1);
}

static void run(int kind, int opt, uint32_t ticks0, uint32_t events, const char *name)
{
    static const uint8_t addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
    static const uint8_t ver[6] = {LL_VERSION_IND, 0x0C, 0x0F, 0x00, 0x00, 0x01};
    static const uint8_t feat[9] = {LL_FEATURE_REQ, 0x3F};              /* the Mac's: DLE among them */
    struct ble_rf_trims tr;
    uint32_t e, t_conn;
    int loss = (opt & O_LOSS) != 0;
    char what[220];
    memset(&cen, 0, sizeof cen);
    memset(&eng, 0, sizeof eng);
    memset(&app, 0, sizeof app);
#if BLE_SMP_LEGACY
    bond_n = 0;
#endif
    memset(&fk, 0, sizeof fk);
    memset(&tr, 0, sizeof tr);
#if BLE_DIAG
    ble_diag_clear();
#endif
    fk.ticks = ticks0;
    fk.tick_per_read = FM1_TICKS_PER_US;
    cen.kind = kind;
    cen.opt = opt;
    ble_hw_wl82_start(&tr);
    ble_init(addr, 1);
    ble_enable(1);
    fk.ticks += 50000u * FM1_TICKS_PER_US;
    cind();
    snprintf(what, sizeof what, "%s: CONNECT_IND taken, found in the buffer RXTOG moved past (CNTL 0)", name);
    check(what, ble_ll_connected() && DG(ble_dg.cind_ok == 1 && ble_dg.rxf_tog_prev == 1 && ble_dg.rxf_cntl == 0));
    snprintf(what, sizeof what, "%s: set-up: both TXBUFnCNTL bit0 = 1, TXDHDR %04X / %04X (TXTOG's bit2 0), none "
             "recorded", name, CB->txdhdr[0], CB->txdhdr[1]);
    check(what, (CB->txbufcntl[0] & 1u) && (CB->txbufcntl[1] & 1u) && CB->txdhdr[1] == 0x0001u && CB->txdhdr[0] == 0x0005u &&
                    !CB->txahdr[0] && !CB->txahdr[1] && !drv.tx_rec[0] && !drv.tx_rec[1]);
    t_conn = fk.ticks;
    memset(fk.col_reads, 0, sizeof fk.col_reads);
    cen_ctrl(ver, 6);
    if (kind == C_MAC) {                                                /* blell8: FEATURE_REQ two events later */
        cen_pdu(1, feat, 0);
        cen_ctrl(feat, 9);
    }
    for (e = 0; e < events && ble_ll_connected(); e++)
        event(e);
    if (opt & O_OLDPOL) {
        snprintf(what, sizeof what, "%s: stalls: %u bit0 = 1 writes on loaded buffers, VERSION_IND %d, FEATURE_RSP %d, "
                 "data PDUs sent %u", name, (unsigned)eng.old_writes, cen.ver_rx, cen.feat_rsp_rx,
                 (unsigned)(eng.sends[0] + eng.sends[1]));
        check(what, eng.old_writes > 0 && cen.ver_rx == 0 && cen.feat_rsp_rx == 0 && cen.len_rsp_rx == 0 &&
                        eng.sends[0] + eng.sends[1] == 0);
        ble_enable(0);
        return;
    }
    snprintf(what, sizeof what, "%s: our VERSION_IND once; queued %u, acked %u, ack within %u events", name,
             (unsigned)DGV(ble_dg.tx_queued), (unsigned)DGV(ble_dg.tx_acked), (unsigned)DGV(ble_dg.tx_ack_evt_max));
    check(what, cen.ver_rx == 1 && DG(ble_dg.tx_acked + 1u >= ble_dg.tx_queued && ble_dg.tx_acked <= ble_dg.tx_queued &&
                    ble_dg.tx_ack_evt_max <= (loss ? 8u : 4u)));
    snprintf(what, sizeof what, "%s: the driver kept the contract (bit0=1 %u, TXTOG %u, bit2 %u, held touched %u, "
             "event IRQ loads %u)", name, (unsigned)eng.sw_set1, (unsigned)eng.sw_tog, (unsigned)eng.sw_bit2,
             (unsigned)eng.sw_touch, (unsigned)eng.evt_load);
    check(what, !eng.sw_set1 && !eng.sw_tog && !eng.sw_bit2 && !eng.sw_touch && !eng.evt_load);
    snprintf(what, sizeof what, "%s: txsnaps: first a load (b %u, snap %u), the TXTOG / bit0 values kept; held %u",
             name, DGV(ble_dg.txs_first.b), DGV(ble_dg.txs_first.snap), (unsigned)DGV(ble_dg.tx_eng_held));
    check(what, DG(ble_dg.txs_n >= 2u && ble_dg.txs_first.what == BTX_LOAD &&
                    ((opt & O_QUIRK) ? ble_dg.tx_eng_held >= 1u : ble_dg.tx_eng_held == 0u)));
    if (kind == C_MAC) {
        snprintf(what, sizeof what, "%s: FEATURE_RSP %d, LENGTH_RSP %d (Mac's LENGTH_REQ in event %u), MTU / group / "
                 "write responses %d / %d / %d", name, cen.feat_rsp_rx, cen.len_rsp_rx, (unsigned)cen.len_req_evt,
                 cen.mtu_rsp_rx, cen.group_rsp_rx, cen.write_rsp_rx);
        check(what, cen.feat_rsp_rx == 1 && cen.len_rsp_rx == 1 && cen.mtu_rsp_rx == 1 && cen.group_rsp_rx == 2 &&
                        cen.write_rsp_rx == 1 && cen.order_bad == 0 && DG(ble_dg.tx_queued >= 7 &&
                        ble_dg.tx_acked >= 7));
        snprintf(what, sizeof what, "%s: the Mac's discovery: services %d (MIDI %u-%u), Service Changed at %u (CCCD %u), "
                 "Database Hash: error %02X", name, cen.nsvc, cen.midi_s, cen.midi_e, cen.sc_val, cen.sc_ccc,
                 cen.hash_err);
        check(what, cen.nsvc == 3 && cen.midi_s == 12 && cen.midi_e == 15 && cen.sc_val == 10 && cen.sc_ccc == 11 &&
                        cen.hash_err == 0x0A);
        snprintf(what, sizeof what, "%s: no Service Changed indication after its CCCD (the stall of blell10-12): %d",
                 name, cen.sc_ind);
        check(what, cen.sc_ind == 0 && cen.st == M_DONE);
        snprintf(what, sizeof what, "%s: CoreMIDI's part: MIDI I/O at %u (props %02X), read empty %d, CCCD %u on, "
                 "parameter request answered %d, unexpected %d", name, cen.midi_val, cen.midi_props,
                 cen.midi_read_empty, cen.midi_ccc, cen.cpup_rsp, cen.att_bad);
        check(what, cen.midi_val == 14 && cen.midi_props == 0x16 && cen.midi_read_empty && cen.midi_ccc == 15 &&
                        cen.cpup_rsp == 1 && cen.att_bad == 0 && bhs.fast == 3);
        snprintf(what, sizeof what, "%s: the Mac's note (Write Command) reached the synth: %u", name,
                 (unsigned)app.midi_in);
        check(what, app.midi_in == 1);
#if BLE_SMP_LEGACY
        snprintf(what, sizeof what, "%s: paired (legacy Just Works) on Insufficient Authentication %d, encrypted "
                 "%d, keys %d, bond kept %d, LL encryption on %u", name, cen.auth_err, cen.enc_on, cen.keys_rx,
                 bond_n, (unsigned)DGV(ble_dg.enc_on_n));
        check(what, (!BLE_MIDI_NEED_ENC || cen.auth_err == 1) && cen.paired == BLE_MIDI_NEED_ENC &&
                        cen.enc_on == BLE_MIDI_NEED_ENC && cen.keys_rx == 2 * BLE_MIDI_NEED_ENC &&
                        bond_n == BLE_MIDI_NEED_ENC && ble_ll_encrypted() == BLE_MIDI_NEED_ENC);
#endif
        snprintf(what, sizeof what, "%s: a MIDI notification out (%d, event %u, asked in %u)", name, cen.notif_rx,
                 (unsigned)cen.notif_evt, MIDI_EVT);
        check(what, cen.notif_rx == 1 && cen.notif_evt >= MIDI_EVT && cen.notif_evt <= MIDI_EVT + (loss ? 8u : 4u));
        snprintf(what, sizeof what, "%s: the LENGTH_RSP within %u events of the LENGTH_REQ", name,
                 (unsigned)(cen.mtu_req_evt - cen.len_req_evt));
        check(what, cen.mtu_req_evt >= cen.len_req_evt && cen.mtu_req_evt - cen.len_req_evt <= (loss ? 8u : 4u));
    } else if (kind == C_WAITS) {
        snprintf(what, sizeof what, "%s: our PERIPHERAL_FEATURE_REQ out (%d)", name, cen.feat_req_rx);
        check(what, cen.feat_req_rx == 1 && DG(ble_dg.tx_queued >= 2 && ble_dg.tx_acked >= 2 && ble_dg.ctl_tx_n >= 2));
    }
    snprintf(what, sizeof what, "%s: no column 0 / 14 (slot clock) read from the ISRs over %u events", name,
             (unsigned)e);
    check(what, fk.col_reads[0] == 0 && fk.col_reads[14] == 0);
    if (!loss) {
        snprintf(what, sizeof what, "%s: RX in a connection: CNTL bit0, RXTOG past it (%u / %u)", name,
                 (unsigned)DGV(ble_dg.rxc_tog_past), (unsigned)DGV(ble_dg.rxc_tog_at));
        check(what, DG(ble_dg.rx_desync == 0 && ble_dg.rxc_tog_past >= e - 1u && ble_dg.rxc_tog_at == 0));
    }
    if (kind != C_SILENT) {
        snprintf(what, sizeof what, "%s: the link still up after %u s (procedure timeout 40 s, supervision 720 ms)",
                 name, (unsigned)((fk.ticks - t_conn) / 24000000u));
        check(what, ble_ll_connected() && DG(ble_dg.closes == 0) && e == events && !cen.terminated &&
                        (fk.ticks - t_conn) / 24000000u >= 60u);
    } else {
        uint32_t s = (fk.ticks - t_conn) / 24000u;
        snprintf(what, sizeof what, "%s: no FEATURE_RSP: LL response timeout 0x22 at 40 s (%u ms after connect)", name,
                 (unsigned)s);
        check(what, !ble_ll_connected() && DG(ble_dg.closes == 1 && ble_dg.close_reason == BLE_ERR_LL_RSP_TIMEOUT &&
                        ble_dg.close_since_start_us >= 40000000u && ble_dg.close_since_start_us <= 40400000u) &&
                        s >= 40000u && s <= 40400u);
    }
    ble_enable(0);
}

int main(void)
{
    run(C_MAC, O_QUIRK, 1000u, 2100u, "§8.2 engine (FM-1 first-event quirk), a Mac");
    run(C_MAC, 0, 1000u, 2100u, "§8.2 engine (the emulator's model), a Mac");
    run(C_MAC, O_QUIRK | O_LOSS, 1000u, 2100u, "§8.2 engine, a Mac, packets lost both ways");
    run(C_WAITS, O_QUIRK, 1000u, 2100u, "§8.2 engine, a central that waits");
    run(C_MAC, O_QUIRK, 0xFFFFFFFFu - 10u * 24000000u, 2100u, "§8.2 engine, TIMER4 wraps 10 s in");
    run(C_SILENT, O_QUIRK, 5000u, 2100u, "§8.2 engine, the central silent");
    run(C_MAC, O_QUIRK | O_OLDPOL, 1000u, 300u, "§8.2 engine, the previous drivers' polarity (bit0 = 1 = loaded)");
    printf("%s\n", fails ? "BLE driver: FAILED" : "BLE driver: all passed");
    return fails != 0;
}
