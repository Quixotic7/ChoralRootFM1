#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Compare the shipping iterative RF interpreter with the pinned upstream one.

The recorded program runs through a deterministic register/transaction sink.
This proves replay/trim/control-flow equivalence, not analog RF behavior.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'firmware/hal/fm1_ble_rf.h').read_text()


def function(name):
    start = SOURCE.index('static ', SOURCE.index(name) - 32)
    brace = SOURCE.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (SOURCE[end] == '{') - (SOURCE[end] == '}')
        end += 1
    return SOURCE[start:end]


PRELUDE = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "firmware/hal/ble_rf_tables.h"
#define FM1_WL_A00 0x11900u
#define FM1_RF_BURST_GROUPS (1u << 5)
static struct { uint32_t addr; } fm1_ble_bc;
static struct { uint32_t ops,trims,skipped,delay_us,lut_words; uint8_t bad_op; } fm1_ble_rf_stat;
static uint64_t digest;
static uint32_t addresses[1024], values[1024], count;
static void record(uint32_t a,uint32_t b,uint32_t c) {
    uint32_t v[3]={a,b,c};
    for(unsigned i=0;i<3;i++){digest^=v[i];digest*=1099511628211ULL;}
}
static uint32_t *reg(uint32_t a) {
    record(1,a,0);unsigned i=0;for(;i<count&&addresses[i]!=a;i++);
    assert(i<1024);if(i==count)addresses[count++]=a;return values+i;
}
#define FM1_RF_REG(a) (*reg(a))
static uint32_t fm1_rf_u32(const uint8_t *p) {return p[0]|p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t fm1_rf_field(uint32_t v,uint32_t field,const uint8_t *const rec[4]) {
    return v ^ ((uint32_t)rec[field%4][field%2] << (field%24));
}
static void fm1_ble_rf_tick(void) {fm1_ble_rf_stat.ops++;}
static void fm1_bbp_win_wr(uint32_t w,uint32_t e,uint32_t v){record(2+w,e,v);}
static uint32_t fm1_bbp_win_rd(uint32_t w,uint32_t e){record(7,w,e);return (w*13+e)&255;}
static uint32_t fm1_bbp(uint32_t r,uint32_t v,uint32_t rd){record(8,r,v|rd<<16);return (r*7+v)&255;}
static void fm1_rfspi(uint32_t a,uint32_t cmd,uint32_t v){record(9,a|cmd<<16,v);}
static void fm1_delay_us(uint32_t n){record(10,n,0);}
static void fm1_ble_vco_scan(uint32_t n){record(11,n,0);}
static void fm1_ble_rf_section(uint32_t g){record(12,g,0);}
static uint32_t fm1_ble_rf_burst(uint32_t pc,uint32_t end,const uint8_t *const rec[4]){
    (void)end;(void)rec;return pc; /* same writes, without the timing-only SRAM burst */
}
/* the measured calibration (hal/fm1_rf_cal.h) off, as with a stored calibration: the replay must be the upstream one */
static uint8_t fm1_rf_cal_on, fm1_rfc_scanned, fm1_rfc_trimmed, fm1_rfc_x187[68];
static int fm1_rfc_scan_rc;
static struct { uint8_t bank121; } fm1_rfc;
static uint32_t fm1_wl_vco_band;
#define FM1_WL_VCO_BAND fm1_wl_vco_band
static int fm1_rfc_is187(const uint8_t *t,uint32_t n){(void)t;(void)n;return 0;}
static void fm1_rfc_before_187(void){}
static uint32_t fm1_rf_cal_reg(uint32_t a,uint32_t v,const uint8_t *x){(void)a;(void)x;return v;}
static uint32_t fm1_rf_cal_win(uint32_t w,uint32_t e,uint32_t v,const uint8_t *x){(void)w;(void)e;(void)x;return v;}
static uint32_t fm1_rfc_lut(uint32_t e,uint32_t c,uint32_t w){(void)e;(void)c;return w;}
static int fm1_rf_cal_scan(void *c){(void)c;return 0;}
'''
MAIN = r'''
int main(void) {
    uint8_t a[100],b[100],c[100],d[100]; const uint8_t *rec[4]={a,b,c,d};
    for(unsigned seed=0;seed<32;seed++) {
        memset(a,seed,sizeof a);memset(b,seed+3,sizeof b);memset(c,seed+7,sizeof c);memset(d,seed+11,sizeof d);
        memset(values,0,sizeof values);count=0;digest=0;memset(&fm1_ble_rf_stat,0,sizeof fm1_ble_rf_stat);
        rf_reference(0,sizeof ble_rf_prog,rec,0);
        uint64_t expected=digest;uint32_t saved[1024];memcpy(saved,values,sizeof saved);
        uint32_t n=count;typeof(fm1_ble_rf_stat) stats=fm1_ble_rf_stat;
        memset(values,0,sizeof values);count=0;digest=0;memset(&fm1_ble_rf_stat,0,sizeof fm1_ble_rf_stat);
        fm1_ble_rf_exec(0,sizeof ble_rf_prog,rec,0);
        assert(digest==expected&&count==n&&!memcmp(saved,values,sizeof saved));
        assert(!memcmp(&stats,&fm1_ble_rf_stat,sizeof stats));assert(!stats.bad_op);
    }
    puts("BLE RF: iterative replay matches pinned upstream transactions, register values and counters for 32 trim sets");
}
'''

if __name__ == '__main__':
    reference = (ROOT / 'tests/fixtures/ble_rf_exec_reference.h').read_text()
    code = PRELUDE + '\n' + '\n'.join(function(n) for n in (
        'fm1_ble_rf_lut2', 'fm1_ble_rf_wwg')) + reference + function('fm1_ble_rf_exec') + MAIN
    with tempfile.TemporaryDirectory() as directory:
        src = Path(directory) / 'rf.c'
        exe = Path(directory) / 'rf'
        src.write_text(code)
        subprocess.run(['cc', '-w', '-O1', '-I', str(ROOT), '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
