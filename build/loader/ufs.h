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
#define UFSHC_IE            0x024u
#define UFSHC_HCS           0x030u
#define UFSHC_HCE           0x034u
/* 0x38 is the UIC PHY-adapter error code, read-to-clear. Log before clearing. */
#define UFSHC_UECPA         0x038u
#define UFSHC_UTRLBA        0x050u
#define UFSHC_UTRLBAU       0x054u
#define UFSHC_UTRLDBR       0x058u
#define UFSHC_UTRLCLR       0x05Cu
#define UFSHC_UTRLRSR       0x060u
/* Task-management list. Audited here, left STOPPED: every request this driver
 * sends (including QUERY REQUEST) goes through the transfer list, not UTMRL. */
#define UFSHC_UTMRLBA       0x070u
#define UFSHC_UTMRLBAU      0x074u
#define UFSHC_UTMRLDBR      0x078u
#define UFSHC_UTMRLRSR      0x080u
/* UIC command registers + MediaTek vendor reference-clock control. */
#define UFSHC_UICCMD        0x090u
#define UFSHC_UICARG1       0x094u
#define UFSHC_UICARG2       0x098u
#define UFSHC_UICARG3       0x09Cu
#define UFSHC_REFCLK_CTRL   0x144u

/* infracfg_ao reset block (soc/mt6789.toml + X6886 FDT). */
#define UFS_INFRACFG        HAKIOS_SOC_INFRACFG_AO
#define INFRACFG_HCI_SET    0x130u
#define INFRACFG_HCI_CLR    0x134u
#define INFRACFG_UNIPRO_SET 0x140u
#define INFRACFG_UNIPRO_CLR 0x144u
#define INFRACFG_HCI_BIT    15u
#define INFRACFG_UNIPRO_BIT 7u

#define UFSHC_CAP_NUTRS_MASK 0x1Fu
#define UFSHC_CAP_64BIT      (1u << 24)
/* HCS: bit0 UCRDY, bit1 UTRLRDY, bit2 UTMRLRDY, bit3 DP. 0x0f = all ready. */
#define UFSHC_HCS_UCRDY      (1u << 0)
#define UFSHC_HCS_UTRLRDY    (1u << 1)
#define UFSHC_HCS_UTMRLRDY   (1u << 2)
#define UFSHC_HCS_DP         (1u << 3)
#define UFSHC_HCS_READY      (UFSHC_HCS_UCRDY | UFSHC_HCS_UTRLRDY)
#define UFSHC_IS_FATAL       0x00031804u
#define IS_UIC_ERROR         (1u << 2)
#define IS_UIC_COMMAND_COMPL (1u << 10)

/* MediaTek SiP UFS control (refs/klios716 .../0107-kl7-mtk-ufs.patch). */
#define MTK_SIP_UFS_CONTROL       0x82000276ULL
#define UFS_SIP_DEVICE_RESET      2u
#define UFS_SIP_REF_CLK_NOTIFY    8u
#define UFS_REFCLK_REQUEST        0x1u
#define UFS_REFCLK_ACK            0x2u

#define UIC_DME_GET           0x01u
#define UIC_DME_SET           0x02u
#define UIC_DME_ENABLE        0x12u
#define UIC_DME_RESET         0x14u
#define UIC_DME_LINK_STARTUP  0x16u
#define PA_LOCAL_TX_LCC_ENABLE    0x155eu
#define PA_PWRMODE                0x1571u
#define VS_UNIPROPOWERDOWNCONTROL 0xd0a8u
#define VS_SAVEPOWERCONTROL       0xd0a6u

#define UPIU_QUERY_REQ       0x16u
#define UPIU_QUERY_RSP       0x36u
#define QUERY_READ_FLAG      0x05u
#define QUERY_SET_FLAG       0x06u
#define QUERY_FUNC_READ      0x01u
#define QUERY_FUNC_WRITE     0x81u
#define QUERY_FLAG_FDEVICEINIT 0x01u

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

/*
 * Every dbg_* character goes to the panel and to the ramoops console at
 * 0x480af000, so one call leaves the same evidence in both places.  Reading
 * 0x38..0x48 clears the UIC error codes; the value printed is also the clear.
 * Called at the list-enable gate so the next hardware log shows the exact
 * register state at the point where UTRLRSR fails to latch.
 */
static void ufs_dump_state(const char *when)
{
    dbg_puts("UFS ");
    dbg_puts(when);
    dbg_puts(": VER ");   dbg_hex(rd32(UFS_BASE + UFSHC_VER), 8);
    dbg_puts(" CAP ");    dbg_hex(rd32(UFS_BASE + UFSHC_CAP), 8);
    dbg_puts(" HCS ");    dbg_hex(rd32(UFS_BASE + UFSHC_HCS), 8);
    dbg_puts(" HCE ");    dbg_hex(rd32(UFS_BASE + UFSHC_HCE), 8);
    dbg_puts(" IS ");     dbg_hex(rd32(UFS_BASE + UFSHC_IS), 8);
    dbg_puts(" UECPA ");  dbg_hex(rd32(UFS_BASE + UFSHC_UECPA), 8);
    dbg_puts("\nUFS lists: UTRLBA ");  dbg_hex(rd32(UFS_BASE + UFSHC_UTRLBA), 8);
    dbg_puts(" UTRLBAU "); dbg_hex(rd32(UFS_BASE + UFSHC_UTRLBAU), 8);
    dbg_puts(" UTRLDBR "); dbg_hex(rd32(UFS_BASE + UFSHC_UTRLDBR), 8);
    dbg_puts(" UTRLRSR "); dbg_hex(rd32(UFS_BASE + UFSHC_UTRLRSR), 8);
    dbg_puts("\nUFS task:  UTMRLBA "); dbg_hex(rd32(UFS_BASE + UFSHC_UTMRLBA), 8);
    dbg_puts(" UTMRLBAU "); dbg_hex(rd32(UFS_BASE + UFSHC_UTMRLBAU), 8);
    dbg_puts(" UTMRLDBR "); dbg_hex(rd32(UFS_BASE + UFSHC_UTMRLDBR), 8);
    dbg_puts(" UTMRLRSR "); dbg_hex(rd32(UFS_BASE + UFSHC_UTMRLRSR), 8);
    dbg_puts("\nUFS rst: 0x130 "); dbg_hex(rd32(UFS_INFRACFG + INFRACFG_HCI_SET), 8);
    dbg_puts(" 0x134 ");  dbg_hex(rd32(UFS_INFRACFG + INFRACFG_HCI_CLR), 8);
    dbg_puts(" 0x140 ");  dbg_hex(rd32(UFS_INFRACFG + INFRACFG_UNIPRO_SET), 8);
    dbg_puts(" 0x144 ");  dbg_hex(rd32(UFS_INFRACFG + INFRACFG_UNIPRO_CLR), 8);
    dbg_puts(" REFCLK "); dbg_hex(rd32(UFS_BASE + UFSHC_REFCLK_CTRL), 8);
    dbg_puts("\n");
}

static int ufs_fail(const char *what, uint32_t status)
{
    ufs_last.what = what;
    ufs_last.status = status;
    /* Any failure disarms UFS: the caller then boots the embedded XNUP ramdisk. */
    ufs_list_phys = 0;
    ufs_dump_state(what);
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

/* ---- full MT6789 UFS bring-up, from refs/klios716 0107-kl7-mtk-ufs.patch ---- */

/* Bounded waits: no loop in this header may spin forever. */
#define UFS_UIC_TIMEOUT     500u    /* x 1 ms */
#define UFS_QUERY_TIMEOUT   100u    /* x 1 ms */
#define UFS_DEVINIT_POLLS   50u     /* x 5 ms */
#define UFS_HCE_TIMEOUT     1000u   /* x 1 ms */

static int ufs_read_lun(uint32_t lun, uint64_t lba, uint32_t blocks, void *dst);

/* stage 0 = PRE, 1 = POST (heliodarwin 0141 patch:758-759). SMC32 0x82000276 stays. */
static uint64_t ufs_smc(uint64_t cmd, uint64_t val, uint64_t stage)
{
    register uint64_t x0 __asm__("x0") = MTK_SIP_UFS_CONTROL;
    register uint64_t x1 __asm__("x1") = cmd;
    register uint64_t x2 __asm__("x2") = val;
    register uint64_t x3 __asm__("x3") = stage;

    __asm__ volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

/* One UIC command; result 0 means success, 0x100 means the bounded poll timed out. */
static uint32_t ufs_uic(uint32_t op, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t *val)
{
    wr32(UFS_BASE + UFSHC_IS, IS_UIC_COMMAND_COMPL | IS_UIC_ERROR);
    wr32(UFS_BASE + UFSHC_UICARG1, a1);
    wr32(UFS_BASE + UFSHC_UICARG2, a2);
    wr32(UFS_BASE + UFSHC_UICARG3, a3);
    wr32(UFS_BASE + UFSHC_UICCMD, op & 0xffu);

    for (uint32_t i = 0; i < UFS_UIC_TIMEOUT; i++) {
        if (rd32(UFS_BASE + UFSHC_IS) & IS_UIC_COMMAND_COMPL) {
            uint32_t result = rd32(UFS_BASE + UFSHC_UICARG2) & 0xffu;

            if (val) {
                *val = rd32(UFS_BASE + UFSHC_UICARG3);
            }
            wr32(UFS_BASE + UFSHC_IS, IS_UIC_COMMAND_COMPL);
            return result;
        }
        dbg_delay_ms(1);
    }
    return 0x100u;
}

static int ufs_host_reset(void)
{
    uint32_t v = rd32(UFS_INFRACFG + INFRACFG_HCI_SET);

    wr32(UFS_INFRACFG + INFRACFG_HCI_SET, v | (1u << INFRACFG_HCI_BIT));
    v = rd32(UFS_INFRACFG + INFRACFG_UNIPRO_SET);
    wr32(UFS_INFRACFG + INFRACFG_UNIPRO_SET, v | (1u << INFRACFG_UNIPRO_BIT));
    dbg_delay_ms(1);
    v = rd32(UFS_INFRACFG + INFRACFG_UNIPRO_CLR);
    wr32(UFS_INFRACFG + INFRACFG_UNIPRO_CLR, v | (1u << INFRACFG_UNIPRO_BIT));
    v = rd32(UFS_INFRACFG + INFRACFG_HCI_CLR);
    wr32(UFS_INFRACFG + INFRACFG_HCI_CLR, v | (1u << INFRACFG_HCI_BIT));
    dbg_delay_ms(1);
    dbg_puts("UFS: host reset done\n");
    return 1;
}

static int ufs_refclk_on(void)
{
    uint32_t v = rd32(UFS_BASE + UFSHC_REFCLK_CTRL);

    if ((v & UFS_REFCLK_REQUEST) && ((v & UFS_REFCLK_ACK) >> 1) == (v & UFS_REFCLK_REQUEST)) {
        return 1;
    }
    /* PRE (heliodarwin 0141:795): tell TF-A before writing the request bit. */
    uint64_t s = ufs_smc(UFS_SIP_REF_CLK_NOTIFY, 1, 0);

    if (s) {
        return ufs_fail("refclk PRE SMC", (uint32_t)s);
    }
    dbg_delay_ms(1);
    wr32(UFS_BASE + UFSHC_REFCLK_CTRL, UFS_REFCLK_REQUEST);
    for (uint32_t i = 0; i < 100u; i++) {
        v = rd32(UFS_BASE + UFSHC_REFCLK_CTRL);
        if (((v & UFS_REFCLK_ACK) >> 1) == (v & UFS_REFCLK_REQUEST)) {
            /* POST (0141:809): the notification is a pair, not one call. */
            s = ufs_smc(UFS_SIP_REF_CLK_NOTIFY, 1, 1);
            if (s) {
                return ufs_fail("refclk POST SMC", (uint32_t)s);
            }
            dbg_puts("UFS: refclk on (PRE+POST)\n");
            return 1;
        }
        dbg_delay_ms(1);
    }
    /* Timeout: POST with on=0, as 0141:804 does, then abort to the ramdisk. */
    (void)ufs_smc(UFS_SIP_REF_CLK_NOTIFY, 0, 1);
    return ufs_fail("refclk ACK", v);
}

static int ufs_hce_cycle(void)
{
    /*
     * LK leaves the reference clock requested and acked. The infracfg host
     * reset clears that handshake, so asking for the clock AFTER the reset
     * never gets an ACK. Request it first (PRE+POST), then reset, then ask
     * again once the reset has cleared the bits.
     */
    if (!ufs_refclk_on()) {
        return 0;
    }
    if (rd32(UFS_BASE + UFSHC_HCE) & 1u) {
        wr32(UFS_BASE + UFSHC_HCE, 0);
        for (uint32_t i = 0; i < 10u && (rd32(UFS_BASE + UFSHC_HCE) & 1u); i++) {
            dbg_delay_ms(1);
        }
    }
    if (rd32(UFS_BASE + UFSHC_HCE) & 1u) {
        return ufs_fail("HCE stop", rd32(UFS_BASE + UFSHC_HCE));
    }
    if (!ufs_host_reset() || !ufs_refclk_on()) {
        return 0;
    }
    wr32(UFS_BASE + UFSHC_HCE, 1);
    dbg_delay_ms(1);
    for (uint32_t i = 0; i < UFS_HCE_TIMEOUT && !(rd32(UFS_BASE + UFSHC_HCE) & 1u); i++) {
        dbg_delay_ms(1);
    }
    if (!(rd32(UFS_BASE + UFSHC_HCE) & 1u)) {
        return ufs_fail("HCE start", rd32(UFS_BASE + UFSHC_HCE));
    }
    dbg_puts("UFS: HCE cycled, HCS ");
    dbg_hex(rd32(UFS_BASE + UFSHC_HCS), 8);
    dbg_puts("\n");
    return 1;
}

static int ufs_pre_link(void)
{
    uint32_t v = 0, r;

    r = ufs_uic(UIC_DME_SET, VS_UNIPROPOWERDOWNCONTROL << 16, 0, 0, &v);
    if (r) {
        return ufs_fail("prelink HPM", r);
    }
    r = ufs_uic(UIC_DME_SET, PA_LOCAL_TX_LCC_ENABLE << 16, 0, 0, &v);
    if (r) {
        return ufs_fail("prelink TXLCC", r);
    }
    r = ufs_uic(UIC_DME_GET, VS_SAVEPOWERCONTROL << 16, 0, 0, &v);
    if (r) {
        return ufs_fail("prelink SAVEPWR get", r);
    }
    r = ufs_uic(UIC_DME_SET, VS_SAVEPOWERCONTROL << 16, 0, v & ~(1u << 6), &v);
    if (r) {
        return ufs_fail("prelink SAVEPWR set", r);
    }
    return 1;
}

static int ufs_bringup(void)
{
    uint32_t v = 0, r;

    if (!ufs_hce_cycle()) {
        return 0;
    }
    uint64_t s = ufs_smc(UFS_SIP_DEVICE_RESET, 0, 0);

    if (s) {
        return ufs_fail("device reset low", (uint32_t)s);
    }
    dbg_delay_ms(1);
    s = ufs_smc(UFS_SIP_DEVICE_RESET, 1, 0);
    if (s) {
        return ufs_fail("device reset high", (uint32_t)s);
    }
    dbg_delay_ms(15);
    dbg_puts("UFS: device reset done\n");

    if (!ufs_pre_link()) {
        return 0;
    }
    r = ufs_uic(UIC_DME_LINK_STARTUP, 0, 0, 0, &v);
    if (r) {
        /* The KL7 recovery path: DME_RESET -> DME_ENABLE -> pre-link -> retry. */
        (void)ufs_uic(UIC_DME_RESET, 0, 0, 0, &v);
        (void)ufs_uic(UIC_DME_ENABLE, 0, 0, 0, &v);
        if (!ufs_pre_link()) {
            return 0;
        }
        r = ufs_uic(UIC_DME_LINK_STARTUP, 0, 0, 0, &v);
    }
    if (r) {
        return ufs_fail("DME_LINK_STARTUP", r);
    }
    dbg_puts("UFS: link up\n");
    (void)ufs_uic(UIC_DME_GET, PA_PWRMODE << 16, 0, 0, &v);
    dbg_puts("UFS: PA_PWRMODE ");
    dbg_hex(v, 8);
    dbg_puts("\n");
    return 1;
}

/* One QUERY REQUEST in slot 0; value is the big-endian word at UPIU +20. */
static uint8_t ufs_tag;
static uint8_t ufs_gpt_buf[UFS_BLOCK_BYTES] __attribute__((aligned(16)));

static int ufs_query_flag(uint32_t opcode, uint32_t idn, uint32_t index, uint32_t *out)
{
    struct utp_transfer_req_desc *utrd = (struct utp_transfer_req_desc *)ufs_list_phys;
    struct utp_transfer_cmd_desc *ucd =
        (struct utp_transfer_cmd_desc *)(ufs_list_phys + UFS_SLOTS * sizeof(*utrd));
    uint64_t ucd_pa = ufs_list_phys + UFS_SLOTS * sizeof(*utrd);
    uint8_t *upiu = ucd->command_upiu;
    const uint8_t *r = ucd->response_upiu;
    uint32_t value;

    if (!ufs_list_phys) {
        return ufs_fail("query not initialized", 0);
    }
    for (uint32_t w = 0; w < sizeof(*utrd) / 4; w++) {
        ((volatile uint32_t *)utrd)[w] = 0;
    }
    utrd->command_desc_base_addr_lo = (uint32_t)ucd_pa;
    utrd->command_desc_base_addr_hi = (uint32_t)(ucd_pa >> 32);
    utrd->response_upiu_length = 512u / 4u;
    utrd->response_upiu_offset = 512u / 4u;
    /* A QUERY REQUEST has no data phase: no PRDT entry. */
    utrd->prd_table_length = 0;
    utrd->prd_table_offset = 0;
    utrd->dword_2 = OCS_INVALID;
    utrd->dword_0 = UTRD_DW0_READ;

    for (uint32_t i = 0; i < 512; i++) {
        ucd->command_upiu[i] = 0;
        ucd->response_upiu[i] = 0;
    }
    upiu[0] = UPIU_QUERY_REQ;
    upiu[2] = 0;                       /* device management, not a LUN */
    upiu[3] = ++ufs_tag;
    upiu[5] = (opcode == QUERY_SET_FLAG) ? QUERY_FUNC_WRITE : QUERY_FUNC_READ;
    upiu[12] = (uint8_t)opcode;
    upiu[13] = (uint8_t)idn;
    upiu[14] = (uint8_t)index;

    __asm__ volatile("dsb sy" ::: "memory");
    wr32(UFS_BASE + UFSHC_UTRLDBR, 1u);
    for (uint32_t i = 0; i < UFS_QUERY_TIMEOUT && (rd32(UFS_BASE + UFSHC_UTRLDBR) & 1u); i++) {
        dbg_delay_ms(1);
    }
    if (rd32(UFS_BASE + UFSHC_UTRLDBR) & 1u) {
        wr32(UFS_BASE + UFSHC_UTRLCLR, ~1u);
        return ufs_fail("query timeout", opcode);
    }
    __asm__ volatile("dsb sy" ::: "memory");
    wr32(UFS_BASE + UFSHC_IS, rd32(UFS_BASE + UFSHC_IS));

    value = ufs_get_be32(r + 20);
    if (out) {
        *out = value;
    }
    if ((utrd->dword_2 & 0xffu) != OCS_SUCCESS || r[0] != UPIU_QUERY_RSP || r[6]) {
        return ufs_fail("query response", (uint32_t)utrd->dword_2);
    }
    return 1;
}

static int ufs_complete_dev_init(void)
{
    uint32_t val = 0;

    if (!ufs_query_flag(QUERY_SET_FLAG, QUERY_FLAG_FDEVICEINIT, 0, &val)) {
        return 0;
    }
    for (uint32_t i = 0; i < UFS_DEVINIT_POLLS; i++) {
        if (!ufs_query_flag(QUERY_READ_FLAG, QUERY_FLAG_FDEVICEINIT, 0, &val)) {
            return 0;
        }
        if (!(val & 1u)) {
            return 1;
        }
        dbg_delay_ms(5);
    }
    return ufs_fail("fDeviceInit poll", val);
}

static int ufs_lun2_ready(void)
{
    static const char efi[] = "EFI PART";

    if (!ufs_read_lun(UFS_MAIN_LUN, 1, 1, ufs_gpt_buf)) {
        return 0;
    }
    for (uint32_t i = 0; i < sizeof(efi) - 1; i++) {
        if (ufs_gpt_buf[i] != (uint8_t)efi[i]) {
            return ufs_fail("LUN2 GPT", ufs_gpt_buf[i]);
        }
    }
    dbg_puts("UFS: LUN2 GPT ready\n");
    return 1;
}

/*
 * Controller-level gates only. UTRLRSR is deliberately NOT checked here:
 * ufs_bringup() cycles HCE, which stops the list, and the list is installed
 * afterwards by ufs_list_start(). The old check bailed out with "list not
 * running" before that code ran, so no request ever reached the device.
 */
static int ufs_ready(void)
{
    uint32_t cap = rd32(UFS_BASE + UFSHC_CAP);
    uint32_t hcs = rd32(UFS_BASE + UFSHC_HCS);
    uint32_t hce = rd32(UFS_BASE + UFSHC_HCE);
    uint32_t is  = rd32(UFS_BASE + UFSHC_IS);

    if (!(hce & 1)) {
        return ufs_fail("controller not enabled", hce);
    }
    if ((hcs & UFSHC_HCS_READY) != UFSHC_HCS_READY) {
        return ufs_fail("controller not ready", hcs);
    }
    if (!(hcs & UFSHC_HCS_UTRLRDY)) {
        return ufs_fail("UTRLRDY low", hcs);
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

/* This loader runs with SCTLR_EL1.C=0 (start.S), so the clean is defensive.
 * It becomes required if caches are ever enabled before the list start. */
static void ufs_clean_desc(uint64_t va, uint64_t size)
{
    uint64_t end = (va + size + 63u) & ~63ULL;

    for (uint64_t a = va & ~63ULL; a < end; a += 64u) {
        __asm__ volatile("dc cvac, %0" :: "r"(a) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

/* UIC error codes 0x38..0x48 are read-to-clear. Clear them once after link
 * startup, before the list is enabled, so a stale latch cannot block it. */
static void ufs_clear_uic_errors(void)
{
    (void)rd32(UFS_BASE + UFSHC_UECPA);
    (void)rd32(UFS_BASE + 0x03Cu);
    (void)rd32(UFS_BASE + 0x040u);
    (void)rd32(UFS_BASE + 0x044u);
    (void)rd32(UFS_BASE + 0x048u);
    __asm__ volatile("dsb sy" ::: "memory");
}

/* Install our own transfer list and start it. Called after link up, PA_PWRMODE
 * and HCS.UTRLRDY==1 -- never before. UTMRL stays stopped: this driver sends
 * no task-management requests, and writing UTMRL* would only disturb state. */
static int ufs_list_start(uint64_t list_phys)
{
    struct utp_transfer_req_desc *utrd = (struct utp_transfer_req_desc *)list_phys;
    uint64_t ucd_pa = list_phys + UFS_SLOTS * sizeof(*utrd);
    uint32_t rsr;

    if (!(rd32(UFS_BASE + UFSHC_HCS) & UFSHC_HCS_UTRLRDY)) {
        return ufs_fail("UTRLRDY low at list start", rd32(UFS_BASE + UFSHC_HCS));
    }
    if (!list_phys || (list_phys & 0x3ffu)) {
        return ufs_fail("UTRLBA not 1KiB aligned", (uint32_t)list_phys);
    }
    /* 64-bit DMA: high base word must be a plausible DRAM PA (< 64 GiB). */
    if ((list_phys >> 32) >= 0x10u || (ucd_pa >> 32) >= 0x10u) {
        return ufs_fail("UTRLBA/UCD not valid DMA", (uint32_t)(list_phys >> 32));
    }
    if (rd32(UFS_BASE + UFSHC_UTRLDBR) != 0) {
        return ufs_fail("doorbell busy at list start", rd32(UFS_BASE + UFSHC_UTRLDBR));
    }

    for (uint32_t i = 0; i < UFS_SLOTS; i++) {
        volatile uint32_t *w = (volatile uint32_t *)&utrd[i];
        uint64_t ucd = ucd_pa + (uint64_t)i * sizeof(struct utp_transfer_cmd_desc);

        for (uint32_t k = 0; k < sizeof(*utrd) / 4u; k++) {
            w[k] = 0;
        }
        utrd[i].command_desc_base_addr_lo = (uint32_t)ucd;
        utrd[i].command_desc_base_addr_hi = (uint32_t)(ucd >> 32);
    }
    ufs_clear_uic_errors();
    ufs_clean_desc(list_phys, UFS_SLOTS * (sizeof(*utrd) + sizeof(struct utp_transfer_cmd_desc)));
    ufs_dump_state("before list start");

    wr32(UFS_BASE + UFSHC_UTRLRSR, 0);
    for (uint32_t i = 0; i < 1000 && (rd32(UFS_BASE + UFSHC_UTRLRSR) & 1u); i++) {
        dbg_delay_ms(1);
    }
    if (rd32(UFS_BASE + UFSHC_UTRLRSR) & 1u) {
        wr32(UFS_BASE + UFSHC_UTRLRSR, 1);
        return ufs_fail("list would not stop", rd32(UFS_BASE + UFSHC_UTRLRSR));
    }
    wr32(UFS_BASE + UFSHC_UTRLBA, (uint32_t)list_phys);
    wr32(UFS_BASE + UFSHC_UTRLBAU, (uint32_t)(list_phys >> 32));
    __asm__ volatile("dsb sy" ::: "memory");
    if (rd32(UFS_BASE + UFSHC_UTRLBA) != (uint32_t)list_phys ||
        rd32(UFS_BASE + UFSHC_UTRLBAU) != (uint32_t)(list_phys >> 32)) {
        return ufs_fail("UTRLBA writeback", rd32(UFS_BASE + UFSHC_UTRLBA));
    }
    wr32(UFS_BASE + UFSHC_UTRLRSR, 1);
    for (uint32_t i = 0; i < 1000; i++) {
        rsr = rd32(UFS_BASE + UFSHC_UTRLRSR);
        if (rsr & 1u) {
            break;
        }
        dbg_delay_ms(1);
    }
    ufs_dump_state("after list start");
    if (!(rd32(UFS_BASE + UFSHC_UTRLRSR) & 1u)) {
        return ufs_fail("UTRLRSR did not latch", rd32(UFS_BASE + UFSHC_UTRLRSR));
    }
    dbg_puts("UFS: UTRLRSR latched, list running\n");
    return 1;
}

static int ufs_init(uint64_t list_phys)
{
    /* Read-only snapshot before touching anything, for the on-screen log. */
    ufs_dump_state("as LK left it");
    /*
     * MT6789 LK powers UFSHCI down before the handoff. Full bring-up is the
     * hardware-proven MT6789 sequence; EVERY failure returns 0 with
     * ufs_list_phys = 0 so the caller boots the embedded XNUP ramdisk.
     * Never resets the board, never spins without a bound.
     */
    if (!ufs_bringup() || !ufs_ready()) {
        return 0;
    }
    if (!ufs_list_start(list_phys)) {
        return 0;
    }
    ufs_list_phys = list_phys;
    dbg_puts("UFS: transfer list handed off\n");
    if (!ufs_complete_dev_init() || !ufs_lun2_ready()) {
        ufs_list_phys = 0;
        return 0;
    }
    dbg_puts("UFS: fDeviceInit done\n");
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
