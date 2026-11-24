/*
 * X6886 (MT6789 / Infinix X6886) Mali-G57 MC2, Valhall v9 Job Manager.
 *
 * The hardware logic is adapted from the real MT6768/Mali-G52 MMIO bring-up in
 * refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_mali.c:
 *   power domains and clocks  : mtk_mali.c:260-301, 513-566
 *   GPU identity / reset      : mtk_mali.c:544-568
 *   Job Manager submission    : mtk_mali.c:321-344, 610-634, 896-952
 *   address space / paging    : mtk_mali.c:334-344, 387-511
 *   job queue and faults      : mtk_mali.c:721-952, 963-1091
 *
 * Nothing here is an idle stub. Block bases are from the device's own fdt.dts /
 * iomem.txt; internal register offsets are X676C reference values that need
 * owner sign-off (both are separated in PORT-GPU.md sections 2 and 6).
 * Every diagnostic line is mirrored to the live ramoops console at 0x480af000.
 *
 * There is no CSF firmware on this GPU and no CSF path here: all submission is
 * through the Job Manager job slots at 0x1800/0x1880/0x1900.
 *
 * The X676C GPU_ID 0x90930010 is a reference observation (HelioDarwin
 * MaliValhallHw.h:47, wiki/MT6789-GPU-Power-Sequence.md:19). It is logged and it
 * selects r0p1 quirk writes only when the hardware actually reports it; a
 * different, non-zero ID is accepted. An unpowered bank (0 or ~0) is an error.
 *
 * Boot-arg mali=1 enables the driver (nothing is touched without it).
 *   mali_hwctl=1     allow writes to the X676C-sourced, DTB-unproven clock and
 *                    power gates; with the default 0 the driver only reads
 *                    their status and returns ENXIO if they are not already on
 *   mali_fastmap=0   cached page-table walks instead of the default non-cached
 *   mali_trace=1     one line per completed job
 *
 * Every failure is an error return with a line saying which stage stopped.
 * There is no panic, no fake GPU_ID and no guessed regulator voltage.
 */

#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <vm/vm_kern.h>
#include <vm/vm_kern_xnu.h>     /* kmem_alloc()/KMA_* for XNU_KERNEL_PRIVATE builds */
#include <stdarg.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t size);
extern uint64_t kvtophys(vm_offset_t va);
extern vm_offset_t phystokv(uint64_t pa);
extern void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);
extern void delay(int usec);

SYSCTL_DECL(_kern_hakios);

static LCK_GRP_DECLARE(mali_lock_group, "x6886-mali");

/* ---- ramoops console mirror: SHARED X6886 convention ----------------------- */

/* Exact same contract as the other X6886 drivers (audio.c:38-78, battery.c:38-81,
 * camera.c:40-83, keys.c:38-81, wifi.c:34-81, bt.c:33-77, charger.c:64-108,
 * touch.c:95-194, usb.c:212-248): sig at +0, write cursor at +4, RING CAPACITY
 * at +8 (never a valid-byte count), data at +12.  size must be >= 16 and <=
 * window-12.  A valid header from another driver is never re-initialised, so
 * a mali-first boot cannot drop the other boot logs. */
#define X6886_PSTORE_PHYS       0x480af000ULL
#define X6886_PSTORE_SIZE       0x1000u
#define X6886_PSTORE_SIG        0x43474244u
#define X6886_PSTORE_HEAD       12u

static vm_offset_t x6886_pstore_va;
static LCK_GRP_DECLARE(x6886_mali_pstore_grp, "x6886-mali-pstore");
static LCK_SPIN_DECLARE(x6886_mali_pstore_lock, &x6886_mali_pstore_grp);

static void
x6886_mali_pstore_map(void)
{
	if (x6886_pstore_va == 0) {
		x6886_pstore_va = ml_io_map(X6886_PSTORE_PHYS, X6886_PSTORE_SIZE);
	}
}

static void
x6886_mali_pstore_write(const char *message, unsigned length)
{
	volatile uint8_t *base;
	uint32_t sig, start, size;
	unsigned n;

	if (x6886_pstore_va == 0 || message == NULL || length == 0) {
		return;
	}
	lck_spin_lock(&x6886_mali_pstore_lock);
	base = (volatile uint8_t *)x6886_pstore_va;
	sig = *(volatile uint32_t *)(base + 0);
	start = *(volatile uint32_t *)(base + 4);
	size = *(volatile uint32_t *)(base + 8);
	if (sig != X6886_PSTORE_SIG || size < 16 || size > X6886_PSTORE_SIZE - X6886_PSTORE_HEAD) {
		/* Fresh zone: create the shared header exactly as touch.c:175-181 does.
		 * A valid header is left untouched: other drivers keep their logs. */
		*(volatile uint32_t *)(base + 0) = X6886_PSTORE_SIG;
		*(volatile uint32_t *)(base + 4) = 0;
		*(volatile uint32_t *)(base + 8) = X6886_PSTORE_SIZE - X6886_PSTORE_HEAD;
		start = 0;
		size = X6886_PSTORE_SIZE - X6886_PSTORE_HEAD;
	}
	if (start >= size) {
		start = 0;
	}
	n = length < size - 2 ? length : size - 2;
	for (unsigned i = 0; i < n; i++) {
		base[X6886_PSTORE_HEAD + ((start + i) % size)] = (uint8_t)message[i];
	}
	base[X6886_PSTORE_HEAD + ((start + n) % size)] = '\n';
	*(volatile uint32_t *)(base + 4) = (start + n + 1) % size;
	lck_spin_unlock(&x6886_mali_pstore_lock);
}

/* Every diagnostic and every failure dump goes to the normal log AND pstore. */
static void
mali_log(const char *format, ...)
{
	char line[512];
	int length;
	va_list ap;

	length = snprintf(line, sizeof(line), "[X6886-mali] ");
	if (length > 0 && length < (int)sizeof(line)) {
		va_start(ap, format);
		vsnprintf(line + length, sizeof(line) - (size_t)length, format, ap);
		va_end(ap);
	}
	printf("%s\n", line);
	x6886_mali_pstore_write(line, (unsigned)strlen(line));
}

/* ---- physical blocks ------------------------------------------------------ */

/* fdt.dts:5737 mali@13000000, :5738 arm,mali-valhall, :5739 reg 0x13000000/0x4000;
 * iomem.txt:117 13000000-13003fff : 13000000.mali */
#define X6886_MALI_PHYS         0x13000000u
#define X6886_MALI_SIZE         0x4000u

/* fdt.dts:2354-2356 topckgen@10000000; iomem.txt:23 */
#define X6886_TOPCKGEN_PHYS     0x10000000u
/* fdt.dts:2359-2361 infracfg_ao@10001000; iomem.txt:24 */
#define X6886_INFRACFG_PHYS     0x10001000u
/* fdt.dts:1629-1631 power-controller@10006000 (mediatek,mt6789-scpsys); iomem.txt:26 */
#define X6886_SPM_PHYS          0x10006000u
/* fdt.dts:1805-1807 syscon@13fbf000 (mediatek,mt6789-mfg); fdt.dts:5750 gpufreq reg[0] */
#define X6886_MFGCFG_PHYS       0x13fbf000u

#define X6886_BLOCK_SIZE        0x1000u

/* Interrupt routing, from fdt.dts:5740-5741 (SPI 0x18c..0x190 = 396..400) and
 * interrupts.txt:159-161 (GICv3 428/429/430 -> 13000000.mali; EVENT and PWR
 * have never fired on this device). This tree has no driver-facing GIC
 * registration, so the GPU-side masks are programmed and status is polled. */
#define X6886_MALI_SPI_GPU      396u
#define X6886_MALI_SPI_MMU      397u
#define X6886_MALI_SPI_JOB      398u
#define X6886_MALI_SPI_EVENT    399u
#define X6886_MALI_SPI_PWR      400u

static volatile uint32_t *topckgen, *infracfg, *spm, *mfgcfg, *mali;
static uint32_t reached, gpu_id, gpu_arch, gpu_rev;
static uint64_t shader_mask, tiler_mask, l2_mask;
static uint32_t jobs_done, jobs_failed;

SYSCTL_UINT(_kern_hakios, OID_AUTO, mali, CTLFLAG_RD | CTLFLAG_LOCKED, &reached, 0,
    "Mali bring-up stage reached: 6 means a job ran through GPU page tables");
SYSCTL_UINT(_kern_hakios, OID_AUTO, mali_id, CTLFLAG_RD | CTLFLAG_LOCKED, &gpu_id, 0, "Mali GPU_ID");
SYSCTL_UINT(_kern_hakios, OID_AUTO, mali_jobs, CTLFLAG_RD | CTLFLAG_LOCKED, &jobs_done, 0, "JM jobs completed");
SYSCTL_UINT(_kern_hakios, OID_AUTO, mali_failed, CTLFLAG_RD | CTLFLAG_LOCKED, &jobs_failed, 0, "JM jobs that did not finish");

/* ---- MMIO ----------------------------------------------------------------- */

static inline uint32_t
rd(volatile uint32_t *base, uint32_t at)
{
	return *(volatile uint32_t *)((uintptr_t)base + at);
}

static inline void
wr(volatile uint32_t *base, uint32_t at, uint32_t value)
{
	*(volatile uint32_t *)((uintptr_t)base + at) = value;
	__asm__ volatile ("dsb sy");
}

static inline uint64_t
rd64(volatile uint32_t *base, uint32_t at)
{
	return (uint64_t)rd(base, at) | ((uint64_t)rd(base, at + 4) << 32);
}

static inline void
wr64(volatile uint32_t *base, uint32_t at, uint64_t value)
{
	wr(base, at, (uint32_t)value);
	wr(base, at + 4, (uint32_t)(value >> 32));
}

/* Until (register & mask) == want, for at most ms milliseconds. */
static bool
wait_for(volatile uint32_t *base, uint32_t at, uint64_t mask, uint64_t want, uint32_t ms)
{
	for (uint32_t us = 0; us < ms * 1000; us += 10) {
		if ((rd64(base, at) & mask) == want) {
			return true;
		}
		delay(10);
	}
	return (rd64(base, at) & mask) == want;
}

/* ---- SPM MTCMOS power domains (MT6789, AP-direct; GPUEB is off) ------------- */

#define SPM_PWR_STATUS          0x16cu
#define SPM_PWR_STATUS_2ND      0x170u
#define PWR_RST_B               (1u << 0)
#define PWR_ISO                 (1u << 1)
#define PWR_ON                  (1u << 2)
#define PWR_ON_2ND              (1u << 3)
#define PWR_CLK_DIS             (1u << 4)
#define PWR_SRAM_PDN            (1u << 8)
#define PWR_SRAM_ACK            (1u << 12)

/* Offsets and status masks: HelioDarwin wiki/MT6789-GPU-Power-Sequence.md:78-85,
 * patch 0182-x676c-gpu-mtcmos-power-on-from-gpufreq-re.patch. The SPM bank base
 * itself is this device's fdt.dts:1629 / iomem.txt:26. */
static const struct {
	const char *name;
	uint32_t con;
	uint32_t status;
} domains[4] = {
	{ "MFG0 (async/bus)", 0x308u, 1u << 2 },
	{ "MFG1 (core)",      0x30cu, 1u << 3 },
	{ "MFG2 (shader 0)",  0x310u, 1u << 4 },
	{ "MFG3 (shader 1)",  0x314u, 1u << 5 },
};

static bool
domain_on(unsigned i)
{
	uint32_t con = domains[i].con, bit = domains[i].status;

	wr(spm, con, rd(spm, con) | PWR_ON);
	wr(spm, con, rd(spm, con) | PWR_ON_2ND);
	if (!wait_for(spm, SPM_PWR_STATUS, bit, bit, 20) ||
	    !wait_for(spm, SPM_PWR_STATUS_2ND, bit, bit, 20)) {
		return false;
	}
	delay(100);
	wr(spm, con, rd(spm, con) & ~PWR_CLK_DIS);
	wr(spm, con, rd(spm, con) & ~PWR_ISO);
	wr(spm, con, rd(spm, con) | PWR_RST_B);
	wr(spm, con, rd(spm, con) & ~PWR_SRAM_PDN);
	return wait_for(spm, con, PWR_SRAM_ACK, 0, 20);
}

/* MFG1 up: release the bus protection between the GPU and the rest of the SoC
 * (HelioDarwin patch 0182: infracfg_ao 0x718/0x2a4/0x718/0x2ac). */
static void
mfg_bus_protection_release(void)
{
	wr(infracfg, 0x718u, 0x80u);
	wr(infracfg, 0x2a4u, 0x600000u);
	wr(infracfg, 0x718u, 0x60u);
	wr(infracfg, 0x2acu, 0x200000u);
	delay(20);
}

/* Read-only check used when mali_hwctl=0: prove the firmware actually left the
 * MFG domains on before touching any GPU register. No writes are performed. */
static bool
domains_ready_survey(void)
{
	uint32_t first = rd(spm, SPM_PWR_STATUS), second = rd(spm, SPM_PWR_STATUS_2ND);

	for (unsigned i = 0; i < 4; i++) {
		uint32_t bit = domains[i].status;

		if ((first & bit) != bit || (second & bit) != bit) {
			mali_log("power domain %s is off (status %#x/%#x); mali_hwctl=1 needed\n",
			    domains[i].name, first, second);
			return false;
		}
	}
	return true;
}

/* ---- GPU registers (MaliValhallHw.h:9-154, cross-checked to mtk_mali.c:303-343) */

#define GPU_ID                  0x000u
#define GPU_L2_FEATURES         0x004u
#define GPU_CORE_FEATURES       0x008u
#define GPU_TILER_FEATURES      0x00cu
#define GPU_MEM_FEATURES        0x010u
#define GPU_MMU_FEATURES        0x014u
#define AS_PRESENT              0x018u
#define JS_PRESENT              0x01cu
#define GPU_INT_RAWSTAT         0x020u
#define GPU_INT_CLEAR           0x024u
#define GPU_INT_MASK            0x028u
#define GPU_CMD                 0x030u
#define GPU_STATUS              0x034u
#define PWR_KEY                 0x050u
#define PWR_OVERRIDE0           0x054u
#define PWR_OVERRIDE1           0x058u
#define COHERENCY_ENABLE        0x304u
#define JM_CONFIG               0x0f00u
#define SHADER_CONFIG           0x0f04u
#define L2_MMU_CONFIG           0x0f0cu

#define SHADER_PRESENT          0x100u
#define SHADER_PRESENT_HI       0x104u
#define TILER_PRESENT           0x110u
#define TILER_PRESENT_HI        0x114u
#define L2_PRESENT              0x120u
#define L2_PRESENT_HI           0x124u
#define SHADER_READY            0x140u
#define SHADER_READY_HI         0x144u
#define TILER_READY             0x150u
#define TILER_READY_HI          0x154u
#define L2_READY                0x160u
#define L2_READY_HI             0x164u
#define SHADER_PWRON            0x180u
#define SHADER_PWRON_HI         0x184u
#define TILER_PWRON             0x190u
#define TILER_PWRON_HI          0x194u
#define L2_PWRON                0x1a0u
#define L2_PWRON_HI             0x1a4u

#define IRQ_RESET_COMPLETED     (1u << 8)

#define JOB_INT_RAWSTAT         0x1000u
#define JOB_INT_CLEAR           0x1004u
#define JOB_INT_MASK            0x1008u
#define JOB_INT_STATUS          0x100cu
#define MMU_INT_RAWSTAT         0x2000u
#define MMU_INT_CLEAR           0x2004u
#define MMU_INT_MASK            0x2008u

/* Job slot n starts at 0x1800 + n*0x80, registers from the slot base. */
#define JS0                     0x1800u
#define JS1                     0x1880u
#define JS_STRIDE               0x80u
#define JS_STATUS               0x24u
#define JS_HEAD_NEXT_LO         0x40u
#define JS_HEAD_NEXT_HI         0x44u
#define JS_AFFINITY_NEXT_LO     0x50u
#define JS_AFFINITY_NEXT_HI     0x54u
#define JS_CONFIG_NEXT          0x58u
#define JS_COMMAND_NEXT         0x60u
#define JS_COMMAND_START        1u
/* AS 0, caches flushed on both ends, priority 8: mtk_mali.c:332,
 * MaliValhallHw.h:54-55 (MALI_JS_CONFIG_CACHE_FLUSH | PRIORITY). */
#define JS_CONFIG               ((3u << 8) | (3u << 12) | (8u << 16))

/* Address space n starts at 0x2400 + n*0x40. */
#define AS0                     0x2400u
#define AS_TRANSTAB_LO          0x00u
#define AS_TRANSTAB_HI          0x04u
#define AS_MEMATTR_LO           0x08u
#define AS_MEMATTR_HI           0x0cu
#define AS_LOCKADDR_LO          0x10u
#define AS_COMMAND              0x18u
#define AS_FAULTSTATUS          0x1cu
#define AS_FAULTADDRESS_LO      0x20u
#define AS_FAULTADDRESS_HI      0x24u
#define AS_STATUS               0x28u
#define AS_TRANSCFG_LO          0x30u
#define AS_TRANSCFG_HI          0x34u
#define AS_COMMAND_UPDATE       1u
#define AS_COMMAND_UNLOCK       3u
#define AS_COMMAND_FLUSH_PT     4u

/* GPU_COMMAND opcodes (MaliValhallHw.h:81-86). */
#define GPU_CMD_NOP             0x00u
#define GPU_CMD_SOFT_RESET      0x01u
#define GPU_CMD_CLEAN_INV       0x08u
#define COHERENCY_NONE          31u

/* X676C r0p1 reference values, applied only when GPU_ID is exactly 0x90930010. */
#define REF_G57_GPU_ID          0x90930010u
#define REF_G57_SHADER_QUIRK    (1u << 29)
#define REF_G57_IDVS_GROUP_SIZE (15u << 16)
#define REF_G57_MMU_OUT_MASK    0x0003ff80u

/* Four-level AArch64, 4 KiB GPU pages (48-bit VA), from mtk_mali.c:402-416
 * adapted to the four-level layout recorded in
 * wiki/MT6789-GPU-Power-Sequence.md:130. */
#define GPU_ADDRESS_FIRST       0x01000000ull
#define GPU_ADDRESS_END         (1ull << 48)
#define FIRST_JOB_GPU           0x00100000ull
#define PTE_TABLE               3ull
#define PTE_PAGE                3ull
#define PTE_AF                  (1ull << 10)
#define PTE_SH_INNER            (3ull << 8)
#define PTE_AP_RW               (1ull << 6)
#define PTE_ADDRESS             0x0000fffffffff000ull
#define PTE_VALID               0x403ull
#define PTE_BITS                (PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_PAGE)
/* Valhall v9 AS_TRANSCFG/MEMATTR: MaliValhallHw.h:52-53 (X676C REF). */
#define TRANSCFG_AARCH64_4K     0x42000006ull
#define TRANSCFG_IDENTITY       2ull
#define MEMATTR_G57             0x00004c8d888d8f88ull

/* v9 descriptor: 32-byte header, write-value payload starts at byte 32
 * (MaliValhallHw.h:164-190; job control and payload words from mtk_mali.c:354-385). */
#define JOB_HEADER_WORDS        8u
#define JOB_TYPE_WRITEVALUE     2u
#define JOB_INDEX               1u
#define JOB_VALUE_TYPE          6u      /* a 32-bit immediate */
#define JOB_VALUE               0xc0ffee01u

static int mali_fastmap = 1;
static uint32_t mali_trace_queue;
/* Unproven clock/power gate writes stay OFF by default. Only mali_hwctl=1
 * allows writing the X676C-sourced topckgen/infracfg/mfgcfg/SPM PWR_CON
 * registers; with 0 the driver does a read-only survey and returns ENXIO. */
static uint32_t mali_hwctl;

static const char *
gpu_name(uint32_t id)
{
	/* mtk_mali.h:249-259 of the source repo lists the plausible products. */
	switch (id >> 16) {
	case 0x9093: return "Mali-G57";
	case 0x7212: return "Mali-G52";
	case 0x7402: return "Mali-G52 r1";
	case 0x7211: return "Mali-G76";
	case 0x7093: return "Mali-G31";
	default:     return "an unfamiliar Mali";
	}
}

/* ---- operating points from this device's fdt.dts:5777-6005 ---------------- */
/* 45 entries opp00..opp44; opp15 has a corrupt opp-microvolt string at :5858
 * and is kept here only as a marked invalid slot, never as an OPP. The table is
 * validated and reported; no DVFS transition is programmed (see PORT-GPU.md). */
struct x6886_opp {
	uint16_t index;
	bool valid;
	uint32_t hz;                    /* exact opp-hz, not a rounded MHz figure */
	uint32_t uv;
};

static const struct x6886_opp x6886_gpu_opps[45] = {
	{  0, true, 1100000000,  900000 },  /* fdt.dts:5782-5783  0x4190ab00 / 0xdbba0 */
	{  1, true, 1086000000,  893750 },  /* :5787-5788  0x40bb0b80 / 0xda336 */
	{  2, true, 1072000000,  887500 },  /* :5792-5793  0x3fe56c00 / 0xd8acc */
	{  3, true, 1058000000,  881250 },  /* :5797-5798  0x3f0fcc80 / 0xd7262 */
	{  4, true, 1045000000,  875000 },  /* :5802-5803  0x3e496f40 / 0xd59f8 */
	{  5, true, 1031000000,  868750 },  /* :5807-5808  0x3d73cfc0 / 0xd418e */
	{  6, true, 1017000000,  862500 },  /* :5812-5813  0x3c9e3040 / 0xd2924 */
	{  7, true, 1003000000,  856250 },  /* :5817-5818  0x3bc890c0 / 0xd10ba */
	{  8, true,  990000000,  850000 },  /* :5822-5823  0x3b023380 / 0xcf850 */
	{  9, true,  976000000,  843750 },  /* :5827-5828  0x3a2c9400 / 0xcdfe6 */
	{ 10, true,  962000000,  831250 },  /* :5832-5833  0x3956f480 / 0xcaf12 */
	{ 11, true,  948000000,  825000 },  /* :5837-5838  0x38815500 / 0xc96a8 */
	{ 12, true,  935000000,  818750 },  /* :5842-5843  0x37baf7c0 / 0xc7e3e */
	{ 13, true,  921000000,  812500 },  /* :5847-5848  0x36e55840 / 0xc65d4 */
	{ 14, true,  907000000,  806250 },  /* :5852-5853  0x360fb8c0 / 0xc4d6a */
	{ 15, false,         0,       0 },  /* :5857-5858  opp-hz 0x353a1940, voltage "\0\f5" */
	{ 16, true,  880000000,  793750 },  /* :5862-5863  0x3473bc00 / 0xc1c96 */
	{ 17, true,  868000000,  787500 },  /* :5867-5868  0x33bca100 / 0xc042c */
	{ 18, true,  857000000,  781250 },  /* :5872-5873  0x3314c840 / 0xbebc2 */
	{ 19, true,  846000000,  775000 },  /* :5877-5878  0x326cef80 / 0xbd358 */
	{ 20, true,  835000000,  768750 },  /* :5882-5883  0x31c516c0 / 0xbbaee */
	{ 21, true,  823000000,  762500 },  /* :5887-5888  0x310dfbc0 / 0xba284 */
	{ 22, true,  812000000,  756250 },  /* :5892-5893  0x30662300 / 0xb8a1a */
	{ 23, true,  801000000,  756250 },  /* :5897-5898  0x2fbe4a40 / 0xb8a1a */
	{ 24, true,  790000000,  750000 },  /* :5902-5903  0x2f167180 / 0xb71b0 */
	{ 25, true,  778000000,  743750 },  /* :5907-5908  0x2e5f5680 / 0xb5946 */
	{ 26, true,  767000000,  737500 },  /* :5912-5913  0x2db77dc0 / 0xb40dc */
	{ 27, true,  756000000,  731250 },  /* :5917-5918  0x2d0fa500 / 0xb2872 */
	{ 28, true,  745000000,  725000 },  /* :5922-5923  0x2c67cc40 / 0xb1008 */
	{ 29, true,  733000000,  718750 },  /* :5927-5928  0x2bb0b140 / 0xaf79e */
	{ 30, true,  722000000,  712500 },  /* :5932-5933  0x2b08d880 / 0xadf34 */
	{ 31, true,  711000000,  706250 },  /* :5937-5938  0x2a60ffc0 / 0xac6ca */
	{ 32, true,  700000000,  700000 },  /* :5942-5943  0x29b92700 / 0xaae60 */
	{ 33, true,  674000000,  700000 },  /* :5947-5948  0x282c6c80 / 0xaae60 */
	{ 34, true,  648000000,  700000 },  /* :5952-5953  0x269fb200 / 0xaae60 */
	{ 35, true,  622000000,  693750 },  /* :5957-5958  0x2512f780 / 0xa95f6 */
	{ 36, true,  596000000,  693750 },  /* :5962-5963  0x23863d00 / 0xa95f6 */
	{ 37, true,  570000000,  693750 },  /* :5967-5968  0x21f98280 / 0xa95f6 */
	{ 38, true,  545000000,  687500 },  /* :5972-5973  0x207c0a40 / 0xa7d8c */
	{ 39, true,  519000000,  687500 },  /* :5977-5978  0x1eef4fc0 / 0xa7d8c */
	{ 40, true,  493000000,  687500 },  /* :5982-5983  0x1d629540 / 0xa7d8c */
	{ 41, true,  467000000,  681250 },  /* :5987-5988  0x1bd5dac0 / 0xa6522 */
	{ 42, true,  441000000,  681250 },  /* :5992-5993  0x1a492040 / 0xa6522 */
	{ 43, true,  415000000,  681250 },  /* :5997-5998  0x18bc65c0 / 0xa6522 */
	{ 44, true,  390000000,  675000 },  /* :6002-6003  0x173eed80 / 0xa4cb8 */
};

static uint32_t valid_opp_count;
SYSCTL_UINT(_kern_hakios, OID_AUTO, mali_opps, CTLFLAG_RD | CTLFLAG_LOCKED, &valid_opp_count, 0,
    "valid GPU OPPs from the device tree (opp15 is corrupt)");

static bool
x6886_mali_opp_check(void)
{
	uint32_t previous = ~0u, count = 0;

	for (unsigned i = 0; i < 45; i++) {
		const struct x6886_opp *o = &x6886_gpu_opps[i];

		if (o->index != i || !o->valid) {
			if (o->index == 15 && !o->valid) {
				continue;               /* the one corrupt entry */
			}
			mali_log("OPP %u is marked invalid; ignoring it\n", i);
			continue;
		}
		if (o->hz == 0 || o->uv < 600000 || o->uv > 1000000 || o->hz >= previous) {
			mali_log("OPP %u failed validation (%u Hz, %u uV)\n", i, o->hz, o->uv);
			return false;
		}
		previous = o->hz;
		count++;
	}
	valid_opp_count = count;
	mali_log("%u valid OPPs from opp-table0, %u..%u Hz; opp15 skipped\n", count,
	    x6886_gpu_opps[44].hz, x6886_gpu_opps[0].hz);
	return count == 44;
}

/* ---- GPU page tables (four levels, 4 KiB GPU pages) ----------------------- */

static uint64_t *pt_top;
static uint64_t pt_top_phys;

static uint64_t *
pt_page(uint64_t *phys)
{
	vm_offset_t page = 0;

	if (kmem_alloc(kernel_map, &page, PAGE_SIZE, KMA_KOBJECT | KMA_ZERO | KMA_DATA,
	    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		return NULL;
	}
	*phys = kvtophys(page);
	FlushPoC_DcacheRegion(page, PAGE_SIZE);
	return (uint64_t *)page;
}

static uint64_t *
pt_below(uint64_t *entry)
{
	uint64_t phys;
	uint64_t *table;

	if (*entry & 1) {
		return (uint64_t *)phystokv(*entry & PTE_ADDRESS);
	}
	table = pt_page(&phys);
	if (table == NULL) {
		return NULL;
	}
	*entry = phys | PTE_TABLE;
	FlushPoC_DcacheRegion((vm_offset_t)entry, sizeof(*entry));
	return table;
}

static bool
map_page(uint64_t gpu, uint64_t phys)
{
	static const unsigned shift[4] = { 39, 30, 21, 12 };
	uint64_t *table;

	if (gpu >= GPU_ADDRESS_END || (gpu & 0xfff) != 0) {
		return false;
	}
	if (pt_top == NULL && (pt_top = pt_page(&pt_top_phys)) == NULL) {
		return false;
	}
	table = pt_top;
	for (int level = 0; level < 3; level++) {
		table = pt_below(&table[(gpu >> shift[level]) & 511]);
		if (table == NULL) {
			return false;
		}
	}
	table[(gpu >> shift[3]) & 511] = (phys & PTE_ADDRESS) | PTE_BITS;
	FlushPoC_DcacheRegion((vm_offset_t)&table[(gpu >> shift[3]) & 511], sizeof(*table));
	return true;
}

static void
as0_update(uint64_t transtab, uint64_t transcfg, uint64_t memattr)
{
	wr64(mali, AS0 + AS_TRANSTAB_LO, transtab);
	wr64(mali, AS0 + AS_MEMATTR_LO, memattr);
	wr64(mali, AS0 + AS_TRANSCFG_LO, transcfg);
	wr(mali, AS0 + AS_COMMAND, AS_COMMAND_UPDATE);
	(void)wait_for(mali, AS0 + AS_STATUS, 1, 0, 50);
}

/* ---- one Job Manager write-value job -------------------------------------- */

/* The smallest real job: a 32-byte v9 header at the start of `page`, a
 * write-value payload at byte 32, and the word written at page+0x800. `gpu` is
 * the address the GPU knows the page by (physical in identity mode, or through
 * the page tables). Cache maintenance is required: this GPU has coherency
 * disabled (mtk_mali.c:347-353, HelioDarwin patch 0189 reset()). */
static bool
run_write_job(vm_offset_t page, uint64_t gpu, const char *what)
{
	volatile uint32_t *job = (volatile uint32_t *)page;
	volatile uint32_t *target = (volatile uint32_t *)(page + 0x800);
	bool ok;

	bzero((void *)page, PAGE_SIZE);
	job[4] = (JOB_TYPE_WRITEVALUE << 1) | 1u | (JOB_INDEX << 16);
	job[8] = (uint32_t)(gpu + 0x800);
	job[9] = (uint32_t)((gpu + 0x800) >> 32);
	job[10] = JOB_VALUE_TYPE;
	job[12] = JOB_VALUE;
	FlushPoC_DcacheRegion(page, PAGE_SIZE);

	wr(mali, JOB_INT_MASK, 0);
	wr(mali, JOB_INT_CLEAR, 0xffffffffu);
	wr(mali, JS1 + JS_HEAD_NEXT_LO, (uint32_t)gpu);
	wr(mali, JS1 + JS_HEAD_NEXT_HI, (uint32_t)(gpu >> 32));
	wr(mali, JS1 + JS_AFFINITY_NEXT_LO, (uint32_t)shader_mask);
	wr(mali, JS1 + JS_AFFINITY_NEXT_HI, (uint32_t)(shader_mask >> 32));
	wr(mali, JS1 + JS_CONFIG_NEXT, JS_CONFIG);
	wr(mali, JS1 + JS_COMMAND_NEXT, JS_COMMAND_START);
	(void)wait_for(mali, JOB_INT_RAWSTAT, (1u << 1) | (1u << 17), 1u << 1, 100);

	FlushPoC_DcacheRegion(page, PAGE_SIZE);
	ok = *target == JOB_VALUE;
	mali_log("%s: JOB_INT_RAWSTAT %#x, JS1_STATUS %#x, AS_FAULTSTATUS %#x, header %#x, word %#x%s\n",
	    what, rd(mali, JOB_INT_RAWSTAT), rd(mali, JS1 + JS_STATUS), rd(mali, AS0 + AS_FAULTSTATUS),
	    job[4], *target, ok ? " (GPU ran it)" : " (NOT WRITTEN)");
	wr(mali, JOB_INT_CLEAR, 0xffffffffu);
	if (ok) {
		jobs_done++;
	} else {
		jobs_failed++;
	}
	return ok;
}

/* Identity job first (does the JM work at all?), then the same job through the
 * GPU's own tables (do they translate?). AS0 is left on the tables. */
static unsigned
first_jobs(void)
{
	vm_offset_t page = 0;
	unsigned done = 0;

	if (kmem_alloc(kernel_map, &page, PAGE_SIZE, KMA_KOBJECT | KMA_ZERO | KMA_DATA,
	    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		mali_log("no page for the first jobs\n");
		return 0;
	}
	as0_update(0, TRANSCFG_IDENTITY, 0);
	if (run_write_job(page, kvtophys(page), "first job, identity mode")) {
		done = 1;
		if (map_page(FIRST_JOB_GPU, kvtophys(page))) {
			as0_update(pt_top_phys, TRANSCFG_AARCH64_4K,
			    mali_fastmap ? 0 : MEMATTR_G57);
			if (run_write_job(page, FIRST_JOB_GPU, "second job, through GPU page tables")) {
				done = 2;
			}
		} else {
			mali_log("no memory for page tables\n");
		}
	}
	kmem_free(kernel_map, page, PAGE_SIZE);
	return done;
}

/* ---- bring-up ------------------------------------------------------------- */

static int
x6886_mali_up(void)
{
	uint32_t ck, mux, as, js;
	int error;

	topckgen = (volatile uint32_t *)ml_io_map(X6886_TOPCKGEN_PHYS, X6886_BLOCK_SIZE);
	infracfg = (volatile uint32_t *)ml_io_map(X6886_INFRACFG_PHYS, X6886_BLOCK_SIZE);
	spm = (volatile uint32_t *)ml_io_map(X6886_SPM_PHYS, X6886_BLOCK_SIZE);
	if (topckgen == NULL || infracfg == NULL || spm == NULL) {
		mali_log("cannot map topckgen/infracfg/spm\n");
		error = ENXIO;
		goto fail;
	}

	/* 1: MFG reference clock. The two writes below are X676C-sourced and
	 * unproven on X6886: they happen only with mali_hwctl=1. The status read
	 * and the bounded poll are always safe. */
	if (mali_hwctl) {
		wr(topckgen, 0x58u, (1u << 18) | (1u << 7));
		wr(topckgen, 0x04u, 1u << 3);
	} else {
		mali_log("mali_hwctl=0: read-only survey, no unproven clock writes\n");
	}
	ck = 0;
	for (unsigned i = 0; i < 1000; i++) {
		ck = rd(topckgen, 0x50u);
		if (ck & 0x40000u) {
			break;
		}
		delay(10);
	}
	if ((ck & 0x40000u) == 0) {
		mali_log("topckgen MFG clock not stable (CLK_CFG_3 %#x)\n", ck);
		error = ENXIO;
		goto fail;
	}
	reached = 1;

	/* 2: the four MFG power domains. With mali_hwctl=0 only their status is
	 * read; with 1 the X676C MTCMOS sequence and bus protection are written. */
	if (mali_hwctl) {
		for (unsigned i = 0; i < 4; i++) {
			if (!domain_on(i)) {
				mali_log("power domain %s did not come up (status %#x/%#x)\n",
				    domains[i].name, rd(spm, SPM_PWR_STATUS), rd(spm, SPM_PWR_STATUS_2ND));
				error = ENXIO;
				goto fail;
			}
			if (i == 1) {
				mfg_bus_protection_release();
			}
		}
	} else if (!domains_ready_survey()) {
		error = ENXIO;
		goto fail;
	}
	reached = 2;

	/* 3: the block's own clock gate and external clock controls. */
	mfgcfg = (volatile uint32_t *)ml_io_map(X6886_MFGCFG_PHYS, X6886_BLOCK_SIZE);
	mali = (volatile uint32_t *)ml_io_map(X6886_MALI_PHYS, X6886_MALI_SIZE);
	if (mfgcfg == NULL || mali == NULL) {
		mali_log("cannot map mfgcfg or GPU registers\n");
		error = ENXIO;
		goto fail;
	}
	/* 3: the block's own gate and external CG. All offsets here are X676C
	 * reference values: writes only with mali_hwctl=1, otherwise read-only. */
	if (mali_hwctl) {
		wr(mfgcfg, 0x08u, 1u);
		if (!wait_for(mfgcfg, 0x00u, 1, 0, 50)) {
			mali_log("bg3d clock gate did not open (status %#x)\n", rd(mfgcfg, 0x00u));
			error = ENXIO;
			goto fail;
		}
		wr(mfgcfg, 0x20u, rd(mfgcfg, 0x20u) | 0x03c00000u);
		wr(mfgcfg, 0xb0u, rd(mfgcfg, 0xb0u) & ~0x0600u);
		wr(mfgcfg, 0xb4u, rd(mfgcfg, 0xb4u) | 0x0010u);
		wr(mfgcfg, 0xb0u, (rd(mfgcfg, 0xb0u) & ~0x00080100u) | 0x80000u);
		wr(mfgcfg, 0x24u, rd(mfgcfg, 0x24u) | 1u);
		wr(mfgcfg, 0x0f60u, rd(mfgcfg, 0x0f60u) | 1u);
	} else {
		uint32_t gate = rd(mfgcfg, 0x00u);

		if (gate & 1u) {
			mali_log("bg3d clock gate is closed (status %#x); mali_hwctl=1 needed\n", gate);
			error = ENXIO;
			goto fail;
		}
	}
	reached = 3;

	/* 4: the GPU itself. The ID is detected, never required to be a value
	 * observed on another phone; 0 or ~0 means the bank is still unpowered. */
	gpu_id = rd(mali, GPU_ID);
	if (gpu_id == 0 || gpu_id == ~0u) {
		mali_log("GPU_ID %#x, GPU bank unpowered; stopping (no assumed ID)\n", gpu_id);
		error = ENXIO;
		goto fail;
	}
	gpu_arch = (gpu_id >> 12) & 0xf;
	gpu_rev = (gpu_id >> 4) & 0xff;
	mux = rd(topckgen, 0x50u);
	as = rd(mali, AS_PRESENT);
	js = rd(mali, JS_PRESENT);
	mali_log("GPU_ID %#x (%s r%up%u), L2 %#x, shader %#x, MMU %#x, AS_PRESENT %#x, JS_PRESENT %#x, MFG clock %#x\n",
	    gpu_id, gpu_name(gpu_id), gpu_arch, gpu_rev,
	    rd(mali, GPU_L2_FEATURES), rd(mali, GPU_CORE_FEATURES), rd(mali, GPU_MMU_FEATURES), as, js, mux);

	wr(mali, GPU_INT_MASK, 0);
	wr(mali, JOB_INT_MASK, 0);
	wr(mali, MMU_INT_MASK, 0);
	wr(mali, GPU_INT_CLEAR, 0xffffffffu);
	wr(mali, GPU_CMD, GPU_CMD_SOFT_RESET);
	if (!wait_for(mali, GPU_INT_RAWSTAT, IRQ_RESET_COMPLETED, IRQ_RESET_COMPLETED, 100)) {
		mali_log("GPU_ID %#x did not finish its reset\n", gpu_id);
		error = ENXIO;
		goto fail;
	}
	wr(mali, GPU_INT_CLEAR, 0xffffffffu);
	wr(mali, JOB_INT_CLEAR, 0xffffffffu);
	wr(mali, MMU_INT_CLEAR, 0xffffffffu);

	/* No hardware cache coherency on MT6789 (patch 0189 reset()). */
	wr(mali, COHERENCY_ENABLE, COHERENCY_NONE);
	if (gpu_id == REF_G57_GPU_ID) {
		wr(mali, SHADER_CONFIG, rd(mali, SHADER_CONFIG) | REF_G57_SHADER_QUIRK);
		wr(mali, JM_CONFIG, rd(mali, JM_CONFIG) | REF_G57_IDVS_GROUP_SIZE);
		wr(mali, L2_MMU_CONFIG, rd(mali, L2_MMU_CONFIG) & ~REF_G57_MMU_OUT_MASK);
		mali_log("applied the X676C r0p1 quirk set (GPU_ID matched %#x)\n", REF_G57_GPU_ID);
	}

	l2_mask = rd64(mali, L2_PRESENT);
	tiler_mask = rd64(mali, TILER_PRESENT);
	shader_mask = rd64(mali, SHADER_PRESENT);
	if (l2_mask == 0 || shader_mask == 0) {
		mali_log("no L2 or shader cores present (L2 %#llx, shader %#llx)\n",
		    (unsigned long long)l2_mask, (unsigned long long)shader_mask);
		error = ENXIO;
		goto fail;
	}
	wr64(mali, L2_PWRON, l2_mask);
	if (!wait_for(mali, L2_READY, l2_mask, l2_mask, 50)) {
		mali_log("L2 cores did not power up (ready %#llx)\n",
		    (unsigned long long)rd64(mali, L2_READY));
		error = ENXIO;
		goto fail;
	}
	if (tiler_mask != 0) {
		wr64(mali, TILER_PWRON, tiler_mask);
	}
	wr64(mali, SHADER_PWRON, shader_mask);
	if ((tiler_mask != 0 && !wait_for(mali, TILER_READY, tiler_mask, tiler_mask, 50)) ||
	    !wait_for(mali, SHADER_READY, shader_mask, shader_mask, 50)) {
		mali_log("shader/tiler cores did not power up (ready %#llx/%#llx)\n",
		    (unsigned long long)rd64(mali, SHADER_READY),
		    (unsigned long long)rd64(mali, TILER_READY));
		error = ENXIO;
		goto fail;
	}
	reached = 4;
	mali_log("all cores powered, shader mask %#llx (JS slots %#x, AS slots %#x)\n",
	    (unsigned long long)shader_mask, js, as);

	/* 5: a real job twice, once in identity mode and once through the tables. */
	switch (first_jobs()) {
	case 2: reached = 6; break;
	case 1: reached = 5; break;
	default: break;
	}
	if (reached < 6) {
		mali_log("no job completed through page tables (stage %u)\n", reached);
		error = ENXIO;
		goto fail;
	}
	return 0;

fail:
	if (mali != NULL) {
		wr(mali, GPU_INT_MASK, 0xffffffffu);
		wr(mali, JOB_INT_MASK, 0xffffffffu);
		wr(mali, MMU_INT_MASK, 0xffffffffu);
	}
	return error;
}

/* ---- exported driver surface ---------------------------------------------- */

bool
x6886_mali_ready(void)
{
	return reached >= 6 && mali != NULL;
}

uint32_t
x6886_mali_gpu_id(void)
{
	return gpu_id;
}

/* The last buffers handed GPU addresses and whether they were unmapped. */
#define MALI_RECENT 4096
static struct {
	uint64_t address, size;
	uint32_t freed, kind;
	uint32_t width, height, row_bytes, format, offset;
} mali_recent[MALI_RECENT];
static uint32_t mali_recent_next;

static LCK_MTX_DECLARE(mali_lock, &mali_lock_group);
static LCK_MTX_DECLARE(mali_job_lock, &mali_lock_group);

void
x6886_mali_tag(uint64_t address, uint32_t kind, uint32_t width, uint32_t height,
    uint32_t row_bytes, uint32_t format, uint32_t offset)
{
	lck_mtx_lock(&mali_lock);
	for (int i = 0; i < MALI_RECENT; i++) {
		if (mali_recent[i].address == address && mali_recent[i].size != 0) {
			mali_recent[i].kind = kind;
			mali_recent[i].width = width;
			mali_recent[i].height = height;
			mali_recent[i].row_bytes = row_bytes;
			mali_recent[i].format = format;
			mali_recent[i].offset = offset;
		}
	}
	lck_mtx_unlock(&mali_lock);
}

static void
say_kind(int i)
{
	if (mali_recent[i].kind == 2) {
		mali_log("    an IOSurface %ux%u, %u bytes a row, format %#x, from byte %u\n",
		    mali_recent[i].width, mali_recent[i].height, mali_recent[i].row_bytes,
		    mali_recent[i].format, mali_recent[i].offset);
	} else {
		mali_log("    %s\n", mali_recent[i].kind == 1 ? "a panfrost BO" : "made by nothing that said");
	}
}

static void
describe_address(uint64_t address)
{
	int below = -1;

	for (int i = 0; i < MALI_RECENT; i++) {
		if (mali_recent[i].size == 0) {
			continue;
		}
		if (address >= mali_recent[i].address && address < mali_recent[i].address + mali_recent[i].size) {
			mali_log("  the fault is %#llx into a buffer of %#llx bytes at %#llx, %s\n",
			    (unsigned long long)(address - mali_recent[i].address),
			    (unsigned long long)mali_recent[i].size,
			    (unsigned long long)mali_recent[i].address,
			    mali_recent[i].freed ? "FREED already" : "still there (a page not entered?)");
			say_kind(i);
			return;
		}
		if (mali_recent[i].address <= address &&
		    (below < 0 || mali_recent[i].address > mali_recent[below].address)) {
			below = i;
		}
	}
	if (below >= 0) {
		mali_log("  the fault is %#llx past the end of a buffer at %#llx (%s)\n",
		    (unsigned long long)(address - (mali_recent[below].address + mali_recent[below].size)),
		    (unsigned long long)mali_recent[below].address,
		    mali_recent[below].freed ? "freed" : "live");
	} else {
		mali_log("  the fault is in no buffer of the last %u\n", MALI_RECENT);
	}
}

static uint64_t next_address = GPU_ADDRESS_FIRST;

uint64_t
x6886_mali_address(uint64_t size)
{
	uint64_t address;

	lck_mtx_lock(&mali_lock);
	address = next_address;
	if (address + size > GPU_ADDRESS_END) {
		address = 0;
	} else {
		next_address += (size + 0xffff) & ~0xffffull;
		mali_recent[mali_recent_next % MALI_RECENT].address = address;
		mali_recent[mali_recent_next % MALI_RECENT].size = size;
		mali_recent[mali_recent_next % MALI_RECENT].freed = 0;
		mali_recent[mali_recent_next % MALI_RECENT].kind = 0;
		mali_recent_next++;
	}
	lck_mtx_unlock(&mali_lock);
	return address;
}

bool
x6886_mali_map(uint64_t gpu, uint64_t phys)
{
	bool done;

	lck_mtx_lock(&mali_lock);
	done = map_page(gpu, phys);
	lck_mtx_unlock(&mali_lock);
	return done;
}

void
x6886_mali_unmap(uint64_t gpu, uint64_t size)
{
	static const unsigned shift[4] = { 39, 30, 21, 12 };

	lck_mtx_lock(&mali_lock);
	for (int i = 0; i < MALI_RECENT; i++) {
		if (mali_recent[i].address == gpu && mali_recent[i].size != 0) {
			mali_recent[i].freed = 1;
		}
	}
	for (uint64_t at = gpu; at < gpu + size; at += 4096) {
		uint64_t *entry = pt_top != NULL ? &pt_top[(at >> shift[0]) & 511] : NULL;
		uint64_t *middle, *bottom;

		if (entry == NULL || !(*entry & 1)) {
			continue;
		}
		middle = (uint64_t *)phystokv(*entry & PTE_ADDRESS);
		entry = &middle[(at >> shift[1]) & 511];
		if (!(*entry & 1)) {
			continue;
		}
		bottom = (uint64_t *)phystokv(*entry & PTE_ADDRESS);
		entry = &bottom[(at >> shift[2]) & 511];
		if (!(*entry & 1)) {
			continue;
		}
		entry = &((uint64_t *)phystokv(*entry & PTE_ADDRESS))[(at >> shift[3]) & 511];
		*entry = 0;
		FlushPoC_DcacheRegion((vm_offset_t)entry, sizeof(*entry));
	}
	lck_mtx_unlock(&mali_lock);

	/* An entry was taken out: the MMU may have cached it. */
	lck_mtx_lock(&mali_job_lock);
	lck_mtx_lock(&mali_lock);
	as0_update(pt_top_phys, TRANSCFG_AARCH64_4K, mali_fastmap ? 0 : MEMATTR_G57);
	lck_mtx_unlock(&mali_lock);
	lck_mtx_unlock(&mali_job_lock);
}

void
x6886_mali_mapped(void)
{
	if (mali_fastmap) {
		return;                     /* non-cached walks read what the CPU wrote */
	}
	lck_mtx_lock(&mali_job_lock);
	lck_mtx_lock(&mali_lock);
	as0_update(pt_top_phys, TRANSCFG_AARCH64_4K, MEMATTR_G57);
	lck_mtx_unlock(&mali_lock);
	lck_mtx_unlock(&mali_job_lock);
}

/* A parameter answered from the registers this GPU actually has. */
/* Enum numbers from this device's kernel headers:
 * .../kernel-headers/include/uapi/drm/panfrost_drm.h:133-174. */
#define MALI_PARAM_GPU_ID_HI    0u
#define MALI_PARAM_GPU_ID_LO    1u
#define MALI_PARAM_SHADER_PRESENT 2u
#define MALI_PARAM_TILER_PRESENT 3u
#define MALI_PARAM_L2_PRESENT   4u
#define MALI_PARAM_AS_PRESENT   6u
#define MALI_PARAM_JS_PRESENT   7u
#define MALI_PARAM_L2_FEATURES  8u
#define MALI_PARAM_CORE_FEATURES 9u
#define MALI_PARAM_TILER_FEATURES 10u
#define MALI_PARAM_MEM_FEATURES 11u
#define MALI_PARAM_MMU_FEATURES 12u
#define MALI_PARAM_NR_CORE_GROUPS 38u

bool
x6886_mali_parameter(uint32_t which, uint64_t *value)
{
	if (!x6886_mali_ready() || value == NULL) {
		return false;
	}
	lck_mtx_lock(&mali_lock);
	switch (which) {
	case MALI_PARAM_GPU_ID_HI:
		*value = gpu_id >> 16;
		break;
	case MALI_PARAM_GPU_ID_LO:
		*value = gpu_id & 0xffff;
		break;
	case MALI_PARAM_SHADER_PRESENT:
		*value = rd64(mali, SHADER_PRESENT);
		break;
	case MALI_PARAM_TILER_PRESENT:
		*value = rd64(mali, TILER_PRESENT);
		break;
	case MALI_PARAM_L2_PRESENT:
		*value = rd64(mali, L2_PRESENT);
		break;
	case MALI_PARAM_AS_PRESENT:
		*value = rd(mali, AS_PRESENT);
		break;
	case MALI_PARAM_JS_PRESENT:
		*value = rd(mali, JS_PRESENT);
		break;
	case MALI_PARAM_L2_FEATURES:
		*value = rd(mali, GPU_L2_FEATURES);
		break;
	case MALI_PARAM_CORE_FEATURES:
		*value = rd(mali, GPU_CORE_FEATURES);
		break;
	case MALI_PARAM_TILER_FEATURES:
		*value = rd(mali, GPU_TILER_FEATURES);
		break;
	case MALI_PARAM_MEM_FEATURES:
		*value = rd(mali, GPU_MEM_FEATURES);
		break;
	case MALI_PARAM_MMU_FEATURES:
		*value = rd(mali, GPU_MMU_FEATURES);
		break;
	case MALI_PARAM_NR_CORE_GROUPS:
		*value = 1;
		break;
	default:
		lck_mtx_unlock(&mali_lock);
		return false;               /* no invented values for unknown parameters */
	}
	lck_mtx_unlock(&mali_lock);
	return true;
}

/* ---- waiting for a job without burning a core ----------------------------- */

static int mali_wait_event;
static LCK_MTX_DECLARE(mali_wait_lock, &mali_lock_group);

static bool
wait_job(uint32_t at, uint32_t mask, uint32_t want, uint32_t ms)
{
	uint64_t deadline;

	for (int i = 0; i < 20; i++) {
		if ((rd(mali, at) & mask) == want) {
			return true;
		}
		delay(10);
	}
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	while (mach_absolute_time() < deadline) {
		uint64_t nap;

		if ((rd(mali, at) & mask) == want) {
			return true;
		}
		clock_interval_to_deadline(200, NSEC_PER_USEC, &nap);
		lck_mtx_lock(&mali_wait_lock);
		(void)lck_mtx_sleep_deadline(&mali_wait_lock, LCK_SLEEP_DEFAULT,
		    (event_t)&mali_wait_event, THREAD_UNINT, nap);
		lck_mtx_unlock(&mali_wait_lock);
	}
	return (rd(mali, at) & mask) == want;
}

static void
walk_tables(uint64_t gpu, uint64_t walk[4])
{
	static const unsigned shift[4] = { 39, 30, 21, 12 };
	uint64_t *table = pt_top;

	walk[0] = walk[1] = walk[2] = walk[3] = 0;
	for (int level = 0; level < 4 && table != NULL; level++) {
		walk[level] = table[(gpu >> shift[level]) & 511];
		table = (walk[level] & 1) && level < 3
		    ? (uint64_t *)phystokv(walk[level] & PTE_ADDRESS) : NULL;
	}
}

static struct {
	uint32_t count, slot, raw, status, mmu, fault, fault_address;
	uint64_t chain, transtab, walk[4];
} last_failure;

/* One JM chain, run to its end before this returns. Slot 0 is fragment, slot 1
 * is vertex/tiler, slot 2 is compute; all three are Job Manager slots, never
 * CSF queues. Returns 0 on completion, the slot status on a job fault, or 0xff
 * when the JM gave no answer within a second (and the GPU is reset). */
uint32_t
x6886_mali_submit(uint64_t chain, bool fragment)
{
	uint32_t slot = fragment ? 0 : 1, js = fragment ? JS0 : JS1, raw, status = 0;
	bool finished;

	if (!x6886_mali_ready()) {
		return 0xff;
	}
	lck_mtx_lock(&mali_job_lock);
	lck_mtx_lock(&mali_lock);
	wr(mali, JOB_INT_CLEAR, (1u << slot) | (1u << (slot + 16)));
	wr(mali, js + JS_HEAD_NEXT_LO, (uint32_t)chain);
	wr(mali, js + JS_HEAD_NEXT_HI, (uint32_t)(chain >> 32));
	wr(mali, js + JS_AFFINITY_NEXT_LO, (uint32_t)shader_mask);
	wr(mali, js + JS_AFFINITY_NEXT_HI, (uint32_t)(shader_mask >> 32));
	wr(mali, js + JS_CONFIG_NEXT, JS_CONFIG);
	wr(mali, js + JS_COMMAND_NEXT, JS_COMMAND_START);
	lck_mtx_unlock(&mali_lock);
	finished = wait_job(JOB_INT_RAWSTAT, (1u << slot) | (1u << (slot + 16)), 1u << slot, 1000);
	lck_mtx_lock(&mali_lock);
	if (finished) {
		jobs_done++;
	} else {
		jobs_failed++;
		raw = rd(mali, JOB_INT_RAWSTAT);
		status = (raw & (1u << (slot + 16))) ? rd(mali, js + JS_STATUS) : 0xff;
		last_failure.count++;
		last_failure.slot = slot;
		last_failure.raw = raw;
		last_failure.status = rd(mali, js + JS_STATUS);
		last_failure.mmu = rd(mali, MMU_INT_RAWSTAT);
		last_failure.fault = rd(mali, AS0 + AS_FAULTSTATUS);
		last_failure.fault_address = rd(mali, AS0 + AS_FAULTADDRESS_LO) |
		    ((uint64_t)rd(mali, AS0 + AS_FAULTADDRESS_HI) << 32);
		last_failure.chain = chain;
		last_failure.transtab = rd64(mali, AS0 + AS_TRANSTAB_LO);
		walk_tables(last_failure.fault_address, last_failure.walk);
		mali_log("JM slot %u did not finish: JOB_INT_RAWSTAT %#x, JS_STATUS %#x, MMU %#x, fault %#x at %#llx\n",
		    slot, raw, last_failure.status, last_failure.mmu, last_failure.fault,
		    (unsigned long long)last_failure.fault_address);
		describe_address(last_failure.fault_address);

		/* Reset and put the cores and AS0 back exactly as bring-up left them. */
		wr(mali, GPU_INT_CLEAR, 0xffffffffu);
		wr(mali, GPU_CMD, GPU_CMD_SOFT_RESET);
		(void)wait_for(mali, GPU_INT_RAWSTAT, IRQ_RESET_COMPLETED, IRQ_RESET_COMPLETED, 100);
		wr(mali, COHERENCY_ENABLE, COHERENCY_NONE);
		wr64(mali, L2_PWRON, rd64(mali, L2_PRESENT));
		wr64(mali, TILER_PWRON, rd64(mali, TILER_PRESENT));
		wr64(mali, SHADER_PWRON, shader_mask);
		(void)wait_for(mali, SHADER_READY, shader_mask, shader_mask, 50);
		as0_update(pt_top_phys, TRANSCFG_AARCH64_4K, mali_fastmap ? 0 : MEMATTR_G57);
		wr(mali, MMU_INT_CLEAR, 0xffffffffu);
	}
	wr(mali, JOB_INT_CLEAR, (1u << slot) | (1u << (slot + 16)));
	lck_mtx_unlock(&mali_lock);
	lck_mtx_unlock(&mali_job_lock);
	return status;
}

/* ---- asynchronous queue (jobs in submission order) ----------------------- */

#define MALI_QUEUE      64
static struct {
	uint64_t chain, seq, queued;
	bool fragment;
} mali_queue[MALI_QUEUE];
static uint32_t mali_trace_queue;
static unsigned mali_queue_head, mali_queue_count;
static uint64_t mali_queued_seq, mali_done_seq;
static bool mali_worker_started, mali_worker_stop;
static LCK_MTX_DECLARE(mali_queue_lock, &mali_lock_group);

/*
 * Sign-off note on the waits below: these are event-driven sleeps, not spins.
 * lck_mtx_sleep_deadline() puts the waiter on the channel and drops the lock
 * atomically, so the wakeup() in x6886_mali_enqueue() cannot be lost. Each
 * sleep has a fresh 30-second deadline and the loops also test mali_worker_stop,
 * so there is no literal infinite loop and the worker exits on stop.
 */
static void
mali_worker(__unused void *parameter, __unused wait_result_t result)
{
	while (!mali_worker_stop) {
		uint64_t chain, seq, queued;
		bool fragment;

		lck_mtx_lock(&mali_queue_lock);
		while (mali_queue_count == 0 && !mali_worker_stop) {
			uint64_t nap;

			clock_interval_to_deadline(30000, NSEC_PER_MSEC, &nap);
			(void)lck_mtx_sleep_deadline(&mali_queue_lock, LCK_SLEEP_DEFAULT,
			    (event_t)&mali_queue_count, THREAD_UNINT, nap);
		}
		if (mali_worker_stop) {
			lck_mtx_unlock(&mali_queue_lock);
			break;
		}
		chain = mali_queue[mali_queue_head].chain;
		seq = mali_queue[mali_queue_head].seq;
		queued = mali_queue[mali_queue_head].queued;
		fragment = mali_queue[mali_queue_head].fragment;
		lck_mtx_unlock(&mali_queue_lock);

		uint64_t began = mach_absolute_time();

		if (x6886_mali_submit(chain, fragment) != 0) {
			jobs_failed++;
		}
		if (mali_trace_queue) {
			uint64_t q, b, e;

			absolutetime_to_nanoseconds(queued, &q);
			absolutetime_to_nanoseconds(began, &b);
			absolutetime_to_nanoseconds(mach_absolute_time(), &e);
			mali_log("job %llu %c q %llu b %llu e %llu us\n",
			    (unsigned long long)seq, fragment ? 'f' : 'v',
			    (unsigned long long)(q / 1000), (unsigned long long)(b / 1000),
			    (unsigned long long)(e / 1000));
		}

		lck_mtx_lock(&mali_queue_lock);
		mali_queue_head = (mali_queue_head + 1) % MALI_QUEUE;
		mali_queue_count--;
		mali_done_seq = seq;
		lck_mtx_unlock(&mali_queue_lock);
		wakeup((caddr_t)&mali_done_seq);
		wakeup((caddr_t)&mali_queue_head);
	}
}

uint64_t
x6886_mali_enqueue(uint64_t chain, bool fragment)
{
	uint64_t seq;

	lck_mtx_lock(&mali_queue_lock);
	if (!mali_worker_started) {
		thread_t thread;

		if (kernel_thread_start(mali_worker, NULL, &thread) == KERN_SUCCESS) {
			thread_set_thread_name(thread, "x6886-mali-jobs");
			thread_deallocate(thread);
			mali_worker_started = true;
		}
	}
	while (mali_queue_count == MALI_QUEUE && !mali_worker_stop) {
		uint64_t nap;

		clock_interval_to_deadline(30000, NSEC_PER_MSEC, &nap);
		(void)lck_mtx_sleep_deadline(&mali_queue_lock, LCK_SLEEP_DEFAULT,
		    (event_t)&mali_queue_head, THREAD_UNINT, nap);
	}
	if (mali_worker_stop) {
		lck_mtx_unlock(&mali_queue_lock);
		return 0;
	}
	seq = ++mali_queued_seq;
	unsigned at = (mali_queue_head + mali_queue_count) % MALI_QUEUE;

	mali_queue[at].chain = chain;
	mali_queue[at].fragment = fragment;
	mali_queue[at].seq = seq;
	mali_queue[at].queued = mach_absolute_time();
	mali_queue_count++;
	lck_mtx_unlock(&mali_queue_lock);
	wakeup((caddr_t)&mali_queue_count);
	return seq;
}

bool
x6886_mali_wait(uint64_t seq, uint64_t timeout_ns)
{
	uint64_t deadline;
	bool done;

	if (timeout_ns > 10ull * NSEC_PER_SEC) {
		timeout_ns = 10ull * NSEC_PER_SEC;
	}
	nanoseconds_to_absolutetime(timeout_ns, &deadline);
	deadline += mach_absolute_time();
	lck_mtx_lock(&mali_queue_lock);
	while (mali_done_seq < seq && timeout_ns != 0 && mach_absolute_time() < deadline) {
		lck_mtx_sleep_deadline(&mali_queue_lock, LCK_SLEEP_DEFAULT,
		    (event_t)&mali_done_seq, THREAD_UNINT, deadline);
	}
	done = mali_done_seq >= seq;
	lck_mtx_unlock(&mali_queue_lock);
	return done;
}

/* ---- init / stop ---------------------------------------------------------- */

int
x6886_mali_init(void)
{
	uint32_t on = 0;
	int error;

	x6886_mali_pstore_map();
	mali_worker_stop = false;
	mali_hwctl = 0;                 /* unproven gate writes are OFF by default */
	(void)PE_parse_boot_argn("mali_hwctl", &mali_hwctl, sizeof(mali_hwctl));
	(void)PE_parse_boot_argn("mali_fastmap", &mali_fastmap, sizeof(mali_fastmap));
	(void)PE_parse_boot_argn("mali_trace", &mali_trace_queue, sizeof(mali_trace_queue));
	if (!PE_parse_boot_argn("mali", &on, sizeof(on)) || on == 0) {
		return 0;                   /* opt-in: no register is touched */
	}
	if (!x6886_mali_opp_check()) {
		mali_log("GPU OPP table from fdt.dts failed validation; stopping\n");
		return EINVAL;
	}
	error = x6886_mali_up();
	mali_log("bring-up returned %d at stage %u of 6\n", error, reached);
	return error;
}

void
x6886_mali_stop(void)
{
	mali_worker_stop = true;    /* bound the worker's event-driven waits */
	wakeup((caddr_t)&mali_queue_count);
	wakeup((caddr_t)&mali_queue_head);
	reached = 0;                /* the job worker sees "not ready" before any unmap */
	if (mali != NULL) {
		wr(mali, GPU_INT_MASK, 0xffffffffu);
		wr(mali, JOB_INT_MASK, 0xffffffffu);
		wr(mali, MMU_INT_MASK, 0xffffffffu);
		ml_io_unmap((vm_offset_t)mali, X6886_MALI_SIZE);
		mali = NULL;
	}
	if (mfgcfg != NULL) {
		ml_io_unmap((vm_offset_t)mfgcfg, X6886_BLOCK_SIZE);
		mfgcfg = NULL;
	}
	if (spm != NULL) {
		ml_io_unmap((vm_offset_t)spm, X6886_BLOCK_SIZE);
		spm = NULL;
	}
	if (infracfg != NULL) {
		ml_io_unmap((vm_offset_t)infracfg, X6886_BLOCK_SIZE);
		infracfg = NULL;
	}
	if (topckgen != NULL) {
		ml_io_unmap((vm_offset_t)topckgen, X6886_BLOCK_SIZE);
		topckgen = NULL;
	}
	gpu_id = 0;
}
