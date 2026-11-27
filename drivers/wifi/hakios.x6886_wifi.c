/*
 * X6886 (MT6789) CONNSYS Wi-Fi: real MMIO / WPDMA / firmware download, no idle stubs.
 *
 * The logic here is the MediaTek gen4m CONNSYS bring-up, adapted from
 *   refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_wifi.c
 *   (steps 12 and 13: driver own, WPDMA rings, INIT commands, WIFI_RAM_CODE download)
 *   refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_connsys.c
 *   (SPM power survey and the CONNSYS register windows)
 * Nothing is invented: every physical address is from the device's own fdt.dts / iomem.txt,
 * every firmware name from /vendor/firmware of the stock dump, and every register offset
 * from the two reference drivers named above. PORT-WIFI.md carries the line-by-line proof.
 *
 * The boot-args are connsys_step=N (1 survey, 2 require powered CONNSYS, 3 take the radio and
 * set up its rings, 4 download WIFI_RAM_CODE and wait for the firmware), and
 * connsys_chip=1d|1e (which of the two stock WIFI_RAM_CODE files is used; 1d is the default).
 * Steps stop at the first failure; no write follows a failed read. Nothing is started without
 * a boot-arg, as in the reference driver.
 */

#include <kern/clock.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t size);
extern void delay(int usec);
extern const uint8_t *hakios_image_file(const char *dir, const char *name, uint64_t *size, int *disk_out);

/* ---- ramoops pstore mirror (boot-audit GATE 4) -------------------------
 * Every diagnostic is written to the normal log AND to the persistent_ram_buffer
 * at 0x480af000: +0 sig 0x43474244, +4 start, +8 size, +12 data.
 * Mapping is done once at init; writes are bounded and never panic.
 */
#define X6886_PSTORE_PHYS       0x480af000ULL
#define X6886_PSTORE_SIZE       0x1000u
#define X6886_PSTORE_SIG        0x43474244u

static vm_offset_t x6886_pstore_va;
static LCK_GRP_DECLARE(x6886_pstore_grp, "x6886-pstore");
static LCK_SPIN_DECLARE(x6886_pstore_lock, &x6886_pstore_grp);

static void
x6886_pstore_map(void)
{
	if (x6886_pstore_va == 0) {
		x6886_pstore_va = ml_io_map(X6886_PSTORE_PHYS, X6886_PSTORE_SIZE);
	}
}

static void
x6886_pstore_write(const char *message, unsigned length)
{
	volatile uint8_t *base;
	uint32_t sig, start, size;
	unsigned n;

	if (x6886_pstore_va == 0 || message == NULL || length == 0) {
		return;
	}
	lck_spin_lock(&x6886_pstore_lock);
	base = (volatile uint8_t *)x6886_pstore_va;
	sig = *(volatile uint32_t *)(base + 0);
	start = *(volatile uint32_t *)(base + 4);
	size = *(volatile uint32_t *)(base + 8);
	if (sig == X6886_PSTORE_SIG && size >= 16 && size <= X6886_PSTORE_SIZE - 12) {
		if (start >= size) {
			start = 0;
		}
		n = length < size - 2 ? length : size - 2;
		for (unsigned i = 0; i < n; i++) {
			base[12 + ((start + i) % size)] = (uint8_t)message[i];
		}
		base[12 + ((start + n) % size)] = '\n';
		*(volatile uint32_t *)(base + 4) = (start + n + 1) % size;
	}
	lck_spin_unlock(&x6886_pstore_lock);
}

#define X6886_LOG(fmt, ...) do {                                              \
	char x6886_log_buf[256];                                              \
	int x6886_log_len = snprintf(x6886_log_buf, sizeof(x6886_log_buf),    \
	    "[X6886-wifi] " fmt, ##__VA_ARGS__);                              \
	printf("%s", x6886_log_buf);                                          \
	if (x6886_log_len > 0) {                                              \
		x6886_pstore_write(x6886_log_buf, (unsigned)x6886_log_len);       \
	}                                                                     \
} while (0)


#ifndef VM_WIMG_IO
#define VM_WIMG_IO              0x7
#endif
#ifndef VM_WIMG_WCOMB
#define VM_WIMG_WCOMB           0x6
#endif

/* ---- X6886 physical blocks, all from the device dump ------------------------------------------------ */

/* fdt.dts:6955 consys@18002000, compatible mediatek,mt6789-consys at :6956; reg words at :6960 */
#define X6886_MCU_PHYS          0x18002000u     /* reg index 0, size 0x1000 */
#define X6886_PWRAP_PHYS        0x10007000u     /* reg index 1, size 0x100  (PWRAP, MT6789 has no MT6358 VCN map) */
#define X6886_INFRACFG_PHYS     0x10001000u     /* reg index 2, size 0x1000 */
#define X6886_SPM_PHYS          0x10006000u     /* reg index 3, size 0x1000 */
#define X6886_MCU_MISC_PHYS     0x180b1000u     /* reg index 6, size 0x1000 */

/* fdt.dts:7571 wifi@18000000, reg <0 0x18000000 0 0x100000> at :7574; iomem.txt:141 AXI-BUS 18000000-180fffff */
#define X6886_WF_CSR_PHYS       0x18000000u
#define X6886_WF_CSR_SIZE       0x100000u

/* fdt.dts:6963-6964 consys emi-addr 0x7f000000, emi-size 0x500000; fdt.dts:513-516 + iomem.txt:168-169 WIFI-EMI */
#define X6886_CONNSYS_EMI_PHYS  0x7f000000u
#define X6886_CONNSYS_EMI_SIZE  0x500000u

/* fdt.dts:506-511 mblock-37-shared-dma-pool_wifi-reserve-memory_dma, reg 0xbe000000 size 0x600000, no-map;
 * iomem.txt:184 be000000-be5fffff reserved */
#define X6886_WIFI_DMA_PHYS     0xbe000000u
#define X6886_WIFI_DMA_SIZE     0x600000u

/* fdt.dts:4477 btif@1100c000 reg at :4479; reg index 1 and 2 are the AP DMA channels */
#define X6886_BTIF_PHYS         0x1100c000u
#define X6886_APDMA_PHYS        0x10217b00u

/* Interrupt routing, as the device reports it (GIC number = DT SPI + 32):
 * fdt.dts:7575 wifi@18000000 interrupts <0 0x199 4 0> -> GICv3 441, line 356 "wlan0" (interrupts.txt:175)
 * fdt.dts:6961 consys interrupts 0x19a -> GIC 442 "BTIF_WAKEUP_IRQ" (interrupts.txt:174)
 *                                     0x19b -> GIC 443 "CONN_LOG_IRQ" (interrupts.txt:173)
 *                                     0x6f  -> GIC 143, not named in interrupts.txt (gap in PORT-WIFI.md)
 * This driver does not claim a GIC line: gic_v3_irq.c in this tree installs only its own handler.
 * The Wi-Fi side is programmed for events and polled, exactly as the reference driver does. */
#define X6886_WLAN0_SPI         0x199u
#define X6886_CONN_BTIF_SPI     0x19au
#define X6886_CONN_LOG_SPI      0x19bu
#define X6886_CONN_MCU_SPI      0x06fu

/* SPM and MCU offsets, from mtk_connsys.c:16-26 and :62-66 */
#define SPM_TOP1_PWR_CTRL       0x32cu
#define SPM_PWR_STATUS          0x180u
#define SPM_PWR_STATUS_2ND      0x184u
#define PWR_STATUS_CONN         (1u << 1)
#define PWR_STATUS_CONN_TOP2    (1u << 30)
#define CONSYS_IP_VER           0x10u
#define CONSYS_CONF_ID          0x1cu
#define CONSYS_HW_ID            0x00u
#define CONSYS_FW_ID            0x04u
#define CONNSYS_IP_VER_ID       0x10020501u     /* CONNSYS__IP_VER_ID read back from this chip; compared only, never required (mtk_wifi.c:88) */

/* WPDMA / CONN_HIF, from mtk_wifi.c:64-105 */
#define WPDMA_APSRC_ACK         0x4160u
#define MCU2HOST_SW_INT_ENA     0x41f4u
#define WPDMA_INT_STA           0x4200u
#define WPDMA_INT_MSK           0x4204u
#define WPDMA_GLO_CFG           0x4208u
#define WPDMA_RST_PTR           0x420cu
#define WPDMA_PAUSE_TX_Q        0x4224u
#define WPDMA_TX_RING           0x4300u
#define WPDMA_RX_RING           0x4400u
#define WPDMA_TX_RING_EXT       0x4500u
#define WPDMA_RX_RING_EXT       0x4580u
#define CONN_HIF_ON_LPCTL       0x7000u
#define CONN_HIF_ON_IRQ_ENA     0x7008u

#define GLO_TX_DMA_EN           (1u << 0)
#define GLO_TX_DMA_BUSY         (1u << 1)
#define GLO_RX_DMA_EN           (1u << 2)
#define GLO_RX_DMA_BUSY         (1u << 3)
#define GLO_BT_SIZE_3           (3u << 4)
#define GLO_TX_WB_DDONE         (1u << 6)
#define GLO_MULTI_DMA_EN_2      (2u << 10)
#define GLO_FIFO_LITTLE_ENDIAN  (1u << 12)
#define GLO_ADDR_EXT_EN         (1u << 26)
#define GLO_CLK_GATE_DIS        (1u << 30)
#define GLO_BYPASS_DMASHDL_TX3  (1u << 9)       /* asicEnableFWDownload */

/* the chip's own addresses, translated to the 1 MB window by x6886_bus() */
#define TOP_HVR                 0x80021000u
#define TOP_FVR                 0x80021004u
#define SW_SYNC0                0x81021140u
#define SW_READY_BITS           (0x3u << 1)

/* INIT download commands and the tailer's features (mtk_wifi.c:107-122, :112) */
#define INIT_CMD_ID_DOWNLOAD_CONFIG     1
#define INIT_CMD_ID_WIFI_START          2
#define INIT_CMD_ID_ACCESS_REG          3
#define INIT_EVENT_ID_CMD_RESULT        1
#define INIT_EVENT_ID_ACCESS_REG        2
#define INIT_RXD_SIZE                   16
#define INIT_EVENT_SIZE                 8
#define FW_CHUNK                        2048
#define FW_FEATURE_SET_ENCRY            (1u << 0)
#define FW_FEATURE_SET_KEY_MASK         (3u << 1)
#define FW_FEATURE_ENCRY_MODE           (1u << 4)
#define FW_FEATURE_NOT_DOWNLOAD         (1u << 6)
#define FW_FEATURE_DL_TO_EMI            (1u << 7)
#define DL_VALID_RAM_ENTRY              (1u << 5)
#define DL_ENCRYPTION_MODE              (1u << 0)
#define DL_RESET_OPTION                 (1u << 3)
#define DL_ENCRY_MODE_SEL               (1u << 6)
#define DL_ACK_OPTION                   (1u << 31)

/* the rings: 0x200 of descriptors each, 32 slots of 4 KB; 6 rings fit in 0x28000 of the 6 MB */
#define WF_DESC                 0x04000u
#define WF_BUFS                 0x10000u
#define WF_SLOTS                32u
#define WF_SLOT_SIZE            0x1000u
#define WF_RX_BUF_SIZE          3840u

#define X6886_FW_DIR            "fw/connsys/x6886"
#define X6886_FW_1D             "WIFI_RAM_CODE_soc2_2_1d_1.bin"
#define X6886_FW_1E             "WIFI_RAM_CODE_soc2_2_1e_1.bin"

static vm_offset_t wf_csr, wf_dma;
static uint8_t wf_seq;
static bool wf_tx_ok;
static const char *wf_firmware = X6886_FW_1D;

/* the TX rings this driver uses, gen4m's numbering and the hardware's (mtk_wifi.c:127-132) */
enum { WF_TX_DATA = 0, WF_TX_CMD = 1, WF_TX_FWDL = 2, WF_TX_COUNT };
static const uint32_t wf_tx_hw[WF_TX_COUNT] = { 0, 15, 3 };
static const uint32_t wf_tx_sw[WF_TX_COUNT] = { 0, 2, 3 };
static uint32_t wf_tx_cidx[WF_TX_COUNT];
enum { WF_RX_DATA = 0, WF_RX_EVENT = 1, WF_RX_COUNT };
static uint32_t wf_rx_cidx[WF_RX_COUNT];

static uint32_t
wf_ms(void)
{
	uint64_t ns = 0;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return (uint32_t)(ns / NSEC_PER_MSEC);
}

/* CONNSYS's address -> the offset in the 1 MB window (connac_bus2chip_cr_mapping, mtk_wifi.c:142-176) */
static uint32_t
wf_bus(uint32_t chip)
{
	static const uint32_t map[][3] = {
		{ 0x80000000, 0x00002000, 0x00001000 }, { 0x50000000, 0x00004000, 0x00004000 },
		{ 0x50002000, 0x00005000, 0x00001000 }, { 0x5000A000, 0x00006000, 0x00001000 },
		{ 0x000E0000, 0x00007000, 0x00001000 }, { 0x82060000, 0x00008000, 0x00004000 },
		{ 0x82068000, 0x0000C000, 0x00002000 }, { 0x8206C000, 0x0000E000, 0x00002000 },
		{ 0x82070000, 0x00010000, 0x00010000 }, { 0x820F0000, 0x00020000, 0x00010000 },
		{ 0x820E0000, 0x00030000, 0x00010000 }, { 0x81000000, 0x00050000, 0x00010000 },
		{ 0x80070000, 0x00060000, 0x00010000 }, { 0x40000000, 0x00070000, 0x00010000 },
		{ 0x00300000, 0x00080000, 0x00010000 }, { 0x80010000, 0x000A1000, 0x00001000 },
		{ 0x80030000, 0x000A2000, 0x00001000 }, { 0x81030000, 0x000A3000, 0x00001000 },
		{ 0x80050000, 0x000A4000, 0x00001000 }, { 0x81040000, 0x000A5000, 0x00001000 },
		{ 0x81050000, 0x000A6000, 0x00001000 }, { 0x81060000, 0x000A7000, 0x00001000 },
		{ 0x81080000, 0x000A8000, 0x00001000 }, { 0x81090000, 0x000A9000, 0x00001000 },
		{ 0x810A0000, 0x000AA000, 0x00001000 }, { 0x810B0000, 0x000AB000, 0x00001000 },
		{ 0x810C0000, 0x000AC000, 0x00001000 }, { 0x810D0000, 0x000AD000, 0x00001000 },
		{ 0x820D0000, 0x000AE000, 0x00001000 }, { 0x60000000, 0x000AF000, 0x00001000 },
		{ 0x80020000, 0x000B0000, 0x00010000 }, { 0x81020000, 0x000C0000, 0x00010000 },
	};

	if (chip < 0x40000) {
		return chip;
	}
	for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (chip >= map[i][0] && chip - map[i][0] < map[i][2]) {
			return map[i][1] + (chip - map[i][0]);
		}
	}
	return 0;
}

static uint32_t
wf_rd(uint32_t chip)
{
	uint32_t bus = wf_bus(chip);

	return bus != 0 ? *(volatile uint32_t *)(wf_csr + bus) : 0xdeadbeef;
}

static void
wf_wr(uint32_t chip, uint32_t value)
{
	uint32_t bus = wf_bus(chip);

	if (bus != 0) {
		*(volatile uint32_t *)(wf_csr + bus) = value;
	}
}

static volatile uint32_t *
wf_desc(bool tx, unsigned ring, unsigned slot)
{
	return (volatile uint32_t *)(wf_dma + WF_DESC + ((tx ? 0 : WF_TX_COUNT) + ring) * 0x200u + slot * 16u);
}

static uint32_t
wf_buf_offset(bool tx, unsigned ring, unsigned slot)
{
	return WF_BUFS + ((tx ? 0 : WF_TX_COUNT) + ring) * (WF_SLOTS * WF_SLOT_SIZE) + slot * WF_SLOT_SIZE;
}

/* halWpdmaInitRing: DMA stopped, indices reset, ring bases/counts written, DMA on (mtk_wifi.c:212-270) */
static void
wf_rings_init(void)
{
	uint32_t glo;

	wf_wr(WPDMA_INT_MSK, 0);
	wf_wr(WPDMA_GLO_CFG, wf_rd(WPDMA_GLO_CFG) & ~(GLO_TX_DMA_EN | GLO_RX_DMA_EN));
	wf_wr(WPDMA_PAUSE_TX_Q, 0);
	wf_wr(MCU2HOST_SW_INT_ENA, 0x3cu);
	wf_wr(WPDMA_APSRC_ACK, wf_rd(WPDMA_APSRC_ACK) | (1u << 4));
	for (unsigned i = 0; i < 100 && (wf_rd(WPDMA_GLO_CFG) & (GLO_TX_DMA_BUSY | GLO_RX_DMA_BUSY)); i++) {
		delay(1000);
	}
	wf_wr(WPDMA_RST_PTR, 0xffffffffu);

	for (unsigned r = 0; r < WF_TX_COUNT; r++) {
		uint32_t at = WPDMA_TX_RING + wf_tx_hw[r] * 0x10u;

		for (unsigned s = 0; s < WF_SLOTS; s++) {
			volatile uint32_t *d = wf_desc(true, r, s);

			d[0] = 0;
			d[1] = 1u << 31;                /* DMADONE: the slot is free */
			d[2] = 0;
			d[3] = 0;
		}
		wf_tx_cidx[r] = 0;
		wf_wr(at + 0x0, X6886_WIFI_DMA_PHYS + WF_DESC + r * 0x200u);
		wf_wr(at + 0x8, 0);
		wf_wr(at + 0x4, WF_SLOTS);
		wf_wr(WPDMA_TX_RING_EXT + wf_tx_sw[r] * 4u, 0);
	}
	for (unsigned r = 0; r < WF_RX_COUNT; r++) {
		uint32_t at = WPDMA_RX_RING + r * 0x10u;

		for (unsigned s = 0; s < WF_SLOTS; s++) {
			volatile uint32_t *d = wf_desc(false, r, s);

			d[0] = X6886_WIFI_DMA_PHYS + wf_buf_offset(false, r, s);
			d[1] = WF_RX_BUF_SIZE << 16;
			d[2] = 0;
			d[3] = 0;
		}
		wf_rx_cidx[r] = WF_SLOTS - 1;
		wf_wr(at + 0x0, X6886_WIFI_DMA_PHYS + WF_DESC + (WF_TX_COUNT + r) * 0x200u);
		wf_wr(at + 0x8, WF_SLOTS - 1);
		wf_wr(at + 0x4, WF_SLOTS);
		wf_wr(WPDMA_RX_RING_EXT + r * 4u, 0);
	}
	__asm__ volatile ("dsb sy" ::: "memory");

	/* rx done 0 and 1, tx done of rings 0, 3 and 15, and the MCU's software interrupt */
	wf_wr(WPDMA_INT_MSK, (1u << 0) | (1u << 1) | (1u << (4 + 0)) | (1u << (4 + 3)) | (1u << (4 + 15)) | (1u << 29));
	glo = wf_rd(WPDMA_GLO_CFG) & ~((3u << 4) | (3u << 10));
	glo |= GLO_TX_DMA_EN | GLO_RX_DMA_EN | GLO_BT_SIZE_3 | GLO_ADDR_EXT_EN | GLO_TX_WB_DDONE | GLO_MULTI_DMA_EN_2 |
	    GLO_FIFO_LITTLE_ENDIAN | GLO_CLK_GATE_DIS;
	wf_wr(WPDMA_GLO_CFG, glo);
	wf_wr(WPDMA_PAUSE_TX_Q, 0);
}

/* one frame into a TX ring, waiting for the DMA to take it (mtk_wifi.c:272-299) */
static bool
wf_tx(unsigned ring, const void *frame, uint32_t length)
{
	const uint32_t hw = WPDMA_TX_RING + wf_tx_hw[ring] * 0x10u;
	uint32_t slot = wf_tx_cidx[ring], next = (slot + 1) % WF_SLOTS;
	volatile uint32_t *d = wf_desc(true, ring, slot);

	if (length > WF_SLOT_SIZE) {
		return false;
	}
	memcpy((void *)(wf_dma + wf_buf_offset(true, ring, slot)), frame, length);
	d[0] = X6886_WIFI_DMA_PHYS + wf_buf_offset(true, ring, slot);
	d[2] = 0;
	d[3] = 0;
	d[1] = (length << 16) | (1u << 30);     /* SDLen0, LastSec0; DMADONE clear: the DMA's */
	__asm__ volatile ("dsb sy" ::: "memory");
	wf_tx_cidx[ring] = next;
	wf_wr(hw + 0x8, next);
	for (unsigned i = 0; i < 2000; i++) {
		if (wf_rd(hw + 0xc) == next) {
			return true;
		}
		delay(50);
	}
	return false;
}

/* the next frame the DMA put into an RX ring, or -1 after wait_ms (mtk_wifi.c:300-336) */
static int
wf_rx(unsigned ring, uint8_t *frame, uint32_t room, unsigned wait_ms)
{
	const uint32_t hw = WPDMA_RX_RING + ring * 0x10u;
	uint32_t slot = (wf_rx_cidx[ring] + 1) % WF_SLOTS;
	volatile uint32_t *d = wf_desc(false, ring, slot);
	uint32_t length;

	for (unsigned t = 0; !(d[1] & (1u << 31)); t++) {
		if (t >= wait_ms * 10) {
			return -1;
		}
		delay(100);
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	length = (d[1] >> 16) & 0x3fff;
	if (length > room) {
		length = room;
	}
	if (length > WF_SLOT_SIZE) {
		length = WF_SLOT_SIZE;
	}
	memcpy(frame, (const void *)(wf_dma + wf_buf_offset(false, ring, slot)), length);
	d[0] = X6886_WIFI_DMA_PHYS + wf_buf_offset(false, ring, slot);
	d[1] = WF_RX_BUF_SIZE << 16;
	__asm__ volatile ("dsb sy" ::: "memory");
	wf_rx_cidx[ring] = slot;
	wf_wr(hw + 0x8, slot);
	return (int)length;
}

/* a 64-byte init frame (mtk_wifi.c:337-359) */
static uint8_t
wf_init_cmd(uint8_t cid, const void *body, uint32_t body_length)
{
	static uint8_t frame[64 + FW_CHUNK];
	const uint32_t length = 64 + body_length;

	bzero(frame, 64);
	frame[0] = (uint8_t)length;
	frame[1] = (uint8_t)(length >> 8);
	frame[2] = 0x00;
	frame[3] = cid != 0 ? 0x80 : 0xf8;     /* u2PQ_ID */
	frame[5] = 0xa0;                        /* ucHeaderFormat */
	frame[7] = cid != 0 ? 2 : 3;            /* ucPktFt */
	frame[32] = (uint8_t)(length - 32);
	frame[33] = (uint8_t)((length - 32) >> 8);
	frame[36] = cid;
	frame[37] = 0xa0;                       /* ucPktTypeID */
	frame[39] = ++wf_seq;
	memcpy(frame + 64, body, body_length);
	wf_tx_ok = wf_tx(WF_TX_FWDL, frame, length);
	return wf_seq;
}

/* the ROM's answer on RX ring 1 (mtk_wifi.c:361-387) */
static int
wf_init_event(uint8_t seq, uint8_t *eid, uint8_t *body, uint32_t room, unsigned wait_ms)
{
	uint8_t frame[256];
	int got;
	unsigned tries = 0;
	uint64_t began_ns = 0, now_ns = 0;
	uint64_t budget_ns = ((uint64_t)wait_ms + 10u) * 1000000ULL * 8u;

	absolutetime_to_nanoseconds(mach_absolute_time(), &began_ns);
	while ((got = wf_rx(WF_RX_EVENT, frame, sizeof(frame), wait_ms)) >= 0) {
		absolutetime_to_nanoseconds(mach_absolute_time(), &now_ns);
		if (++tries > 64 || now_ns - began_ns > budget_ns) {
			X6886_LOG("RX event cap (%u frames, %llu ns); wrong-sequence stream stopped\n",
			    tries, (unsigned long long)(now_ns - began_ns));
			return -1;
		}
		if (got < INIT_RXD_SIZE + INIT_EVENT_SIZE) {
			continue;
		}
		if (frame[INIT_RXD_SIZE + 5] != seq) {
			X6886_LOG("an event %#x of sequence %u, not %u\n", frame[INIT_RXD_SIZE + 4],
			    frame[INIT_RXD_SIZE + 5], seq);
			continue;
		}
		*eid = frame[INIT_RXD_SIZE + 4];
		got -= INIT_RXD_SIZE + INIT_EVENT_SIZE;
		if ((uint32_t)got > room) {
			got = (int)room;
		}
		memcpy(body, frame + INIT_RXD_SIZE + INIT_EVENT_SIZE, (uint32_t)got);
		return got;
	}
	return -1;
}

static int
wf_init_cmd_result(uint8_t cid, const void *body, uint32_t body_length, unsigned wait_ms)
{
	uint8_t seq = wf_init_cmd(cid, body, body_length), eid = 0, result[4] = { 0xff };

	if (!wf_tx_ok || wf_init_event(seq, &eid, result, sizeof(result), wait_ms) < 1 || eid != INIT_EVENT_ID_CMD_RESULT) {
		return -1;
	}
	return result[0];
}

static bool
wf_access_reg(uint32_t chip, uint32_t *value)
{
	uint8_t body[12] = { 0 }, answer[8], eid = 0, seq;

	memcpy(body + 4, &chip, 4);
	seq = wf_init_cmd(INIT_CMD_ID_ACCESS_REG, body, sizeof(body));
	if (!wf_tx_ok || wf_init_event(seq, &eid, answer, sizeof(answer), 1000) < 8 || eid != INIT_EVENT_ID_ACCESS_REG) {
		return false;
	}
	memcpy(value, answer + 4, 4);
	return true;
}

/* halSetDriverOwn: ask for ownership every 200 ms until the firmware's bit drops (mtk_wifi.c:414-431) */
static bool
wf_driver_own(void)
{
	const uint32_t began = wf_ms();

	for (uint32_t asked = 0; wf_ms() - began < 2000;) {
		if ((wf_rd(CONN_HIF_ON_LPCTL) & 1) == 0 && asked != 0) {
			return true;
		}
		if (asked == 0 || wf_ms() - asked >= 200) {
			wf_wr(CONN_HIF_ON_LPCTL, 1u << 1);
			asked = wf_ms() | 1;
		}
		delay(100);
	}
	return false;
}

/* wlanDownloadEMISection: the address's low 24 bits are the offset into CONNSYS's 5 MB (mtk_wifi.c:465-483) */
static bool
wf_emi_copy(uint32_t address, const uint8_t *data, uint32_t length)
{
	const uint32_t offset = address & 0xffffffu, page = offset & ~0x3fffu;
	const uint32_t size = (offset - page + length + 0x3fffu) & ~0x3fffu;
	vm_offset_t window;

	if (offset + length > X6886_CONNSYS_EMI_SIZE || size == 0) {
		return false;
	}
	if ((window = ml_io_map_unmappable(X6886_CONNSYS_EMI_PHYS + page, size, VM_WIMG_WCOMB)) == 0) {
		return false;
	}
	memcpy((void *)(window + (offset - page)), data, length);
	__asm__ volatile ("dsb sy" ::: "memory");
	ml_io_unmap(window, size);
	return true;
}

/* step 4: WIFI_RAM_CODE, region by region, then WIFI_START and the ready bits (mtk_wifi.c:484-592) */
static bool
wf_download(void)
{
	const uint32_t began = wf_ms();
	uint64_t file_size = 0;
	uint32_t size, entry = 0, at = 0, sync = 0;
	const uint8_t *file;
	unsigned regions;
	int status = -1;
	bool ok = true;

	file = hakios_image_file(X6886_FW_DIR, wf_firmware, &file_size, NULL);
	if (file == NULL || file_size < 36 + 40 || file_size > 0xffffffffu) {
		X6886_LOG("no %s/%s in the small image\n", X6886_FW_DIR, wf_firmware);
		return false;
	}
	size = (uint32_t)file_size;
	/* wlanGetConnacTailerInfo: 36 bytes at the end, the regions' 40 each before it.
	 * Both X6886 blobs were checked offline: 5 regions, feature 0x27/0x7/0x7/0x80/0x80. */
	regions = file[size - 36 + 2];
	if (regions == 0 || regions > 8 || size < 36 + regions * 40) {
		X6886_LOG("the tailer of %s says %u regions\n", wf_firmware, regions);
		return false;
	}
	X6886_LOG("%s: %u bytes, %u regions, version %.10s of %.14s\n", wf_firmware, size, regions,
	    file + size - 36 + 7, file + size - 36 + 17);
	for (unsigned i = 0; i < regions && ok; i++) {
		const uint8_t *r = file + size - 36 - (regions - i) * 40;
		uint32_t address, length, mode = 0;
		uint8_t feature = r[24];

		memcpy(&address, r + 16, 4);
		memcpy(&length, r + 20, 4);
		if (feature & DL_VALID_RAM_ENTRY) {
			entry = address;
		}
		if ((feature & FW_FEATURE_NOT_DOWNLOAD) || length == 0) {
			at += length;
			continue;
		}
		if (at + length > size - 36 - regions * 40) {
			X6886_LOG("region %u runs past %s\n", i, wf_firmware);
			ok = false;
			break;
		}
		if (feature & FW_FEATURE_DL_TO_EMI) {
			ok = wf_emi_copy(address, file + at, length);
			X6886_LOG("region %u, %u bytes to %#x: copied into CONNSYS's memory%s\n", i, length, address,
			    ok ? "" : " -- NOT");
		} else {
			uint32_t config[3];
			unsigned pieces = 0;

			if (feature & FW_FEATURE_SET_ENCRY) {
				mode |= DL_RESET_OPTION | (feature & FW_FEATURE_SET_KEY_MASK) | DL_ENCRYPTION_MODE;
				if (feature & FW_FEATURE_ENCRY_MODE) {
					mode |= DL_ENCRY_MODE_SEL;
				}
			}
			mode |= DL_ACK_OPTION;
			config[0] = address;
			config[1] = length;
			config[2] = mode;
			status = wf_init_cmd_result(INIT_CMD_ID_DOWNLOAD_CONFIG, config, sizeof(config), 1000);
			for (uint32_t done = 0; status == 0 && done < length; done += FW_CHUNK, pieces++) {
				(void)wf_init_cmd(0, file + at + done, length - done < FW_CHUNK ? length - done : FW_CHUNK);
				if (!wf_tx_ok) {
					status = -2;
				}
			}
			ok = status == 0;
			X6886_LOG("region %u, %u bytes to %#x (feature %#x, mode %#x): %s, %u pieces\n", i, length, address,
			    feature, mode, status == 0 ? "configured" : status == -2 ? "THE DMA DID NOT TAKE A PIECE" :
			    status < 0 ? "CONFIGURATION NOT ANSWERED" : "CONFIGURATION REFUSED", pieces);
		}
		at += length;
	}
	if (!ok) {
		return false;
	}
	{
		uint32_t start[2] = { entry != 0 ? 1u : 0u, entry };
		const uint32_t asked = wf_ms();

		status = wf_init_cmd_result(INIT_CMD_ID_WIFI_START, start, sizeof(start), 3000);
		for (unsigned i = 0; status == 0 && i < 500 && ((sync = wf_rd(SW_SYNC0)) & SW_READY_BITS) != SW_READY_BITS; i++) {
			delay(10000);
		}
		X6886_LOG("WIFI_START (entry %#x) %s in %u ms; sw_sync0 %#x: %s; %u ms in all\n", entry,
		    status == 0 ? "answered" : status < 0 ? "NOT ANSWERED" : "REFUSED", wf_ms() - asked, sync,
		    (sync & SW_READY_BITS) == SW_READY_BITS ? "THE WI-FI FIRMWARE IS READY" : "not ready", wf_ms() - began);
	}
	return status == 0 && (sync & SW_READY_BITS) == SW_READY_BITS;
}

/* ---- the steps -------------------------------------------------------------------------------------- */

/* step 1: SPM and MCU, read only (mtk_connsys.c:2283-2340) */
static bool
wf_survey(void)
{
	vm_offset_t spm = ml_io_map(X6886_SPM_PHYS, 0x1000);
	vm_offset_t mcu = 0, misc = 0;
	uint32_t ctrl, status, status2;
	bool on;

	if (spm == 0) {
		X6886_LOG("no window for SPM %#x\n", X6886_SPM_PHYS);
		return false;
	}
	ctrl = *(volatile uint32_t *)(spm + SPM_TOP1_PWR_CTRL);
	status = *(volatile uint32_t *)(spm + SPM_PWR_STATUS);
	status2 = *(volatile uint32_t *)(spm + SPM_PWR_STATUS_2ND);
	on = (status & status2 & PWR_STATUS_CONN) && (status & status2 & PWR_STATUS_CONN_TOP2);
	X6886_LOG("SPM TOP1_PWR_CTRL %#x (reset %s, iso %s, on %u/%u, clock %u), PWR_STATUS %#x/%#x: CONNSYS %s\n",
	    ctrl, (ctrl & 1) ? "released" : "held", (ctrl & 2) ? "on" : "off", (ctrl >> 2) & 1, (ctrl >> 3) & 1,
	    (ctrl >> 4) & 1, status, status2, on ? "ON" : "off");
	X6886_LOG("CONNSYS reg %#x, wlan CSR %#x, CONNSYS EMI %#x/%#x, Wi-Fi DMA %#x/%#x; SPI wlan %#x, "
	    "BTIF %#x, log %#x, MCU %#x\n", X6886_MCU_PHYS, X6886_WF_CSR_PHYS, X6886_CONNSYS_EMI_PHYS, X6886_CONNSYS_EMI_SIZE,
	    X6886_WIFI_DMA_PHYS, X6886_WIFI_DMA_SIZE, X6886_WLAN0_SPI, X6886_CONN_BTIF_SPI, X6886_CONN_LOG_SPI,
	    X6886_CONN_MCU_SPI);
	if (on) {
		mcu = ml_io_map(X6886_MCU_PHYS, 0x1000);
		misc = ml_io_map(X6886_MCU_MISC_PHYS, 0x1000);
		if (mcu == 0 || misc == 0) {
			X6886_LOG("no window for the CONNSYS MCU or its misc block\n");
		} else {
			X6886_LOG("IP version %#x (%s), config %#x, hardware %#x, firmware %#x\n",
			    *(volatile uint32_t *)(misc + CONSYS_IP_VER),
			    *(volatile uint32_t *)(misc + CONSYS_IP_VER) == CONNSYS_IP_VER_ID ? "expected value" : "different value",
			    *(volatile uint32_t *)(misc + CONSYS_CONF_ID) & 0xf,
			    *(volatile uint32_t *)(mcu + CONSYS_HW_ID) & 0xffff,
			    *(volatile uint32_t *)(mcu + CONSYS_FW_ID) & 0xffff);
		}
	} else {
		X6886_LOG("CONNSYS is off: its own registers are not read (a read of a powered-off block can hang the bus)\n");
	}
	if (misc != 0) {
		ml_io_unmap(misc, 0x1000);
	}
	if (mcu != 0) {
		ml_io_unmap(mcu, 0x1000);
	}
	ml_io_unmap(spm, 0x1000);
	return on;
}

/* step 3 (the radio part of step 12, mtk_wifi.c:433-462) */
static bool
wf_take_radio(void)
{
	uint32_t lpctl, hvr = 0, fvr = 0;

	wf_csr = ml_io_map_unmappable(X6886_WF_CSR_PHYS, X6886_WF_CSR_SIZE, VM_WIMG_IO);
	wf_dma = ml_io_map_unmappable(X6886_WIFI_DMA_PHYS, X6886_WIFI_DMA_SIZE, VM_WIMG_WCOMB);
	if (wf_csr == 0 || wf_dma == 0) {
		X6886_LOG("no window for the Wi-Fi registers or its DMA memory\n");
		return false;
	}
	lpctl = wf_rd(CONN_HIF_ON_LPCTL);
	if (!wf_driver_own()) {
		X6886_LOG("driver own NOT TAKEN (LPCTL %#x -> %#x)\n", lpctl, wf_rd(CONN_HIF_ON_LPCTL));
		return false;
	}
	X6886_LOG("driver own taken (LPCTL %#x -> %#x), sw_sync0 %#x\n", lpctl, wf_rd(CONN_HIF_ON_LPCTL),
	    wf_rd(SW_SYNC0));
	wf_wr(CONN_HIF_ON_IRQ_ENA, 1);          /* asicCapInit */
	wf_rings_init();
	wf_wr(WPDMA_GLO_CFG, wf_rd(WPDMA_GLO_CFG) | GLO_BYPASS_DMASHDL_TX3);    /* asicEnableFWDownload */
	if (!wf_access_reg(TOP_HVR, &hvr) || !wf_access_reg(TOP_FVR, &fvr)) {
		X6886_LOG("the ROM did not answer TOP_HVR/TOP_FVR (rings at %#x, INT_STA %#x)\n",
		    X6886_WIFI_DMA_PHYS + WF_DESC, wf_rd(WPDMA_INT_STA));
		return false;
	}
	X6886_LOG("rings at %#x, GLO_CFG %#x; TOP_HVR %#x, TOP_FVR %#x\n", X6886_WIFI_DMA_PHYS + WF_DESC,
	    wf_rd(WPDMA_GLO_CFG), hvr, fvr);
	return true;
}

static void
wf_release(void)
{
	if (wf_csr != 0) {
		wf_wr(WPDMA_INT_MSK, 0);
		ml_io_unmap(wf_csr, X6886_WF_CSR_SIZE);
		wf_csr = 0;
	}
	if (wf_dma != 0) {
		ml_io_unmap(wf_dma, X6886_WIFI_DMA_SIZE);
		wf_dma = 0;
	}
}

void
x6886_wifi_init(void)
{
	uint32_t step = 0, done = 0;

	x6886_pstore_map();
	char chip[16] = "";

	if (!PE_parse_boot_argn("connsys_step", &step, sizeof(step)) || step == 0) {
		return;
	}
	if (PE_parse_boot_arg_str("connsys_chip", chip, sizeof(chip))) {
		if (strcmp(chip, "1e") == 0) {
			wf_firmware = X6886_FW_1E;
		} else if (strcmp(chip, "1d") != 0 && chip[0] != 0) {
			X6886_LOG("connsys_chip=%s is not 1d or 1e; nothing is touched\n", chip);
			return;
		}
	}
	X6886_LOG("steps 1 to %u (%s)\n", step > 4 ? 4 : step, wf_firmware);
	if (!wf_survey()) {
		X6886_LOG("step 1: CONNSYS is not on. The MT6366 VCN supply registers are not verified "
		    "for this phone, so nothing is written: no power-on is guessed\n");
		return;
	}
	done = 1;
	if (step >= 3 && !wf_take_radio()) {
		X6886_LOG("step 3 FAILED; the firmware is not downloaded\n");
		return;
	}
	if (step >= 3) {
		done = 3;
	}
	if (step >= 4) {
		if (wf_download()) {
			done = 4;
		} else {
			X6886_LOG("step 4 FAILED\n");
		}
	}
	X6886_LOG("over after step %u%s\n", done, done == 4 ? ": the Wi-Fi firmware is running" : "");
	if (done < 4) {
		wf_release();
	}
}

/* called when a run ends in order, or on the way back to the loader */
void
x6886_wifi_stop(void)
{
	wf_release();
}
