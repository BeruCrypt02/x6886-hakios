/* Minimal read-only UFS driver for mtkloader. LK leaves the controller up. */
#ifndef UFS_H
#define UFS_H

#include <stdint.h>

#define UFS_BASE            HAKIOS_SOC_UFS
#define UFS_BLOCK_BYTES     4096u
/* LUN 0/1 are the 4 MB boot LUNs; the GPT, super and userdata live on the main LUN 2. */
#define UFS_MAIN_LUN        2u

#define UFSHC_CAP           0x000u
#define UFSHC_VER           0x008u
#define UFSHC_IS            0x020u
#define UFSHC_HCS           0x030u
#define UFSHC_HCE           0x034u
#define UFSHC_UTRLBA        0x050u
#define UFSHC_UTRLBAU       0x054u
#define UFSHC_UTRLDBR       0x058u
#define UFSHC_UTRLCLR       0x05Cu
#define UFSHC_UTRLRSR       0x060u

#define UFSHC_CAP_NUTRS_MASK 0x1Fu
#define UFSHC_CAP_64BIT      (1u << 24)
#define UFSHC_HCS_READY      0x03u
#define UFSHC_IS_FATAL       0x00031804u

#define UTRD_DW0_READ        (0x10000000u | 0x04000000u)
#define OCS_INVALID         0x0000000Fu
#define OCS_SUCCESS         0x00000000u

#define UPIU_TRANSACTION_COMMAND  0x01u
#define UPIU_CMD_FLAGS_READ       0x40u
#define UPIU_COMMAND_SET_SCSI     0x00u
#define UPIU_TRANSACTION_RESPONSE 0x21u
#define SCSI_READ_10              0x28u

struct ufshcd_sg_entry {
    uint32_t base_addr;
    uint32_t upper_addr;
    uint32_t reserved;
    uint32_t size;
};

struct utp_transfer_req_desc {
    uint32_t dword_0;
    uint32_t dword_1;
    uint32_t dword_2;
    uint32_t dword_3;
    uint32_t command_desc_base_addr_lo;
    uint32_t command_desc_base_addr_hi;
    uint16_t response_upiu_length;
    uint16_t response_upiu_offset;
    uint16_t prd_table_length;
    uint16_t prd_table_offset;
};

struct utp_transfer_cmd_desc {
    uint8_t command_upiu[512];
    uint8_t response_upiu[512];
    struct ufshcd_sg_entry prd_table[128];
};

#define UFS_SLOTS           32u
#define UFS_READ_BLOCKS_MAX 64u

static uint64_t ufs_list_phys;

struct ufs_err {
    const char *what;
    uint32_t status;
};
static struct ufs_err ufs_last;

static int ufs_fail(const char *what, uint32_t status)
{
    ufs_last.what = what;
    ufs_last.status = status;
    return 0;
}

static void ufs_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t ufs_get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int ufs_ready(void)
{
    uint32_t cap = rd32(UFS_BASE + UFSHC_CAP);
    uint32_t hcs = rd32(UFS_BASE + UFSHC_HCS);
    uint32_t hce = rd32(UFS_BASE + UFSHC_HCE);
    uint32_t is  = rd32(UFS_BASE + UFSHC_IS);
    uint32_t rsr = rd32(UFS_BASE + UFSHC_UTRLRSR);

    if (!(hce & 1)) {
        return ufs_fail("controller not enabled", hce);
    }
    if ((hcs & UFSHC_HCS_READY) != UFSHC_HCS_READY) {
        return ufs_fail("controller not ready", hcs);
    }
    if (!(rsr & 1)) {
        return ufs_fail("list not running", rsr);
    }
    if ((cap & UFSHC_CAP_NUTRS_MASK) + 1 < UFS_SLOTS) {
        return ufs_fail("too few slots", cap);
    }
    if (!(cap & UFSHC_CAP_64BIT)) {
        return ufs_fail("no 64-bit addressing", cap);
    }
    if (is & UFSHC_IS_FATAL) {
        return ufs_fail("fatal interrupt latched", is);
    }
    return 1;
}

static int ufs_init(uint64_t list_phys)
{
    uint32_t db = rd32(UFS_BASE + UFSHC_UTRLDBR);

    if (!ufs_ready()) {
        return 0;
    }
    if (db != 0) {
        return ufs_fail("doorbell busy", db);
    }
    wr32(UFS_BASE + UFSHC_UTRLRSR, 0);
    for (uint32_t i = 0; i < 2000000 && (rd32(UFS_BASE + UFSHC_UTRLRSR) & 1); i++) {
    }
    if (rd32(UFS_BASE + UFSHC_UTRLRSR) & 1) {
        wr32(UFS_BASE + UFSHC_UTRLRSR, 1);
        return ufs_fail("list would not stop", rd32(UFS_BASE + UFSHC_UTRLRSR));
    }
    wr32(UFS_BASE + UFSHC_UTRLBA, (uint32_t)list_phys);
    wr32(UFS_BASE + UFSHC_UTRLBAU, (uint32_t)(list_phys >> 32));
    wr32(UFS_BASE + UFSHC_UTRLRSR, 1);
    if (rd32(UFS_BASE + UFSHC_UTRLBA) != (uint32_t)list_phys ||
        rd32(UFS_BASE + UFSHC_UTRLBAU) != (uint32_t)(list_phys >> 32) ||
        !(rd32(UFS_BASE + UFSHC_UTRLRSR) & 1)) {
        return ufs_fail("list handoff failed", rd32(UFS_BASE + UFSHC_UTRLRSR));
    }
    ufs_list_phys = list_phys;
    return 1;
}

static int ufs_read_lun(uint32_t lun, uint64_t lba, uint32_t blocks, void *dst)
{
    struct utp_transfer_req_desc *utrd =
        (struct utp_transfer_req_desc *)ufs_list_phys;
    struct utp_transfer_cmd_desc *ucd =
        (struct utp_transfer_cmd_desc *)(ufs_list_phys + UFS_SLOTS * sizeof(*utrd));
    struct ufshcd_sg_entry *prd = ucd->prd_table;
    uint8_t *data = (uint8_t *)dst;

    if (blocks == 0) {
        return 1;
    }
    if (!ufs_list_phys) {
        return ufs_fail("not initialized", 0);
    }

    for (uint32_t done = 0; done < blocks; ) {
        uint32_t n = blocks - done;
        uint32_t bytes;
        uint8_t *upiu;
        uint64_t dst_phys;

        if (n > UFS_READ_BLOCKS_MAX) {
            n = UFS_READ_BLOCKS_MAX;
        }
        bytes = n * UFS_BLOCK_BYTES;
        dst_phys = (uint64_t)(data + (uint64_t)done * UFS_BLOCK_BYTES);

        for (uint32_t w = 0; w < sizeof(*utrd) / 4; w++) {
            ((volatile uint32_t *)utrd)[w] = 0;
        }
        utrd->command_desc_base_addr_lo = (uint32_t)(uint64_t)ucd;
        utrd->command_desc_base_addr_hi = (uint32_t)((uint64_t)ucd >> 32);
        utrd->response_upiu_length = 512u / 4u;
        utrd->response_upiu_offset = 512u / 4u;
        utrd->prd_table_length = 1;
        utrd->prd_table_offset = 1024u / 4u;
        utrd->dword_2 = OCS_INVALID;
        utrd->dword_0 = UTRD_DW0_READ;

        for (uint32_t i = 0; i < 512; i++) {
            ucd->command_upiu[i] = 0;
            ucd->response_upiu[i] = 0;
        }
        upiu = ucd->command_upiu;
        upiu[0] = UPIU_TRANSACTION_COMMAND;
        upiu[1] = UPIU_CMD_FLAGS_READ;
        upiu[2] = (uint8_t)(lun & 0x3fu);   /* UPIU COMMAND byte 2 = LUN, not always 0 */
        upiu[3] = 0;
        upiu[4] = UPIU_COMMAND_SET_SCSI;
        ufs_put_be32(upiu + 12, bytes);
        upiu[16] = SCSI_READ_10;
        upiu[18] = (uint8_t)((lba + done) >> 24);
        upiu[19] = (uint8_t)((lba + done) >> 16);
        upiu[20] = (uint8_t)((lba + done) >> 8);
        upiu[21] = (uint8_t)(lba + done);
        upiu[23] = (uint8_t)(n >> 8);
        upiu[24] = (uint8_t)n;

        prd->base_addr = (uint32_t)dst_phys;
        prd->upper_addr = (uint32_t)(dst_phys >> 32);
        prd->reserved = 0;
        prd->size = bytes - 1;

        __asm__ volatile("dsb sy" ::: "memory");
        wr32(UFS_BASE + UFSHC_UTRLDBR, 1u);
        for (uint32_t i = 0; i < 20000000 && (rd32(UFS_BASE + UFSHC_UTRLDBR) & 1); i++) {
        }
        if (rd32(UFS_BASE + UFSHC_UTRLDBR) & 1) {
            return ufs_fail("command timeout", rd32(UFS_BASE + UFSHC_UTRLDBR));
        }
        if ((utrd->dword_2 & 0xFu) != OCS_SUCCESS) {
            return ufs_fail("bad OCS", utrd->dword_2);
        }
        {
            const uint8_t *r = ucd->response_upiu;
            if ((r[0] & 0x3Fu) != UPIU_TRANSACTION_RESPONSE) {
                return ufs_fail("bad response", r[0]);
            }
            if (r[3] != 0 || r[6] != 0 || r[7] != 0 || (r[1] & 0x60)) {
                return ufs_fail("SCSI status", (uint32_t)r[6] | ((uint32_t)r[7] << 8));
            }
            if (ufs_get_be32(r + 12) != 0) {
                return ufs_fail("residual bytes", ufs_get_be32(r + 12));
            }
        }
        done += n;
    }
    return 1;
}

/* All GPT/system/payload reads use LUN 2 (the main UFS LUN). */
static inline int ufs_read(uint64_t lba, uint32_t blocks, void *dst)
{
    return ufs_read_lun(UFS_MAIN_LUN, lba, blocks, dst);
}

#endif
