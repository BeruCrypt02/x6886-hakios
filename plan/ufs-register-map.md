# UFS HCI register map + block-read procedure — Infinix X6886 / MT6789

Reference for a minimal, MMU-off, READ-ONLY UFS driver in mtkloader. Controller at
`0x11270000` (`compatible = "mediatek,mt8183-ufshci"`), DT MMIO size `0x2300`.
No device access, no writes to `/home/berucrypt00/otadump`, no port-tools use. This document is the
reference only — no driver is written here.

## READ THIS FIRST — two corrections to the task brief

1. **There is no data area at `UTRLDBR`/`UTMRLDBR`.** Those are 32-bit **doorbell bitmaps**
   (`0x58` and `0x78`): software writes a slot bit to submit, and reads the same bit back to see
   whether the slot is still busy. File data is DMA'd by the controller through the **PRDT**
   (Physical Region Descriptor Table) into a plain physical buffer you supply. `UTRLDBR` is used
   **only for submission and completion polling**; it is never a data source. Same for
   `UTMRLDBR`, which belongs to the unused task-management list.
2. **Legacy (non-MCQ) path only.** The stock DT has `mediatek,ufs-disable-mcq`, and U-Boot's MTK
   driver sets `UFSHCD_QUIRK_MCQ_BROKEN_INTR` and `UFSHCD_QUIRK_BROKEN_LSDBS_CAP`. Build for
   single-doorbell mode, slot 0, task tag 0. Do not touch any MCQ register.

---

## 0. Sources (what is VERIFIED against what)

| Tag | Source | What was taken from it |
|---|---|---|
| **UB-REG** | `https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/ufs/ufshci.h` | All standard register offsets, IS/HCS/HCE bits, OCS values, UTRD/UCD/PRDT structs, command-type/DD constants |
| **UB-CORE** | `https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/ufs/ufs-uclass.c` (Linux 5.2-derived core; `ufs.c` is a 404 on master, real file is `ufs-uclass.c`) | HCE enable flow, `ufshcd_make_hba_operational`, UTRD/UPIU construction, doorbell + IS completion, PRDT encoding, offsets in DW6/DW7 |
| **UB-UCLASS** | `https://raw.githubusercontent.com/u-boot/u-boot/master/include/ufs.h` | uclass API only (no register data) |
| **UB-MTK** | `https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/ufs/ufs-mediatek.c` | MTK quirks, refclk, HCE pre-change quirk, reset/clk/PHY sequence to *avoid* |
| **UB-MTK-H** | `https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/ufs/ufs-mediatek.h` | All MediaTek vendor register offsets and bitfields |
| **UB-SIP** | `https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/ufs/ufs-mediatek-sip.h` | Exact SMC call ID and subcommand bits to avoid |
| **KO-MTK** | `strings -a`, `modinfo` on `/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/ufs-mediatek-mod.ko` | Kernel 6.12 vendor register-bank dump labels, version (vermagic `6.12.38-android16-5-...`), symbol names |
| **KO-CORE** | `strings -a` on `/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/tran-ufshcd-core.ko` | Confirms `ufshcd_wait_for_doorbell_clr`, OCS/doorbell/tag/IS trace fields, READ(10) opcode 0x28 |
| **XNU-HC** | `/home/berucrypt00/hakios-x6886/refs/xnu-12377-hakios/bsd/dev/board/ufshc.c` (+ `ufshc.h`) | Concrete C implementation: raw UTRD dwords, READ(10) UPIU bytes, PRDT, doorbell polling, completion checks, leave-LK-state-alone policy, list handoff sequence |
| **XNU-UFS** | `.../bsd/dev/board/ufs.c` | GPT on UFS uses 4096-byte logical blocks; GPT header is LBA 1, `"EFI PART"` |
| **MSDC** | `/home/berucrypt00/x6886-hakios/refs/oosexgang-xnu27/loader/msdc.h` | House style to mirror: static base, `rd32`/`wr32`, bounded polls, `last error` struct, 32-bit accesses only, no SIMD |
| **DT** | `/home/berucrypt00/hakios-x6886/device-dump/fdt.dts` lines 5660-5674 (also `board/x6886-base.dts`) | Base/size, compatible, resets, clocks, `bootmode = 0x46`, `mediatek,ufs-disable-mcq` |
| **PRIOR** | `/home/berucrypt00/hakios-x6886/plan/ufs-uboot-analysis.md` | LK owns UFS before us; preloader/LK bring-up evidence |

Anything not covered by these tags is marked **INFERENCE**.

---

## 1. Full register offset table

All offsets are relative to `UFS_BASE = 0x11270000`. Use 32-bit MMIO accesses only (MMU-off rule,
as in `msdc.h`). The standard HCI register space ends at `0xA0`.

### 1.1 Standard UFSHCI registers — VERIFIED: UB-REG, cross-checked XNU-HC

| Macro | Offset | R/W | Purpose |
|---|---:|---|---|
| `REG_CONTROLLER_CAPABILITIES` (`UFSHC_CAP`) | `0x000` | RO | CAP. `[4:0]` NUTRS-1 (slots), bit 24 = 64-bit addressing, bit 25 = OOO, bit 30 = MCQ, bit 31 = LSDBS |
| `REG_MCQCAP` | `0x004` | RO | MCQ capabilities — **do not use** |
| `REG_UFS_VERSION` (`UFSHC_VER`) | `0x008` | RO | HCI version, e.g. `0x00000310` = 3.1; `[15:0]` minor, `[31:16]` major |
| `REG_EXT_CONTROLLER_CAPABILITIES` | `0x00C` | RO | — |
| `REG_CONTROLLER_PID` (`UFSHC_HCDDID`) | `0x010` | RO | Controller product ID |
| `REG_CONTROLLER_MID` (`UFSHC_HCPMID`) | `0x014` | RO | Controller manufacturer ID |
| `REG_AUTO_HIBERNATE_IDLE_TIMER` (`UFSHC_AHIT`) | `0x018` | RW | MTK clears bit `MASK_AUTO_HIBERN8_SUPPORT` capability via this; skip |
| `REG_INTERRUPT_STATUS` (`UFSHC_IS`) | `0x020` | RW1C | Interrupt status. **Write the value back to clear.** Read-only use is also OK if polling doorbell |
| `REG_INTERRUPT_ENABLE` (`UFSHC_IE`) | `0x024` | RW | LK's setting — leave alone if polling |
| `REG_CONTROLLER_STATUS` (`UFSHC_HCS`) | `0x030` | RO | Ready/device-present/error bits (table below) |
| `REG_CONTROLLER_ENABLE` (`UFSHC_HCE`) | `0x034` | RW | `1` = enable/start init, `0` = disable. **Do not write if LK left it enabled** |
| `REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER` | `0x038` | RO/W1C | Cleared on read — do not read in fast path |
| `REG_UIC_ERROR_CODE_DATA_LINK_LAYER` | `0x03C` | RO/W1C | " |
| `REG_UIC_ERROR_CODE_NETWORK_LAYER` | `0x040` | RO/W1C | " |
| `REG_UIC_ERROR_CODE_TRANSPORT_LAYER` | `0x044` | RO/W1C | " |
| `REG_UIC_ERROR_CODE_DME` | `0x048` | RO/W1C | " |
| `REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL` (`UFSHC_UTRIACR`) | `0x04C` | RW | U-Boot writes `0` to disable aggregation; leave as LK left it |
| `REG_UTP_TRANSFER_REQ_LIST_BASE_L` (`UFSHC_UTRLBA`) | `0x050` | RW | **Transfer request list base, low 32 bits (physical address)** |
| `REG_UTP_TRANSFER_REQ_LIST_BASE_H` (`UFSHC_UTRLBAU`) | `0x054` | RW | **Base, high 32 bits** |
| `REG_UTP_TRANSFER_REQ_DOOR_BELL` (`UFSHC_UTRLDBR`) | `0x058` | RW | **Doorbell: write `1 << slot` to submit; bit stays 1 until done** |
| `REG_UTP_TRANSFER_REQ_LIST_CLEAR` (`UFSHC_UTRLCLR`) | `0x05C` | WO | Clear doorbell bits; XNU writes `0` only in its own-list teardown |
| `REG_UTP_TRANSFER_REQ_LIST_RUN_STOP` (`UFSHC_UTRLRSR`) | `0x060` | RW | `1` = list running, `0` = stopped |
| `UTRLCNR` (not named in U-Boot) | `0x064` | RO/W1C | Completion notification register — VERIFIED: XNU-HC only. Optional; doorbell polling is enough |
| `REG_UTP_TASK_REQ_LIST_BASE_L` (`UFSHC_UTMRLBA`) | `0x070` | RW | Task-management list — **leave untouched** |
| `REG_UTP_TASK_REQ_LIST_BASE_H` (`UFSHC_UTMRLBAU`) | `0x074` | RW | " |
| `REG_UTP_TASK_REQ_DOOR_BELL` (`UFSHC_UTMRLDBR`) | `0x078` | RW | " |
| `REG_UTP_TASK_REQ_LIST_CLEAR` | `0x07C` | WO | " |
| `REG_UTP_TASK_REQ_LIST_RUN_STOP` (`UFSHC_UTMRLRSR`) | `0x080` | RW | " |
| `REG_UIC_COMMAND` | `0x090` | WO | UIC command — **do not use** (LK owns link state) |
| `REG_UIC_COMMAND_ARG_1` | `0x094` | WO | " |
| `REG_UIC_COMMAND_ARG_2` | `0x098` | WO | " |
| `REG_UIC_COMMAND_ARG_3` | `0x09C` | WO | " |
| `UFSHCI_REG_SPACE_SIZE` | `0x0A0` | — | End of standard space |
| `REG_UFS_CCAP` | `0x100` | RO | Crypto capability |
| `REG_UFS_CRYPTOCAP` | `0x104` | RO | Crypto capability |
| `REG_UFS_MEM_CFG` | `0x300` | RW | bit 0 = `MCQ_MODE_SELECT` — keep 0 |
| `REG_UFS_MCQ_CFG` | `0x380` | RW | MCQ — do not touch |
| `REG_UFS_ESILBA` / `REG_UFS_ESIUBA` | `0x384` / `0x388` | RW | MCQ ESI — do not touch |

### 1.2 MediaTek vendor registers — VERIFIED: UB-MTK-H, spot-checked KO-MTK

| Macro | Offset | Notes |
|---|---:|---|
| `REG_UFS_XOUFS_CTRL` | `0x140` | U-Boot ORs `0x80` (CLK_CG) in HCE PRE_CHANGE to avoid host hang. LK should already have set it — do not toggle |
| `REG_UFS_REFCLK_CTRL` | `0x144` | `REFCLK_REQUEST = BIT(0)`, `REFCLK_ACK = BIT(1)`, `REFCLK_RELEASE = 0`. **Access requires SMC 0x276 subcommand 0x8 — avoid** |
| `REG_UFS_MMIO_OPT_CTRL_0` | `0x160` | bits: `EHS_EN=BIT(0)`, `PFM_IMPV=BIT(1)`, `MCQ_*`, `MCQ_AH8=BIT(4)`. DDR_EN write `UFS_MASK(0x7FFF,8)=0x453000` only for `ip_ver >= IP_VER_MT6989 (0x10450000)` — **not MT6789** |
| `REG_UFS_EXTREG` | `0x2100` | KO-MTK label `Ext Reg (0x2100): ` |
| `REG_UFS_MPHYCTRL` | `0x2200` | KO-MTK label `MPHY Ctrl (0x2200): ` |
| `REG_UFS_MTK_IP_VER` | `0x2240` | Runtime IP version; read-only. U-Boot normalises `0x??xxxxxx` / `0x00xxxxxx` to `0x01xxxxxx` |
| `REG_UFS_REJECT_MON` | `0x22AC` | debug |
| `REG_UFS_DEBUG_SEL` | `0x22C0` | debug |
| `REG_UFS_PROBE` | `0x22C8` | KO-MTK label `Debug Probe (0x22C8): ` |
| `REG_UFS_DEBUG_SEL_B0..B3` | `0x22D0`, `0x22D4`, `0x22D8`, `0x22DC` | debug |
| `REG_UFS_MTK_SQD` / `SQIS` / `CQD` / `CQIS` | `0x2800` / `0x2814` / `0x281C` / `0x2824` | MCQ queues — unused; KO-MTK label `UFSHCI (0x2800): ` |
| `REG_UFS_MCQ_STRIDE` | `0x30` | Offset within queue bank |

KO-MTK also dumps banks `UFSHCI (0x0)`, `(0x100)`, `(0x170)`, `(0x300)`, `(0x380)`,
`(0x22F0)`, `MMIO_RSV_CTRL (0x2308)`. `0x170` and `0x22F0` are vendor debug banks with no
published definition in our sources (**INFERENCE**: MediaTek-private). `0x2308` is 8 bytes past
the DT MMIO size `0x2300` — do not access it. All registers this driver needs are `<= 0x22DC`,
inside the DT window.

### 1.3 Bit values you will actually test — VERIFIED: UB-REG (+ XNU-HC)

```c
/* REG_INTERRUPT_STATUS (0x20) / REG_INTERRUPT_ENABLE (0x24) */
#define UTP_TRANSFER_REQ_COMPL   0x00000001u
#define UIC_DME_END_PT_RESET     0x00000002u
#define UIC_ERROR                0x00000004u
#define UIC_TEST_MODE            0x00000008u
#define UIC_POWER_MODE           0x00000010u
#define UIC_HIBERNATE_EXIT       0x00000020u
#define UIC_HIBERNATE_ENTER      0x00000040u
#define UIC_LINK_LOST            0x00000080u
#define UIC_LINK_STARTUP         0x00000100u
#define UTP_TASK_REQ_COMPL       0x00000200u
#define UIC_COMMAND_COMPL        0x00000400u
#define DEVICE_FATAL_ERROR       0x00000800u
#define CONTROLLER_FATAL_ERROR   0x00010000u
#define SYSTEM_BUS_FATAL_ERROR   0x00020000u
#define CRYPTO_ENGINE_FATAL_ERROR 0x00040000u
#define MCQ_CQ_EVENT_STATUS      0x00100000u
#define INT_FATAL_ERRORS   (DEVICE_FATAL_ERROR | CONTROLLER_FATAL_ERROR | \
                            SYSTEM_BUS_FATAL_ERROR | CRYPTO_ENGINE_FATAL_ERROR | \
                            UIC_LINK_LOST)                 /* 0x00070880 */
#define UFSHCD_ERROR_MASK  (UIC_ERROR | INT_FATAL_ERRORS)   /* 0x00070884 */
/* U-Boot's enable set, if you choose IS polling instead of doorbell polling: */
#define UFSHCD_ENABLE_INTRS (UTP_TRANSFER_REQ_COMPL | UTP_TASK_REQ_COMPL | \
                             UFSHCD_ERROR_MASK)             /* 0x00070A85 */

/* REG_CONTROLLER_STATUS (0x30) */
#define DEVICE_PRESENT                 0x01u
#define UTP_TRANSFER_REQ_LIST_READY    0x02u
#define UTP_TASK_REQ_LIST_READY        0x04u
#define UIC_COMMAND_READY              0x08u
#define HOST_ERROR_INDICATOR           0x10u
#define DEVICE_ERROR_INDICATOR         0x20u
#define UFSHCD_STATUS_READY  (UTP_TRANSFER_REQ_LIST_READY | \
                              UTP_TASK_REQ_LIST_READY | UIC_COMMAND_READY)  /* 0x0E */

/* REG_CONTROLLER_ENABLE (0x34) */
#define CONTROLLER_ENABLE  0x1u
#define CONTROLLER_DISABLE 0x0u

/* Run/stop registers */
#define UTP_TRANSFER_REQ_LIST_RUN_STOP_BIT 0x1u   /* UTRLRSR 0x60 */
#define UTP_TASK_REQ_LIST_RUN_STOP_BIT     0x1u   /* UTMRLRSR 0x80 */
```

XNU-HC's XNU-side fatal subset is `(1<<2)|(1<<11)|(1<<12)|(1<<16)|(1<<17)` =
`0x00031804` (UIC_ERROR, DEVICE_FATAL, UTP transfer fatal, HCFE, SBFES). U-Boot's mask is the
superset above. Preconditions used by XNU-HC before touching any register:
`HCE & 1`, `HCS & (DEVICE_PRESENT|UTRLRDY) == both`, `UTRLRSR & 1`, `VER >= 0x200 && VER < 0x400`,
`(CAP & 0x1F)+1 >= 32`, `CAP & BIT(24)` (64-bit addressing), `UTRLDBR == 0`, `!(IS & IS_FATAL)`.

---

## 2. Descriptor structures — ready to paste as C

### 2.1 UTRD (one per slot, 32 bytes)

VERIFIED: UB-REG `struct utp_transfer_req_desc` + `struct request_desc_header`. Use raw dwords;
do **not** use C bitfields (compiler layout is not wire layout).

```c
#include <stdint.h>

struct utp_transfer_req_desc {
    uint32_t dword_0;                  /* 0x00: command type / data direction / interrupt */
    uint32_t dword_1;                  /* 0x04: reserved = 0 */
    uint32_t dword_2;                  /* 0x08: OCS written by controller, low nibble */
    uint32_t dword_3;                  /* 0x0C: reserved = 0 */
    uint32_t command_desc_base_addr_lo;/* 0x10: physical low 32 of UCD */
    uint32_t command_desc_base_addr_hi;/* 0x14: physical high 32 of UCD */
    uint16_t response_upiu_length;     /* 0x18: in DWORDS = 512/4 = 128 = 0x0080 */
    uint16_t response_upiu_offset;     /* 0x1A: in DWORDS = 512/4 = 128 = 0x0080 */
    uint16_t prd_table_length;         /* 0x1C: number of PRDT entries */
    uint16_t prd_table_offset;         /* 0x1E: in DWORDS = 1024/4 = 256 = 0x0100 */
};                                     /* sizeof = 32 */
```

Byte offsets are exactly as listed: the two 16-bit fields at `0x18` together form dword 6, and
those at `0x1C` form dword 7. On little-endian AArch64 the 32-bit view is:

| DW | Byte offset | Value for a 512-byte read from LUN 0, tag 0 |
|---:|---:|---|
| 0 | `0x00` | `0x15000000` (U-Boot, I bit set) **or** `0x14000000` (XNU-HC, no I bit) |
| 1 | `0x04` | `0x00000000` |
| 2 | `0x08` | `0x0000000F` before submit (`OCS_INVALID_COMMAND_STATUS`), `0x00000000` on success |
| 3 | `0x0C` | `0x00000000` |
| 4 | `0x10` | `(uint32_t)ucd_phys` |
| 5 | `0x14` | `(uint32_t)(ucd_phys >> 32)` |
| 6 | `0x18` | `0x00800080` (length `0x0080`, offset `0x0080`) |
| 7 | `0x1C` | `0x01000000 | prd_entries` |

**DW0 bit layout** — VERIFIED: UB-REG + UB-CORE:

| Bits | Field | Value used for READ |
|---|---|---|
| 31:28 | `UTP_CMD_TYPE_UFS` | `1` → `1 << 28 = 0x10000000` (note: U-Boot uses `1`, not `UTP_CMD_TYPE_SCSI=0`, for SCSI-command UPIUs) |
| 27:25 | Data direction | `UTP_DEVICE_TO_HOST = 2` → `2 << 25 = 0x04000000` |
| 24 | `UTP_REQ_DESC_INT_CMD` (interrupt enable) | `1` → `0x01000000`; optional if polling the doorbell |
| 23:0 | reserved | `0` |

Constants (VERIFIED: UB-REG/UB-CORE):

```c
#define UTP_CMD_TYPE_UFS             0x10000000u
#define UTP_REQ_DESC_INT_CMD         0x01000000u
#define UTP_NO_DATA_TRANSFER         0u
#define UTP_HOST_TO_DEVICE           1u
#define UTP_DEVICE_TO_HOST           2u
#define UTP_SCSI_COMMAND             0x00000000u
#define UTP_NATIVE_UFS_COMMAND       0x10000000u
#define UTP_DEVICE_MANAGEMENT_FUNCTION 0x20000000u
#define OCS_SUCCESS                  0x0u
#define OCS_INVALID_COMMAND_STATUS   0xFu
#define MASK_OCS                     0xFu
```

XNU-HC uses exactly `UTRD_CT_UFS | UTRD_DD_IN = 0x14000000` and never sets the I bit, because it
polls UTRLDBR. Both are correct; **for a minimal MMU-off poller, use `0x14000000`** (no interrupt
programming needed). Do not set DD to `3` (reserved) and never set `UTP_HOST_TO_DEVICE` for a read.

### 2.2 UCD (command descriptor, per slot, 3072 bytes) — VERIFIED: UB-REG + UB-CORE + XNU-HC

```c
#define ALIGNED_UPIU_SIZE   512
#define MAX_BUFF            128
#define UCD_PRDT           1024

struct ufshcd_sg_entry {
    uint32_t base_addr;    /* +0x00: physical address low 32 */
    uint32_t upper_addr;   /* +0x04: physical address high 32 */
    uint32_t reserved;     /* +0x08: 0 */
    uint32_t size;         /* +0x0C: byte_count - 1 */
};                         /* 16 bytes */

struct utp_transfer_cmd_desc {
    uint8_t  command_upiu[512];                 /* offset 0x000 */
    uint8_t  response_upiu[512];                /* offset 0x200 */
    struct ufshcd_sg_entry prd_table[128];      /* offset 0x400, 128*16 = 0x800 */
};                                              /* total 0xC00, 1 KiB-aligned */
```

`UTRLBA` points at an array of **UTRDs**, one per slot, each 32 bytes, 1 KiB-aligned
(U-Boot `memalign(1024, ...)`); each UTRD's `command_desc_base_addr_lo/hi` points at its own UCD.

**PRDT encoding** — VERIFIED: UB-CORE `prepare_prdt_desc()` + XNU-HC `hc_place()`:

```c
entry->size = (byte_count - 1);        /* MUST be count-1, not count */
entry->base_addr = (uint32_t)phys;
entry->upper_addr = (uint32_t)((uint64_t)phys >> 32);
entry->reserved = 0;
```

Max bytes per PRDT entry: `MAX_PRDT_ENTRY = 262144 = 0x40000 = 256 KiB` (UB-CORE), XNU-HC uses
`PRD_MAX = (256u << 10)` — identical. For a larger read, split into `ceil(bytes / 0x40000)`
entries, each covering at most `0x40000` bytes; `prd_table_length = n`. For our GPT/APFS fetch,
read in chunks of at most 256 KiB (and respect the READ(10) 16-bit block count — see §4.6).

---

## 3. UPIU layouts (byte offsets)

### 3.1 Common header (12 bytes) — VERIFIED: UB-REG `struct utp_upiu_header`, UB-CORE macro

`UPIU_HEADER_DWORD(b3,b2,b1,b0) = cpu_to_be32((b3<<24)|(b2<<16)|(b1<<8)|b0)`

| Byte | DW | Command UPIU | Response UPIU |
|---:|---:|---|---|
| 0 | 0 | Transaction type = `UPIU_TRANSACTION_COMMAND = 0x01` | `UPIU_TRANSACTION_RESPONSE = 0x21` |
| 1 | 0 | Flags: `UPIU_CMD_FLAGS_READ = 0x40`, `WRITE = 0x20`, `NONE = 0x00`; task attribute in low bits (simple = 0) | Flags: `UNDERFLOW=0x20`, `OVERFLOW=0x40` |
| 2 | 0 | LUN (7:0 normal LUN; bit 7 set = well-known LUN — **never use**) | Response code (bFunctionality) |
| 3 | 0 | Task tag (use `0` for a single-slot driver) | Task tag — must equal request's tag |
| 4 | 1 | Command set type: `UPIU_COMMAND_SET_TYPE_SCSI = 0x00` | reserved |
| 5-7 | 1 | 0 | 0 (low byte of DW1 at byte 7) |
| 8-9 | 2 | EHS length / reserved; U-Boot writes `0` | sense-data-related; check `0` |
| 10-11 | 2 | Data segment length (16-bit BE); U-Boot writes `0` for SCSI command | residual / data length |

Notes:
- U-Boot's `ufshcd_get_req_rsp()` reads `be32_to_cpu(dword_0) >> 24` → response transaction type.
- U-Boot's `ufshcd_get_rsp_upiu_result()` reads `be32_to_cpu(dword_1) & 0xFFFF`; `>> 8` yields
  the response code at byte 6. XNU-HC checks `r[6] == 0` and `r[7] == 0` (SCSI status byte) — both
  agree.
- XNU-HC additionally rejects a response when `r[1] & (0x20|0x40)` or residual bytes 12..15 are
  nonzero.

### 3.2 Command UPIU (DW0..DW7, 32 bytes) — VERIFIED: UB-REG `struct utp_upiu_req`/`utp_upiu_cmd`, XNU-HC byte construction

```c
struct utp_upiu_cmd {
    uint32_t exp_data_transfer_len;  /* DW3 @ byte 12, BIG ENDIAN: expected bytes */
    uint8_t  cdb[16];                /* DW4..DW7 @ bytes 16..31 */
};
/* total upiu = 12-byte header + 20 = 32 bytes */
```

### 3.3 READ(10) CDB — VERIFIED: XNU-HC `hc_make_read()` lines 335-356 (opcode 0x28)

CDB occupies UPIU bytes **16..31**:

| UPIU byte | CDB index | Value for READ(10) |
|---:|---:|---|
| 16 | 0 | `0x28` (READ(10)) |
| 17 | 1 | `0x00` (RDPROTECT=0, FUA=0, DPO=0) |
| 18 | 2 | `(lba >> 24) & 0xFF` |
| 19 | 3 | `(lba >> 16) & 0xFF` |
| 20 | 4 | `(lba >> 8) & 0xFF` |
| 21 | 5 | `lba & 0xFF` |
| 22 | 6 | `0x00` (group number) |
| 23 | 7 | `(blocks >> 8) & 0xFF` |
| 24 | 8 | `blocks & 0xFF` |
| 25 | 9 | `0x00` |
| 26-31 | 10-15 | `0x00` |

`lba` is in **logical blocks**, not bytes. `exp_data_transfer_len` at UPIU byte 12 must equal
`blocks * block_size` (big endian). XNU-HC enforces exactly this equality (`upiu[12..15] ==
blocks * 4096`) and refuses a mismatch.

### 3.4 Complete READ command UPIU for LUN 0, tag 0, LBA 0, 1 block of 4096 B

```
offset 00: 01 40 00 00   (type=0x01, flags=R 0x40, LUN=0, tag=0)
offset 04: 00 00 00 00   (SCSI command set)
offset 08: 00 00 00 00   (EHS=0, data segment len=0)
offset 0C: 00 00 10 00   (expected bytes = 4096, BE)   <-- 0x00001000
offset 10: 28 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00
```
(If your device's logical block size is 512, byte 0x0F becomes `0x00`. See §4.6.)

### 3.5 Response UPIU — VERIFIED: UB-REG `struct utp_cmd_rsp`, XNU-HC `hc_complete()`

```c
struct utp_cmd_rsp {
    uint32_t residual_transfer_count;  /* DW3 @ byte 12, BE; must be 0 for a good read */
    uint32_t reserved[4];              /* DW4..DW7 @ bytes 16..31 */
    uint16_t sense_data_len;           /* DW8 low half @ bytes 32..33 */
    uint8_t  sense_data[18];           /* bytes 34..51 */
};
```

Completion test used by XNU-HC (all must hold):
`ocs = utrd[2] & 0xFF == 0`, `(rsp[0] & 0x3F) == 0x21`, `rsp[3] == tag`, `rsp[6] == 0`,
`rsp[7] == 0` (or `0x02` for TEST UNIT READY), `(rsp[1] & 0x60) == 0`, `rsp[12..15] == 0`.

### 3.6 Data UPIU — IMPORTANT

There is **no DATA_IN UPIU structure in host memory**. `UPIU_TRANSACTION_DATA_IN = 0x22` is a
link-level transaction the UFS device sends; the HCI extracts its payload and DMAs it straight
into the PRDT-described buffer. Your code sees only: command UPIU (you write), response UPIU
(controller writes), PRDT (you write), data buffer (controller writes).

---

## 4. Step-by-step READ(10) sequence

### 4.0 Preconditions to CHECK before doing anything — VERIFIED: XNU-HC lines 1215-1278

```
base = 0x11270000
cap = rd32(base + 0x00)
ver = rd32(base + 0x08)
hcs = rd32(base + 0x30)
hce = rd32(base + 0x34)
is  = rd32(base + 0x20)
lo  = rd32(base + 0x50); hi = rd32(base + 0x54)
db  = rd32(base + 0x58); rsr = rd32(base + 0x60)
```

Reject (fall back to asking LK / abort) if:
- `(hce & 1) == 0` — controller not enabled
- `(hcs & 0x03) != 0x03` — no device present or transfer list not ready
- `(rsr & 1) == 0` — list not running
- `ver < 0x200 || ver >= 0x400` — XNU-HC's tested range (MT6789 is expected to be 3.x; read it)
- `(cap & 0x1F) + 1 < 32` — not enough slots for legacy 32-slot list
- `(cap & BIT(24)) == 0` — no 64-bit addressing (our PRDT uses 64-bit bases)
- `is & 0x00031804` — fatal status already latched (read IS only, do not clear LK's bits)
- `db != 0` — LK has a request in flight; **wait for it to drain, do not proceed**

### 4.1 Take ownership of the transfer list (only if you must install your own) — VERIFIED: XNU-HC `ufshc_switch_in()` lines 1003-1048

This is the exact, proven sequence; mirror it:

```
1. save old_lo = UTRLBA(0x50), old_hi = UTRLBAU(0x54), old_is, old_cnr
2. wr32(UTRLRSR 0x60, 0)                       /* stop list */
3. poll until (rd32(0x60) & 1) == 0, bounded ~5 ms; if it stays 1, restore wr32(0x60, 1) and abort
4. wr32(UTRLBA 0x50, my_list_phys_lo)
   wr32(UTRLBAU 0x54, my_list_phys_hi)
5. wr32(UTRLRSR 0x60, 1)                       /* restart */
6. verify rd32(0x50)==lo && rd32(0x54)==hi && (rd32(0x60)&1)==1; else restore old base + RSR=1
7. never write UTMRL* (task list), never touch IS/IE/UTRIACR/HCE/HCS
```

If instead you can safely reuse LK's live list, skip §4.1, locate a free slot from LK's UTRLDBR,
and read LK's UTRD[slot].command_desc_base_addr_lo/hi to find its UCD — but this is far more
fragile (**INFERENCE**: no evidence LK exposes its UCD layout to us). Building our own list after
verifying `db == 0` is the recommended path, and is exactly what XNU-HC does to take a list away
from another firmware.

### 4.2 Set up host memory (physical addresses, MMU off)

```
utrdl = 1 KiB-aligned, 32 slots * 32 bytes
ucd[i]= 1 KiB-aligned, 3072 bytes each
data  = physical buffer, size = blocks * block_size
```
Fill UTRD slot 0 (`§2.1`) with `command_desc_base_addr_lo/hi = phys(ucd[0])`, DW6 = `0x00800080`,
DW7 = `0x01000000 | prd_entries`. Clear all other UTRD slots to zero so stray doorbell bits are
harmless.

If the D-cache is enabled with MMU off, clean+invalidate the UTRD/UCD/PRDT region before ringing,
and invalidate the data buffer after completion (XNU-HC uses `FlushPoC_DcacheRegion` both before
and after — `bsd/dev/board/ufshc.c:800,834`). If caches are off, no maintenance is needed
(**INFERENCE** — check your mtkloader start.S).

### 4.3 Build the command in UCD slot 0

```
zero ucd[0] entirely
write the 32-byte command UPIU at ucd[0] + 0x000   (§3.4)
zero ucd[0] + 0x200 .. 0x3FF                        (response area, controller fills)
for n in 0..prd_entries-1:
    prd = ucd[0] + 0x400 + 16*n
    prd[0] = (uint32_t)(data_phys + n*0x40000)
    prd[1] = (uint32_t)((data_phys + n*0x40000) >> 32)
    prd[2] = 0
    prd[3] = chunk_bytes - 1
```
Length check: `exp_data_transfer_len` (UPIU byte 12..15) must equal `blocks * block_size` and
`sum(chunk_bytes)` must equal that same value. XNU-HC enforces both.

### 4.4 Ring the doorbell — VERIFIED: UB-CORE `ufshcd_send_command()`, XNU-HC `hc_ring()`

```
wr32(base + UTRLDBR 0x58, 1u << slot)      /* slot = 0 */
```
Preconditions XNU-HC checks immediately before the write: our list base is in `UTRLBA/U`,
`(UTRLRSR & 1) != 0`, `(UTRLDBR & bit) == 0`. With MMU off, use a barrier around the MMIO write
(`msdc.h` style: plain volatile 32-bit `rd32`/`wr32`; add `dsb sy` if your build has caches/out-of
-order stores).

### 4.5 Poll for completion — two verified options

**Option A (recommended here): poll UTRLDBR bit** — VERIFIED: XNU-HC lines 645-673, 590-633.
The controller clears the bit when the slot's command completes:

```
for (;;) {
    if ((rd32(UTRLDBR 0x58) & 1u) == 0) break;
    if (timeout > 10'000'000 us) fail;      /* XNU-HC TIMEOUT_MS = 10000 */
}
```
No interrupt setup, no IS clearing, works with LK's `IE` whatever it is. Optionally read
`UTRLCNR 0x64` (completion notification) as a secondary indicator — XNU-HC only reads it.

**Option B: poll IS** — VERIFIED: UB-CORE `ufshcd_send_command()`. Only if you enabled
`UTP_TRANSFER_REQ_COMPL` in `IE 0x24`:

```
do {
    st = rd32(0x20);
    en = st & intr_mask;
    wr32(0x20, st);                  /* W1C: writing back clears */
    if (en & UFSHCD_ERROR_MASK) fail;
} while (!(en & UTP_TRANSFER_REQ_COMPL));
```
Do not use this if you did not re-enable `IE`; LK may have masked it.

### 4.6 Verify and collect data — VERIFIED: UB-CORE `ufs_scsi_exec()`, XNU-HC `hc_complete()`

```
ocs = utrd[2] & 0x0F;          if (ocs != OCS_SUCCESS) fail
r   = (uint8_t *)&ucd[0].response_upiu;
if ((r[0] & 0x3F) != 0x21) fail                  /* RESPONSE UPIU */
if (r[3] != 0) fail                              /* task tag */
if (r[6] != 0 || r[7] != 0) fail                 /* response + SCSI status */
if ((r[1] & 0x60) != 0) fail                     /* under/overflow */
if (r[12] | r[13] | r[14] | r[15]) fail          /* residual != 0 */
data is ALREADY in your physical `data` buffer via the PRDT — nothing to read from MMIO
```

**Chunking rule (both verified sources):** one PRDT entry <= `0x40000` bytes (256 KiB); READ(10)'s
transfer-length field is 16-bit, so at most `65535` blocks per command. With XNU-UFS's 4096-byte
logical blocks, that is at most `65535 * 4096 ≈ 256 MiB` per READ(10); with 512-byte blocks,
`~32 MiB`. Use 256 KiB chunks (64 blocks @4096) to stay simple and safe.

**Block size — do not hardcode blindly.** XNU-HC and XNU-UFS assume `BLOCK = 4096` and the GPT
header lives at LBA 1; U-Boot's generic SCSI layer discovers the block size from READ CAPACITY(10).
For a minimal driver reading a GPT, VERIFIED options are:
- XNU-UFS: LBA 1 of the GPT-bearing LUN is 4096 bytes and starts with `"EFI PART"`.
- READ CAPACITY(10) CDB `0x25` (included in XNU-HC's whitelist, so known-good shape) returns the
  block length. **INFERENCE** that its exact response parsing is simple enough for our loader;
  if in doubt, read LBA 1 at 4096 B first and check `"EFI PART"`.

### 4.7 Optional teardown when finished

XNU-HC's `ufshc_switch_out()` (lines 916-949) does: wait for `UTRLDBR == 0`, write `UTRLCLR 0x5C`,
wait for the doorbell to clear, write `UTRLRSR 0`, wait for it to read 0, restore LK's saved
`UTRLBA/U`, write `UTRLRSR 1`. In our one-shot mtkloader flow, leaving our list installed is
acceptable (**INFERENCE**: LK will not resume after handing off to us), but restoring LK's state is
the conservative choice if a later stage expects it.

---

## 5. MediaTek / X6886-specific notes

### 5.1 What LK most likely left initialized — VERIFIED (PRIOR + KO-MTK + XNU-HC pattern)
- Clocks enabled (`ufs`, `ufs_mp`, `unipro_tick`, `unipro_sys`, `ufs_aes`, `ufs_ck` per DT),
  VEMC (`vcc-supply`, shared with MSDC0) powered, MPHY up, UniPro link in HS gear, HCE=1,
  HCS ready bits set, UTRLRSR=1, UTMRLRSR=1, `IE`/`UTRIACR` set to LK's choices, `UTRLBA/U`
  pointing at LK's list, possibly a few doorbell bits still pending.
- `bootmode = 0x46`, `mediatek,ufs-qos`, `mediatek,ufs-disable-mcq` in DT.
- `atag,ufs = <0x1000000 0x1000000>` is LK/preloader handoff data — read-only.
- **Can we skip re-init? YES — that is the whole design.** XNU-HC proves a second driver can drive
  a firmware-initialized UFS controller with *zero* reset, link startup, UIC command, interrupt
  enable, aggregation, clock or power writes. It only swaps the transfer list. Our case is the
  same, with LK replacing UEFI as the owner. Do not run `ufshcd_probe()` / `ufs_mtk_init()` from
  U-Boot in our payload.

### 5.2 SMC calls to AVOID — VERIFIED: UB-SIP
```
MTK_SIP_UFS_CONTROL = ARM_SMCCC_CALL_VAL(FAST_CALL, SMC_64, OWNER_SIP, 0x276)
UFS_MTK_SIP_DEVICE_RESET          = BIT(1) = 0x2
UFS_MTK_SIP_REF_CLK_NOTIFICATION  = BIT(3) = 0x8
```
Do **not** issue `smc(0x276, 0x2, ...)` (device reset) or `smc(0x276, 0x8, on, stage, ...)`
(refclk notify). U-Boot calls the reset when it is the first non-secure bootloader without
`CONFIG_UFS_MEDIATEK_EL3`; that is **not** us — LK already owns the link. Calling either would
drop the link and/or fight ATF over refclk ownership. U-Boot's `ufs_mtk_setup_ref_clk()` also
writes `0x144` with `REFCLK_REQUEST=1` and polls `REFCLK_ACK` before the SMC — skip that function
entirely. (The `mnnoee` fork's `MTK_SIP_BL_UFS_CONTROL` mentioned in PRIOR is a *different*
downstream SMC; treat as untrusted here.)

### 5.3 Other calls/registers to avoid
- `REG_CONTROLLER_ENABLE` 0x34 (HCE): do not write. If you absolutely must re-init, the exact
  U-Boot flow is: `wr32(0x34, 0)` + poll HCE==0 (10 ms), `mdelay(1)`, `wr32(0x34, 1)`, poll until
  HCE reads 1 (up to 10 × 5 ms), then program UTRLBA/U, `wmb`, verify `HCS & 0x0E == 0x0E`, then
  set UTRLRSR=1 (and UTMRLRSR=1 only if you own that list).
- `REG_UFS_XOUFS_CTRL 0x140`: U-Boot ORs `0x80` to enable CLK_CG early because an abnormal
  ERR_CHK signal can hang the host. LK has been up long enough to have done this; do not toggle.
- `REG_UFS_REFCLK_CTRL 0x144`: refclk handshake requires the SMC above.
- DT reset controls `hci_rst`/`unipro_rst`/`crypto_rst` (indices 1/2/3): do not assert.
- `REG_UIC_COMMAND 0x90` and args `0x94..0x9C`: DME GET/SET and LINK_STARTUP are LK's job.
- Power-mode change (`PA_PWRMODE`) and AH8 hibernate: LK's job.
- `REG_UFS_MEM_CFG 0x300` bit 0 `MCQ_MODE_SELECT`: keep 0. Do not enable MCQ/SQ/CQ banks.

### 5.4 Quirks worth encoding — VERIFIED: UB-MTK + UB-REG
- `UFSHCD_QUIRK_MCQ_BROKEN_INTR (1<<20)` — use legacy doorbell, not MCQ interrupts.
- `UFSHCD_QUIRK_BROKEN_LSDBS_CAP (1<<25)` — CAP may *claim* legacy single-doorbell unsupported
  while it actually works. Do not gate on that CAP bit.
- `UFSHCI_QUIRK_SKIP_MANUAL_WB_FLUSH_CTRL (1<<12)` — irrelevant for reads.
- `UFSHCD_QUIRK_PRDT_BYTE_GRAN (1<<9)` — if set, PRDTO/PRDTL in UTRD are in bytes, not dwords.
  U-Boot's MTK driver does **not** set this quirk, so use dword granularity (`0x0080`, `0x0100`).
- HCI version is read at runtime from `0x08`; do not assume a constant. XNU-HC only accepts
  `0x200..0x3FF`; U-Boot accepts 1.0/1.1/2.0/2.1/3.0/3.1/4.0.
- IP version at `0x2240` is read at runtime; MT6789 is likely an older/legacy IP (kernel logs
  `"legacy IP version - 0x%x"` appear in KO-MTK). The MT6989+ DDR_EN workaround does not apply.

### 5.5 House style to mirror — VERIFIED: MSDC
`static uint64_t ufs_base`, `#define UFS_* 0xNN`, `rd32`/`wr32` volatile 32-bit accessors, a
`struct ufs_err { const char *what; uint32_t status; }` + `ufs_last`, bounded spin loops with an
explicit failure return, no interrupts, no dynamic allocation, 32-bit-only accesses, no SIMD.
Do not use U-Boot's `dma_map`/`dev_phys_to_bus` — with MMU off, physical address == virtual address;
just use the physical pointer value in UTRD/PRDT.

---

## 6. VERIFIED vs INFERENCE — honest ledger

| Claim | Status | Evidence |
|---|---|---|
| All standard register offsets and bit masks in §1.1/§1.3 | **VERIFIED** | UB-REG; key offsets independently in XNU-HC |
| `UTRLCNR` at `0x64` | **VERIFIED** | XNU-HC only (not in U-Boot) |
| MTK vendor offsets `0x140/0x144/0x160/0x2100/0x2200/0x2240/0x22C8/0x2800` | **VERIFIED** | UB-MTK-H; several confirmed by KO-MTK dump labels |
| `0x170`, `0x22F0`, `0x2308` are MTK-private | **INFERENCE** | KO-MTK labels only, no definition in any source |
| UTRD layout, 32-byte size, DW6/DW7 dword-granular offsets | **VERIFIED** | UB-REG + UB-CORE `ufshcd_host_memory_configure` + XNU-HC `hc_place` |
| DW0 = `0x14000000` (or `0x15000000` with I bit), DD_IN=2, CT=1 | **VERIFIED** | UB-REG/UB-CORE + XNU-HC |
| UCD layout `512 + 512 + 128*16 = 0xC00`, 1 KiB alignment | **VERIFIED** | UB-REG + UB-CORE `ufshcd_memory_alloc` + XNU-HC constants |
| PRDT `size = byte_count-1`, max 256 KiB per entry | **VERIFIED** | UB-CORE `prepare_prdt_desc` + XNU-HC `hc_place` |
| UPIU header byte semantics and 32-byte command UPIU | **VERIFIED** | UB-REG + UB-CORE `ufshcd_prepare_utp_scsi_cmd_upiu` + XNU-HC |
| READ(10) CDB bytes (`0x28`, LBA bytes 18-21, length bytes 23-24) | **VERIFIED** | XNU-HC `hc_make_read()` |
| `exp_data_transfer_len` must equal `blocks * block_size` | **VERIFIED** | XNU-HC `hc_refusal()` + UB-CORE |
| Doorbell write `1<<slot`, completion = bit cleared | **VERIFIED** | UB-CORE `ufshcd_send_command()` + XNU-HC `hc_complete()`/`hc_poller()` |
| IS is W1C and may be used instead of doorbell polling | **VERIFIED** | UB-CORE |
| Data comes via PRDT, not via UTRLDBR | **VERIFIED** | UB-CORE `prepare_prdt_table` + XNU-HC `hc_place`; no data register exists in UB-REG |
| Completion checks: OCS=0, rsp 0x21, tag, r[6], r[7], residual | **VERIFIED** | UB-CORE `ufs_scsi_exec` + XNU-HC `hc_complete` |
| LK already initialized UFS and owns it before our payload | **VERIFIED** (presence of full LK stack) / **INFERENCE** (exact live register state) | PRIOR, `ufs-uboot-analysis.md` §3 |
| Skipping reset/link/UIC/clk/SMC is safe | **VERIFIED as a working pattern** on Qualcomm UEFI; **INFERENCE for MT6789 LK** | XNU-HC header comment + probe checks |
| Exact SMC IDs to avoid (`0x276`, subcommands `0x2`/`0x8`) | **VERIFIED** | UB-SIP |
| MT6789 HCI/IP version values | **INFERENCE** — read `0x08` and `0x2240` at runtime | KO-MTK logs show runtime reads |
| GPT is 4096-byte blocks, header at LBA 1, `"EFI PART"` | **VERIFIED for the ported Darwin volume**; **INFERENCE for other LUNs** | XNU-UFS |
| Cache maintenance needed only if D-cache is on with MMU off | **INFERENCE** | XNU-HC does it with caches on; MSDC does not (assumed off) |
| Restoring LK's UTRLBA on exit is optional | **INFERENCE** | XNU-HC restores UEFI's because UEFI resumes; our loader does not |

---

## 7. Minimal "function list" to implement (no code)

1. `ufs_probe_ready()` — §4.0 checks only; record `cap`, `ver`, old `UTRLBA/U`, `IS`.
2. `ufs_list_install()` — §4.1; stop RSR, point UTRLBA/U at our 32-UTRD array, restart, verify.
3. `ufs_build_read(lun, lba, blocks, dst_phys)` — §4.2/§4.3: UTRD slot 0, 32-byte command
   UPIU, PRDT chunks <= 0x40000 bytes, `exp_data_transfer_len = blocks*block_size`.
4. `ufs_doorbell(slot=0)` — §4.4.
5. `ufs_wait_slot(slot=0)` — §4.5 Option A (UTRLDBR bit clears), 10 s bound.
6. `ufs_check_result()` — §4.6 (OCS, response UPIU, tag, status, residual); data already in `dst`.
7. `ufs_list_restore()` — §4.7 (optional).
8. Error reporting in the `msdc.h` style: `ufs_last.what`, `ufs_last.status`.

Never implemented: reset, HCE write, UIC/DME, clock/refclk, power mode, IE/IS enable, MCQ,
task-management list, rpml/BOOT-LU writes.
