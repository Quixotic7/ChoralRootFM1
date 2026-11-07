/* SPDX-License-Identifier: GPL-3.0-only */
/* The one remembered device (ble_store.h). */
#include "ble_store.h"
#include "ble_util.h"

BLE_API void ble_store_reset(struct ble_dev_store *s)
{
    ble_zero((uint8_t *)s, sizeof *s);
    s->mark = BLE_DEV_MARK;
    s->ver = BLE_DEV_VER;
    s->sel = BLE_SEL_NONE;
}

BLE_API int ble_store_has_last(const struct ble_dev_store *s)
{
    return (s->dev.info & (BLE_DEV_USED | BLE_DEV_CENTRAL)) == (BLE_DEV_USED | BLE_DEV_CENTRAL);
}

BLE_API int ble_store_load(struct ble_dev_store *s, const uint8_t raw[BLE_DEV_STORE_SIZE])
{
    ble_cpy((uint8_t *)s, raw, BLE_DEV_STORE_SIZE);
    if (s->mark != BLE_DEV_MARK || s->ver != BLE_DEV_VER || s->sel > BLE_SEL_LAST) {
        ble_store_reset(s);
        return 0;
    }
    if (!ble_store_has_last(s)) {        /* (a role P entry cannot be LAST: the ruling) */
        ble_zero((uint8_t *)&s->dev, sizeof s->dev);
        s->sel = BLE_SEL_NONE;
    }
    return 1;
}

BLE_API void ble_store_save(const struct ble_dev_store *s, uint8_t raw[BLE_DEV_STORE_SIZE])
{
    ble_cpy(raw, (const uint8_t *)s, BLE_DEV_STORE_SIZE);
}

BLE_API void ble_store_set_last(struct ble_dev_store *s, const uint8_t addr[6], uint8_t addr_rand, const char *name,
                                uint8_t kind)
{
    uint32_t i;
    uint8_t keep = (uint8_t)(ble_store_has_last(s) && (s->dev.info & BLE_DEV_RANDOM) == (addr_rand ? 1u : 0u) &&
                             ble_eq(s->dev.addr, addr, 6));
    if (!keep)
        ble_zero((uint8_t *)&s->dev, sizeof s->dev);
    ble_cpy(s->dev.addr, addr, 6);
    s->dev.info = (uint8_t)((s->dev.info & (BLE_DEV_BONDED | BLE_DEV_IRK)) | BLE_DEV_USED | BLE_DEV_CENTRAL |
                            (addr_rand ? BLE_DEV_RANDOM : 0u) | (kind & 3u) << BLE_DEV_KIND_SHIFT);
    for (i = 0; i < BLE_NAME_MAX; i++)
        s->dev.name[i] = 0;
    for (i = 0; name && i < BLE_NAME_MAX && name[i]; i++)
        s->dev.name[i] = name[i];
}

BLE_API void ble_store_set_bond(struct ble_dev_store *s, const uint8_t ltk[16], const uint8_t rand[8], uint16_t ediv,
                                int auth)
{
    if (!ble_store_has_last(s))
        return;
    ble_cpy(s->dev.ltk, ltk, 16);
    ble_cpy(s->dev.rand, rand, 8);
    ble_wr16(s->dev.ediv, ediv);
    s->dev.info |= BLE_DEV_BONDED;
    s->dev.sec = (uint8_t)((s->dev.sec & ~BLE_DEV_SEC_AUTH) | (auth ? BLE_DEV_SEC_AUTH : 0u));
}

BLE_API void ble_store_drop_bond(struct ble_dev_store *s)
{
    s->dev.info &= (uint8_t)~BLE_DEV_BONDED;
    s->dev.sec &= (uint8_t)~BLE_DEV_SEC_AUTH;  /* (BLE_DEV_SEC_MITM kept: the new pairing asks for the passkey) */
}

BLE_API void ble_store_set_mitm(struct ble_dev_store *s)
{
    if (ble_store_has_last(s))
        s->dev.sec |= BLE_DEV_SEC_MITM;
}

BLE_API void ble_store_set_id(struct ble_dev_store *s, const uint8_t irk[16], const uint8_t id[6], uint8_t id_rand)
{
    if (!ble_store_has_last(s))
        return;
    ble_cpy(s->dev.irk, irk, 16);
    ble_cpy(s->dev.addr, id, 6);         /* the identity address replaces the RPA it was found at */
    s->dev.info = (uint8_t)((s->dev.info & ~BLE_DEV_RANDOM) | (id_rand ? BLE_DEV_RANDOM : 0u) | BLE_DEV_IRK);
}

BLE_API void ble_store_select(struct ble_dev_store *s, uint8_t sel)
{
    s->sel = (uint8_t)(sel == BLE_SEL_LAST && ble_store_has_last(s) ? BLE_SEL_LAST : BLE_SEL_NONE);
}

BLE_API void ble_store_forget(struct ble_dev_store *s)
{
    ble_store_reset(s);
}

BLE_API void ble_addr_text(const uint8_t a[6], char out[15])
{
    static const char HEX[] = "0123456789ABCDEF";
    static const uint8_t ORD[4] = {5, 4, 1, 0};  /* most significant first, the middle two left out */
    uint32_t k, o = 0;
    for (k = 0; k < 4u; k++) {
        out[o++] = HEX[a[ORD[k]] >> 4];
        out[o++] = HEX[a[ORD[k]] & 15u];
        if (k < 3u)
            out[o++] = ':';
        if (k == 1u)
            out[o++] = '.', out[o++] = '.', out[o++] = ':';
    }
    out[o] = 0;
}

BLE_API void ble_store_name(const struct ble_dev_store *s, char out[BLE_NAME_MAX + 1u])
{
    uint32_t i;
    for (i = 0; i < BLE_NAME_MAX && s->dev.name[i]; i++)
        out[i] = s->dev.name[i];
    out[i] = 0;
    if (!i && ble_store_has_last(s))
        ble_addr_text(s->dev.addr, out);
}
