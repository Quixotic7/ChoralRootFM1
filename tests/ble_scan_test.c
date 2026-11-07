/* SPDX-License-Identifier: GPL-3.0-only */
/* The scanner's software half (firmware/src/ble/ble_scan.c; docs/BLE-DEVICES-DESIGN.md §2, docs/BLE-STACK.md §9.1):
 *   AD parser  flags, 128-bit UUID lists (complete / incomplete, the BLE-MIDI UUID among others), the complete and the
 *              shortened name (complete wins), a structure that runs past the end, zero padding
 *   table      a BLE-MIDI controller with its name in the scan response, a beacon never listed, a device with the UUID
 *              only in its scan response, duplicates, a non-connectable or directed advertiser ignored, another FM-1
 *              (KIND), a random and a public address with the same octets kept apart, the names as the font shows them
 *              (UTF-8 Latin-1, others '?', cut to 16), the list's order kept as devices come and go, ageing (10 s,
 *              unconfirmed 3 s), a full table (unconfirmed ones go first; a listed one only after 2 s unheard)
 *   bars       relative: by the RSSI word when every listed device has one, else by how often it was heard
 * Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BLE_API static
#include "../firmware/src/ble/ble_scan.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("ble_scan: %-104s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static const uint8_t U[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                              0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};
static uint8_t pdu[64];
static uint32_t plen;

/* a PDU: type (| 0x40 TxAdd), AdvA = {a0, 0x11, 0x22, 0x33, 0x44, 0xC5}, then the AD given */
static void mk(uint8_t hdr, uint8_t a0, const uint8_t *ad, uint32_t n)
{
    pdu[0] = hdr;
    pdu[1] = (uint8_t)(6u + n);
    pdu[2] = a0, pdu[3] = 0x11, pdu[4] = 0x22, pdu[5] = 0x33, pdu[6] = 0x44, pdu[7] = 0xC5;
    memcpy(pdu + 8, ad, n);
    plen = 8u + n;
}
static uint32_t ad_name(uint8_t *ad, uint8_t type, const char *s)
{
    uint32_t l = (uint32_t)strlen(s);
    ad[0] = (uint8_t)(l + 1u);
    ad[1] = type;
    memcpy(ad + 2, s, l);
    return l + 2u;
}
static uint32_t ad_midi(uint8_t *ad, uint8_t type)
{
    ad[0] = 17;
    ad[1] = type;
    memcpy(ad + 2, U, 16);
    return 18;
}
static void add(struct ble_scan_tab *t, uint8_t hdr, uint8_t a0, int midi, const char *name, uint16_t rssi, uint32_t ms)
{
    uint8_t ad[31];
    uint32_t n = 0;
    if (midi)
        n += ad_midi(ad + n, 0x07);
    if (name)
        n += ad_name(ad + n, 0x09, name);
    mk(hdr, a0, ad, n);
    ble_scan_add(t, pdu, plen, rssi, ms);
}
static uint32_t listed(const struct ble_scan_tab *t, uint8_t *idx) { return ble_scan_list(t, idx); }

static void t_parse(void)
{
    struct ble_ad a;
    uint8_t ad[48], other[16];
    uint32_t n = 0;
    memset(other, 0x42, sizeof other);
    ad[n++] = 2, ad[n++] = 0x01, ad[n++] = 0x06;
    n += ad_name(ad + n, 0x08, "Key");
    ad[n++] = 17, ad[n++] = 0x06;
    memcpy(ad + n, other, 16), n += 16;
    ble_ad_parse(ad, n, &a);
    check("flags 0x06, a shortened name, another 128-bit UUID: no BLE-MIDI",
          a.has_flags && a.flags == 6u && a.name_type == 0x08u && a.name_len == 3u && !memcmp(a.name, "Key", 3) &&
          !a.midi && !a.bad);
    n = 0;
    ad[n++] = 33, ad[n++] = 0x07;
    memcpy(ad + n, other, 16), n += 16;
    memcpy(ad + n, U, 16), n += 16;
    ble_ad_parse(ad, n, &a);
    check("a complete list of two 128-bit UUIDs, BLE-MIDI the second", a.midi && !a.bad);
    n = ad_name(ad, 0x08, "Ke");
    n += ad_name(ad + n, 0x09, "KeyStep 37");
    n += ad_name(ad + n, 0x08, "K");
    ble_ad_parse(ad, n, &a);
    check("the complete name wins over shortened ones before and after it",
          a.name_type == 0x09u && a.name_len == 10u && !memcmp(a.name, "KeyStep 37", 10));
    n = ad_midi(ad, 0x06);
    ad[n++] = 0;                                 /* padding: the significant part ends */
    ad[n++] = 0x55;
    ble_ad_parse(ad, n, &a);
    check("an incomplete list (0x06) counts; zero padding ends the parse", a.midi && !a.bad);
    n = 0;
    ad[n++] = 2, ad[n++] = 0x01, ad[n++] = 0x06;
    ad[n++] = 17, ad[n++] = 0x07;
    memcpy(ad + n, U, 10), n += 10;              /* cut short */
    ble_ad_parse(ad, n, &a);
    check("a structure running past the end: flagged, the rest ignored, what came before kept",
          a.bad && !a.midi && a.has_flags);
}

static void t_table(void)
{
    static struct ble_scan_tab t;
    uint8_t idx[BLE_SCAN_N], ad[31];
    uint32_t n, gen, k;
    ble_scan_clear(&t);
    add(&t, 0x40, 0x01, 1, 0, 0x0A10, 100);      /* ADV_IND with the UUID, the name to come */
    n = listed(&t, idx);
    check("a BLE-MIDI controller's ADV_IND: listed at once, no name yet", n == 1u && t.e[idx[0]].name[0] == 0 &&
          t.e[idx[0]].addr_rand == 1u && t.e[idx[0]].addr[0] == 0x01u && t.e[idx[0]].kind == BLE_KIND_MIDI);
    gen = t.gen;
    add(&t, 0x44, 0x01, 0, "KeyStep 37", 0x0A10, 110);   /* its SCAN_RSP */
    check("its SCAN_RSP gives the name; the list says it changed (gen)",
          !strcmp(t.e[idx[0]].name, "KeyStep 37") && t.gen != gen);
    add(&t, 0x40, 0x02, 0, "Tile", 0x0300, 120);
    add(&t, 0x44, 0x02, 0, "Tile", 0x0300, 121);
    check("a beacon (no BLE-MIDI UUID): kept unconfirmed, never listed", listed(&t, idx) == 1u && t.kept == 4u);
    add(&t, 0x40, 0x03, 0, 0, 0x0800, 130);      /* the UUID only in the scan response */
    add(&t, 0x44, 0x03, 1, "nanoKEY", 0x0800, 131);
    n = listed(&t, idx);
    check("the UUID only in the scan response: listed then, after the first one", n == 2u &&
          !strcmp(t.e[idx[1]].name, "nanoKEY"));
    add(&t, 0x44, 0x09, 1, "Ghost", 0, 140);
    check("a scan response from an advertiser never seen: nothing", listed(&t, idx) == 2u);
    add(&t, 0x42, 0x0A, 1, "NonConn", 0, 141);   /* ADV_NONCONN_IND */
    add(&t, 0x41, 0x0B, 1, "Direct", 0, 142);    /* ADV_DIRECT_IND */
    add(&t, 0x46, 0x0C, 1, "ScanInd", 0, 143);   /* ADV_SCAN_IND: not connectable */
    check("non-connectable and directed advertisers: never entries", listed(&t, idx) == 2u && t.ignored_type == 3u);
    add(&t, 0x40, 0x01, 1, 0, 0x0A10, 150);
    add(&t, 0x40, 0x01, 1, 0, 0x0A10, 160);
    check("duplicates: the same entry heard again (hits), no new row", listed(&t, idx) == 2u &&
          t.e[idx[0]].hits >= 3u && t.e[idx[0]].seen_ms == 160u);
    add(&t, 0x00, 0x01, 1, "Public twin", 0x0A10, 170);
    n = listed(&t, idx);
    check("the same octets as a public address: another device", n == 3u && t.e[idx[2]].addr_rand == 0u);
    n = 0;
    n += ad_midi(ad, 0x07);
    mk(0x40, 0x0D, ad, n);
    ble_scan_add(&t, pdu, plen, 0x0700, 180);
    n = ad_name(ad, 0x09, "Optimist A1B2");     /* (the name in the SCAN_RSP, as ours: the two do not fit one 31-octet payload) */
    mk(0x44, 0x0D, ad, n);
    ble_scan_add(&t, pdu, plen, 0x0700, 181);
    n = listed(&t, idx);
    check("another FM-1 (\"Optimist \" name): KIND FM-1", n == 4u && t.e[idx[3]].kind == (BLE_KIND_MIDI | BLE_KIND_FM1) &&
          !strcmp(t.e[idx[3]].name, "Optimist A1B2"));
    n = ad_name(ad, 0x09, "FM-1 A1B2");
    mk(0x44, 0x0D, ad, n);
    ble_scan_add(&t, pdu, plen, 0x0700, 182);
    n = listed(&t, idx);
    check("the same device under the name of the builds before the rename (\"FM-1 \"): still KIND FM-1",
          n == 4u && t.e[idx[3]].kind == (BLE_KIND_MIDI | BLE_KIND_FM1));
    {
        static const uint8_t utf[] = {'C', 'a', 'f', 0xC3, 0xA9, ' ', 0xE2, 0x82, 0xAC, ' ', 'L', 'o', 'n', 'g',
                                      'e', 'r', ' ', 'N', 'a', 'm', 'e'};
        n = ad_midi(ad, 0x07);
        mk(0x40, 0x0E, ad, n);
        ble_scan_add(&t, pdu, plen, 0x0700, 190);
        ad[0] = (uint8_t)(sizeof utf + 1u), ad[1] = 0x09;
        memcpy(ad + 2, utf, sizeof utf);
        mk(0x44, 0x0E, ad, sizeof utf + 2u);
        ble_scan_add(&t, pdu, plen, 0x0700, 191);
        n = listed(&t, idx);
        check("a UTF-8 name: e-acute as Latin-1, the euro sign '?', cut to 16",
              n == 5u && !memcmp(t.e[idx[4]].name, "Caf\xE9 ? Longer Na", 16) && strlen(t.e[idx[4]].name) == 16u);
    }
    mk(0x40, 0x0F, ad, 0);
    pdu[1] = 40;                                 /* longer than any advertising payload */
    ble_scan_add(&t, pdu, 48, 0, 200);
    mk(0x40, 0x0F, ad, 0);
    ble_scan_add(&t, pdu, 5, 0, 200);
    check("malformed PDUs (length 40, cut short): counted, nothing kept", t.bad_ad == 2u && listed(&t, idx) == 5u);
    /* ageing: the controller heard again at 9 s, the rest not */
    add(&t, 0x40, 0x01, 1, 0, 0x0A10, 9000);
    ble_scan_age(&t, 9500);
    for (k = 0, n = 0; k < BLE_SCAN_N; k++)
        n += t.e[k].used && t.e[k].addr[0] == 0x02u;
    check("ageing at 9.5 s: listed ones stay (10 s), the beacon (unconfirmed, 3 s) is gone", listed(&t, idx) == 5u &&
          n == 0u);
    gen = t.gen;
    ble_scan_age(&t, 10500);
    n = listed(&t, idx);
    check("at 10.5 s: those not heard since the start leave; the one heard at 9 s stays, first",
          n == 1u && t.e[idx[0]].addr[0] == 0x01u && t.gen != gen);
    add(&t, 0x40, 0x20, 1, "Late", 0, 10600);
    n = listed(&t, idx);
    check("a new device goes to the bottom of the list (the cursor's rows do not jump)",
          n == 2u && !strcmp(t.e[idx[1]].name, "Late") && t.e[idx[0]].addr[0] == 0x01u);
    /* full table */
    ble_scan_clear(&t);
    for (k = 0; k < BLE_SCAN_N; k++)
        add(&t, 0x40, (uint8_t)(0x30 + k), k < 6u, 0, 0, 20000u + k);   /* 6 listed, 2 unconfirmed */
    add(&t, 0x40, 0x50, 1, 0, 0, 20010);
    check("full: a new BLE-MIDI device takes the oldest unconfirmed slot", listed(&t, idx) == 7u && t.dropped_full == 0u);
    add(&t, 0x40, 0x51, 0, 0, 0, 20011);
    add(&t, 0x40, 0x52, 0, 0, 0, 20012);
    check("full: unconfirmed newcomers replace each other, never a listed one", listed(&t, idx) == 7u);
    for (k = 0; k < BLE_SCAN_N; k++)
        if (t.e[k].used && !t.e[k].midi)
            add(&t, 0x44, t.e[k].addr[0], 1, 0, 0, 20013);   /* (its scan response: listed now, the table all listed) */
    add(&t, 0x40, 0x53, 1, 0, 0, 21000);
    check("full of listed ones heard within 2 s: a newcomer is dropped (counted)", listed(&t, idx) == 8u &&
          t.dropped_full == 1u);
    add(&t, 0x40, 0x54, 1, 0, 0, 22500);
    n = listed(&t, idx);
    check("... and after 2 s unheard the oldest listed one gives its place", n == 8u && t.e[idx[7]].addr[0] == 0x54u);
}

static void t_bars(void)
{
    static struct ble_scan_tab t;
    uint8_t idx[BLE_SCAN_N];
    ble_scan_clear(&t);
    add(&t, 0x40, 0x01, 1, "Near", 0x0405, 10);  /* fine 5, gain 4: strong */
    add(&t, 0x40, 0x02, 1, "Mid", 0x1210, 11);
    add(&t, 0x40, 0x03, 1, "Far", 0x2A30, 12);   /* fine 48, gain 42: weak */
    ble_scan_list(&t, idx);
    check("bars by RSSI: strongest 3, middle 2, weakest 1",
          ble_scan_bars(&t, idx[0]) == 3u && ble_scan_bars(&t, idx[1]) == 2u && ble_scan_bars(&t, idx[2]) == 1u);
    check("a strength from the word (no gain table: a smaller fine part / gain index is stronger)",
          ble_scan_strength(0x0405) > ble_scan_strength(0x1210) && ble_scan_strength(0) == 255u);
    check("an entry not listed: no bars", ble_scan_bars(&t, 7) == 0u);
    ble_scan_clear(&t);
    add(&t, 0x40, 0x01, 1, "Often", 0, 10);
    add(&t, 0x40, 0x01, 1, "Often", 0, 11);
    add(&t, 0x40, 0x01, 1, "Often", 0, 12);
    add(&t, 0x40, 0x02, 1, "Once", 0, 13);
    ble_scan_list(&t, idx);
    check("no RSSI word: bars by how often each was heard (the counter)",
          ble_scan_bars(&t, idx[0]) == 3u && ble_scan_bars(&t, idx[1]) == 1u);
    ble_scan_clear(&t);
    add(&t, 0x40, 0x01, 1, "Alone", 0x2A30, 10);
    ble_scan_list(&t, idx);
    check("one device alone: 3 bars", ble_scan_bars(&t, idx[0]) == 3u);
}

int main(void)
{
    t_parse();
    t_table();
    t_bars();
    printf("%s\n", fails ? "ble_scan: FAILED" : "ble_scan: all passed");
    return fails != 0;
}
