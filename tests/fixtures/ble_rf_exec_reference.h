/* SPDX-License-Identifier: GPL-3.0-only */
/* Unchanged Optimist RF bytecode interpreter (symbol renamed for comparison).
 * w0ts/optimist 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66,
 * firmware/hal/fm1_ble_rf.h. Retains its one-level recursive replay. */
static void rf_reference(uint32_t pc, uint32_t end, const uint8_t *const rec[4], int depth)
{
    const uint8_t *p = ble_rf_prog;
    uint8_t trim[8], rmw = 0, nt = 0;
    uint32_t last = 0, v, a, i, n;
    while (pc < end) {
        uint32_t op = p[pc];
        fm1_ble_rf_tick();
        if (op < 0x80u) {                                   /* a register write */
            a = ble_rf_addr[op];
            v = fm1_rf_u32(p + pc + 1);
            fm1_ble_bc.addr = a;
            if (a == FM1_WL_A00)
                v = (v & ~(1u << 14)) | (FM1_RF_REG(a) & 1u << 14);
            for (i = 0; i < nt; i++)
                v = fm1_rf_field(v, trim[i], rec);
            FM1_RF_REG(a) = v;
            nt = rmw = 0;
            pc += 5;
        } else if ((op & 0xFCu) == 0x80u) {                 /* a window write */
            v = rmw ? last : p[pc + 2];
            for (i = 0; i < nt; i++)
                v = fm1_rf_field(v, trim[i], rec);
            fm1_bbp_win_wr(op & 3u, p[pc + 1], v);
            nt = rmw = 0;
            pc += 3;
        } else if ((op & 0xFCu) == 0x84u) {                 /* a window read */
            last = fm1_bbp_win_rd(op & 3u, p[pc + 1]);
            pc += 2;
        } else if (op == 0x88u) {
            fm1_bbp(p[pc + 1], p[pc + 2], 0);
            pc += 3;
        } else if (op == 0x89u) {
            last = fm1_bbp(p[pc + 1], 0, 1);
            pc += 2;
        } else if (op == 0x8Au) {                           /* LUT words: command, first entry, count, words */
            n = p[pc + 3];
            for (i = 0; i < n; i++)
                fm1_rfspi(p[pc + 2] + i, p[pc + 1], fm1_rf_u32(p + pc + 4 + 4u * i));
            fm1_ble_rf_stat.lut_words += n;
            pc += 4u + 4u * n;
        } else if (op == 0x8Bu) {                           /* a trim field for the next write */
            if (nt < sizeof trim && p[pc + 1] < BLE_RF_NFIELDS)
                trim[nt++] = p[pc + 1];
            else
                fm1_ble_rf_stat.bad_op = (uint8_t)op;
            rmw |= p[pc + 2] & 1u;
            fm1_ble_rf_stat.trims++;
            pc += 3;
        } else if (op == 0x8Cu) {
            n = p[pc + 1] | p[pc + 2] << 8;
            fm1_delay_us(n);
            fm1_ble_rf_stat.delay_us += n;
            pc += 3;
        } else if (op == 0x8Du) {
            fm1_ble_vco_scan(fm1_rf_u32(p + pc + 1));
            pc += 5;
        } else if (op == 0x8Eu) {
            a = p[pc + 1] | p[pc + 2] << 8;
            n = p[pc + 3] | p[pc + 4] << 8;
            if (!depth && a + n <= sizeof ble_rf_prog)
                rf_reference(a, a + n, rec, 1);
            else
                fm1_ble_rf_stat.bad_op = (uint8_t)op;
            pc += 5;
        } else if (op == 0x8Fu) {
            fm1_ble_rf_stat.skipped += p[pc + 1] | p[pc + 2] << 8;   /* TODO(hardware): the read-back loop */
            pc += 3;
        } else if (op == 0x91u) {                           /* the LUT words of two commands (packed) */
            pc += fm1_ble_rf_lut2(p + pc);
        } else if (op == 0x92u) {                           /* window writes repeating n entries (packed) */
            pc += fm1_ble_rf_wwg(p + pc);
            nt = rmw = 0;
        } else if (op == 0x90u) {                           /* a section marker (§16.1 group) */
            fm1_ble_rf_section(p[pc + 1]);
            pc += 2;
            if (p[pc - 1] < 32u && (FM1_RF_BURST_GROUPS >> p[pc - 1] & 1u))
                pc = fm1_ble_rf_burst(pc, end, rec);        /* (unchanged: run op by op below) */
        } else {                                            /* 0xFF: the end (anything else: stop there) */
            if (op != 0xFFu)
                fm1_ble_rf_stat.bad_op = (uint8_t)op;
            return;
        }
    }
}
