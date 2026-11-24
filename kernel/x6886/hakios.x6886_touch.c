/*
 * X6886 (MT6789) FocalTech FT3683G SPI touch.
 *
 * The controller, GPIO, FIFO and poll framework is kept from
 *   refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_tcm.c
 * (MT6768 / OmniVision TD4160 on SPI2). The TD4160 message language and its picture-fit
 * maths are removed. Only the MediaTek SPI MMIO, the GPIO bit-bang and the report-posting
 * path survive. Every address, pin, regulator, coordinate and limit below comes from the
 * device's own dump; PORT-TOUCH.md carries the file:line proof and the gaps.
 *
 * Boot args: x6886_touch=N (0 off, 1 readiness, 2 + GPIO power/reset and chip presence,
 * 3 + bounded polling), x6886_poll_us=N, x6886_ticks=N, x6886_spi_ticks=N,
 * x6886_bitbang=1, fts_reg=N, fts_frame=N, fts_head=N, fts_point=N, fts_x_off=N,
 * fts_y_off=N, fts_xy_bytes=N, fts_reset_ms=N.
 * Step 3 refuses to start unless the whole FTS frame layout is supplied: the dump does
 * not contain the compiled frame geometry, and no value is invented here.
 */

#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios_input.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
extern void delay(int usec);

/* ---- X6886 blocks: device-dump/fdt.dts and device-dump/iomem.txt -------------------------- */

#define X6886_SPI1_PHYS         0x11010000u     /* fdt.dts:4489, iomem.txt:96 */
#define X6886_SPI_SIZE          0x100u          /* fdt.dts:4489 */
#define X6886_GPIO_PHYS         0x10005000u     /* fdt.dts:2376-2378 */
#define X6886_GPIO_SIZE         0x1000u         /* fdt.dts:2378 */
#define X6886_TOPCKGEN_PHYS     0x10000000u     /* fdt.dts:1582-1584 */
#define X6886_INFRACFG_PHYS     0x10001000u     /* fdt.dts:1589-1591 */
#define X6886_CLOCK_SIZE        0x1000u         /* fdt.dts:1584, 1591 */
#define X6886_RAMOOPS_PHYS      0x480af000u     /* fdt.dts:92 pstore 0x48090000 + 0xe0000 */
#define X6886_RAMOOPS_SIZE      0x1000u

/* ---- Wiring (focaltech, touch@0) --------------------------------------------------------- */

#define FTS_PIN_IRQ             12u             /* fdt.dts:4503 */
#define FTS_PIN_RESET           116u            /* fdt.dts:4504 */
#define FTS_PIN_AVDD            150u            /* fdt.dts:4505 */
#define FTS_PIN_IOVDD           15u             /* fdt.dts:4502 */
#define FTS_SPI_PIN_CS          37u             /* fdt.dts:2833: 0x2501 */
#define FTS_SPI_PIN_CLK         35u             /* fdt.dts:2833: 0x2301 */
#define FTS_SPI_PIN_MO          36u             /* fdt.dts:2833: 0x2401 */
#define FTS_SPI_PIN_MI          34u             /* fdt.dts:2833: 0x2201 */
#define FTS_SPI_FUNC            1u              /* fdt.dts:2833: low byte of each pinmux */

#define FTS_READ_CMD            0x03u           /* touch.md:466-467: "read 03(SPI) command fail" */
#define FTS_MAX_POINTS          10u             /* fdt.dts:4506, Conf_MultipleTest.ini:34 */
#define FTS_X_MAX               0x437fu         /* fdt.dts:4507 */
#define FTS_Y_MAX               0x95ffu         /* fdt.dts:4507 */
#define FTS_PIXEL_X             1080u           /* Conf_MultipleTest.ini:37 */
#define FTS_PIXEL_Y             2400u           /* Conf_MultipleTest.ini:38 */
#define FTS_BUF_BYTES           256u            /* mtk_tcm.c:451; Conf_MultipleTest.ini:50 */

/* ---- Registers retained from the MT6768 driver (mtk_tcm.c) ------------------------------- */

#define GPIO_DIR0_SET           0x004u          /* mtk_tcm.c:63 */
#define GPIO_DIR0_CLR           0x008u          /* mtk_tcm.c:64 */
#define GPIO_DOUT0_SET          0x104u          /* mtk_tcm.c:65 */
#define GPIO_DOUT0_CLR          0x108u          /* mtk_tcm.c:66 */
#define GPIO_DIN0               0x200u          /* mtk_tcm.c:67 */
#define GPIO_MODE4              0x340u          /* mtk_tcm.c:68 MODE3=0x330 + 8-pin stride */

#define SPI_CFG0                0x00u           /* mtk_tcm.c:128 */
#define SPI_CFG1                0x04u           /* mtk_tcm.c:129 */
#define SPI_TX_DATA             0x10u           /* mtk_tcm.c:130 */
#define SPI_RX_DATA             0x14u           /* mtk_tcm.c:131 */
#define SPI_CMD                 0x18u           /* mtk_tcm.c:132 */
#define SPI_STATUS0             0x1cu           /* mtk_tcm.c:133 */
#define SPI_PAD_SEL             0x24u           /* mtk_tcm.c:134 */
#define SPI_CFG2                0x28u           /* mtk_tcm.c:135 */

#define SPI_CMD_ACT             (1u << 0)       /* mtk_tcm.c:137 */
#define SPI_CMD_RST             (1u << 2)       /* mtk_tcm.c:138 */
#define SPI_CMD_CPHA            (1u << 8)       /* mtk_tcm.c:139 */
#define SPI_CMD_CPOL            (1u << 9)       /* mtk_tcm.c:140 */
#define SPI_CMD_TXMSBF          (1u << 12)      /* mtk_tcm.c:141 */
#define SPI_CMD_RXMSBF          (1u << 13)      /* mtk_tcm.c:142 */
#define SPI_STATUS0_DONE        (1u << 0)       /* mtk_tcm.c:143 */
#define SPI_FIFO_BYTES          32u             /* mtk_tcm.c:144 */

#define X6886_SPI_SPIN_MAX      200000u         /* mtk_tcm.c:193 */
#define X6886_IRQ_SPIN_MAX      64u             /* bounded replacement for mtk_tcm.c:470 */

/* Same pstore ring as the other x6886 drivers, so a trace never clobbers their header. */
#define X6886_PSTORE_SIG        0x43474244u
#define X6886_PSTORE_HEAD       12u

static LCK_GRP_DECLARE(x6886_touch_pstore_grp, "x6886-touch-pstore");
static LCK_SPIN_DECLARE(x6886_touch_pstore_lock, &x6886_touch_pstore_grp);

static vm_offset_t spi, gpio, topckgen, infracfg, ramoops;
static uint32_t spi_ticks = 2;                  /* mtk_tcm.c:153 */
static uint32_t poll_us = 8000;                 /* mtk_tcm.c:452 */
static uint32_t max_ticks = 360000u;
static bool bitbang_enabled = false;
static uint32_t stalls, reports, touches, log_seq;
static uint32_t last_x, last_y;
static thread_t touch_thread;

enum {
	STAGE_MAP = 1, STAGE_POWER, STAGE_CLOCK, STAGE_PINS, STAGE_SPI,
	STAGE_RESET, STAGE_PRESENCE, STAGE_LAYOUT, STAGE_THREAD, STAGE_REPORT
};

struct fts_layout {
	uint32_t reg;
	uint32_t frame;
	uint32_t head;
	uint32_t point;
	uint32_t x_off;
	uint32_t y_off;
	uint32_t xy_bytes;
	uint32_t reset_ms;
};

static struct fts_layout layout;

static inline uint32_t
rd(vm_offset_t base, uint32_t at)
{
	return *(volatile uint32_t *)(base + at);
}

static inline void
wr(vm_offset_t base, uint32_t at, uint32_t value)
{
	*(volatile uint32_t *)(base + at) = value;
}

/* ---- ramoops trace: [X6886-touch] lines into the shared pstore ring ----------------------- */

static void
x6886_trace(uint32_t stage, uint32_t status, const uint32_t *regs, uint32_t count)
{
	char line[256];
	volatile uint8_t *base;
	uint32_t sig, start, size;
	unsigned n;
	int len;

	len = snprintf(line, sizeof(line), "[X6886-touch] stage %u status 0x%x seq %u regs",
	    stage, status, ++log_seq);
	if (count > 8) {
		count = 8;
	}
	for (uint32_t i = 0; i < count && len > 0 && len < (int)sizeof(line) - 12; i++) {
		int added = snprintf(line + len, sizeof(line) - (size_t)len, " %x",
		    regs ? regs[i] : 0);

		if (added <= 0) {
			break;
		}
		len += added;
	}
	printf("%s\n", line);
	if (ramoops == 0) {
		return;
	}
	lck_spin_lock(&x6886_touch_pstore_lock);
	base = (volatile uint8_t *)ramoops;
	sig = *(volatile uint32_t *)(base + 0);
	start = *(volatile uint32_t *)(base + 4);
	size = *(volatile uint32_t *)(base + 8);
	if (sig != X6886_PSTORE_SIG || size < 16 || size > X6886_RAMOOPS_SIZE - X6886_PSTORE_HEAD) {
		*(volatile uint32_t *)(base + 0) = X6886_PSTORE_SIG;
		*(volatile uint32_t *)(base + 4) = 0;
		*(volatile uint32_t *)(base + 8) = X6886_RAMOOPS_SIZE - X6886_PSTORE_HEAD;
		start = 0;
		size = X6886_RAMOOPS_SIZE - X6886_PSTORE_HEAD;
	}
	if (start >= size) {
		start = 0;
	}
	n = len > 0 ? (unsigned)len : 0;
	if (n > size - 2) {
		n = size - 2;
	}
	for (unsigned i = 0; i < n; i++) {
		base[X6886_PSTORE_HEAD + ((start + i) % size)] = (uint8_t)line[i];
	}
	base[X6886_PSTORE_HEAD + ((start + n) % size)] = '\n';
	*(volatile uint32_t *)(base + 4) = (start + n + 1) % size;
	lck_spin_unlock(&x6886_touch_pstore_lock);
}

/* ---- the pins ---------------------------------------------------------------------------- */

static inline void
pin_set(unsigned pin, bool high)
{
	wr(gpio, high ? GPIO_DOUT0_SET : GPIO_DOUT0_CLR, 1u << pin);
}

static inline bool
pin_get(unsigned pin)
{
	return (rd(gpio, GPIO_DIN0) >> pin) & 1u;
}

static inline void
pin_settle(void)
{
	(void)rd(gpio, GPIO_DIN0);
	(void)rd(gpio, GPIO_DIN0);
}

static void
pins_to_spi(void)
{
	uint32_t mode = rd(gpio, GPIO_MODE4);
	const unsigned pins[4] = { FTS_SPI_PIN_CS, FTS_SPI_PIN_CLK, FTS_SPI_PIN_MO, FTS_SPI_PIN_MI };

	for (unsigned i = 0; i < 4; i++) {
		unsigned shift = (pins[i] % 8u) * 4u;

		mode = (mode & ~(0xfu << shift)) | (FTS_SPI_FUNC << shift);
	}
	wr(gpio, GPIO_MODE4, mode);
	__asm__ volatile ("dsb sy");
}

static void
pins_to_gpio(void)
{
	uint32_t mode = rd(gpio, GPIO_MODE4);
	const unsigned pins[4] = { FTS_SPI_PIN_CS, FTS_SPI_PIN_CLK, FTS_SPI_PIN_MO, FTS_SPI_PIN_MI };

	for (unsigned i = 0; i < 4; i++) {
		mode &= ~(0xfu << ((pins[i] % 8u) * 4u));
	}
	wr(gpio, GPIO_MODE4, mode);
	wr(gpio, GPIO_DIR0_SET, (1u << FTS_SPI_PIN_MO) | (1u << FTS_SPI_PIN_CLK) | (1u << FTS_SPI_PIN_CS));
	wr(gpio, GPIO_DIR0_CLR, 1u << FTS_SPI_PIN_MI);
	pin_set(FTS_SPI_PIN_CS, true);
	pin_set(FTS_SPI_PIN_CLK, false);        /* mode 0: clock idles low */
	__asm__ volatile ("dsb sy");
}

/* ---- the SPI controller ------------------------------------------------------------------- */

static void
spi_setup(void)
{
	wr(spi, SPI_CMD, SPI_CMD_RST);
	wr(spi, SPI_CMD, 0);
	wr(spi, SPI_CFG2, ((spi_ticks - 1u) << 0) | ((spi_ticks - 1u) << 16));
	wr(spi, SPI_CFG0, ((spi_ticks - 1u) << 0) | ((spi_ticks - 1u) << 16));
	wr(spi, SPI_PAD_SEL, 0);                /* mediatek,pad-select = <0>, fdt.dts:4488 */
}

static bool
spi_fifo(const uint8_t *tx, uint8_t *rx, uint32_t length)
{
	uint32_t bad[4];
	uint32_t words;

	if (length == 0 || length > SPI_FIFO_BYTES) {
		return false;
	}
	words = (length + 3u) / 4u;
	spi_setup();
	wr(spi, SPI_CMD, 0);                    /* mode 0: Phase_Pola=0, Conf_MultipleTest.ini:31 */
	wr(spi, SPI_CFG1, ((length - 1u) << 16) | ((spi_ticks - 1u) << 0));

	for (uint32_t word = 0; word < words; word++) {
		uint32_t value = 0;

		for (uint32_t byte = 0; byte < 4; byte++) {
			uint32_t at = word * 4u + byte;

			value |= (uint32_t)(tx && at < length ? tx[at] : 0xffu) << (8u * byte);
		}
		wr(spi, SPI_TX_DATA, value);
	}
	wr(spi, SPI_CMD, rd(spi, SPI_CMD) | SPI_CMD_ACT);

	for (uint32_t spin = 0; !(rd(spi, SPI_STATUS0) & SPI_STATUS0_DONE); spin++) {
		if (spin > X6886_SPI_SPIN_MAX) {
			bad[0] = rd(spi, SPI_CMD);
			bad[1] = rd(spi, SPI_STATUS0);
			bad[2] = rd(spi, SPI_CFG1);
			bad[3] = length;
			x6886_trace(STAGE_SPI, 1, bad, 4);
			return false;
		}
	}
	for (uint32_t word = 0; word < words; word++) {
		uint32_t value = rd(spi, SPI_RX_DATA);

		for (uint32_t byte = 0; byte < 4; byte++) {
			uint32_t at = word * 4u + byte;

			if (rx && at < length) {
				rx[at] = (uint8_t)(value >> (8u * byte));
			}
		}
	}
	return true;
}

/* Bit-bang fallback. Pin roles come from the DT pin list and are an inference; off by default. */
static void
spi_by_hand(const uint8_t *tx, uint8_t *rx, uint32_t length, uint32_t gap_us)
{
	pins_to_gpio();
	pin_set(FTS_SPI_PIN_CS, false);
	pin_settle();

	for (uint32_t at = 0; at < length; at++) {
		uint8_t out = tx ? tx[at] : 0xffu, in = 0;

		for (int bit = 7; bit >= 0; bit--) {
			pin_set(FTS_SPI_PIN_MO, (out >> bit) & 1u);
			pin_set(FTS_SPI_PIN_CLK, false);
			pin_settle();
			pin_set(FTS_SPI_PIN_CLK, true);
			pin_settle();
			in = (uint8_t)((in << 1) | (pin_get(FTS_SPI_PIN_MI) ? 1u : 0u));
		}
		if (rx) {
			rx[at] = in;
		}
		if (gap_us) {
			delay(gap_us);
		}
	}
	pin_set(FTS_SPI_PIN_CS, true);
	pin_settle();
	pins_to_spi();
}

static bool
spi_transfer(const uint8_t *tx, uint8_t *rx, uint32_t length)
{
	if (spi_fifo(tx, rx, length)) {
		return true;
	}
	stalls++;
	if (!bitbang_enabled) {
		x6886_trace(STAGE_SPI, 2, NULL, 0);
		return false;
	}
	spi_by_hand(tx, rx, length, 0);
	return true;
}

/* ---- FT3683G: one read command, one bounded frame ---------------------------------------- */

static bool
fts_read_frame(uint8_t *rx, uint32_t length)
{
	uint8_t tx[FTS_BUF_BYTES], wire[FTS_BUF_BYTES];

	if (length == 0 || length + 2u > SPI_FIFO_BYTES || layout.reg == 0) {
		x6886_trace(STAGE_READ, 1, NULL, 0);
		return false;
	}
	memset(tx, 0xff, sizeof(tx));
	tx[0] = FTS_READ_CMD;
	tx[1] = (uint8_t)layout.reg;
	if (!spi_transfer(tx, wire, length + 2u)) {
		return false;
	}
	for (uint32_t i = 0; i < length; i++) {
		rx[i] = wire[i + 2u];
	}
	return true;
}

static uint32_t
be_value(const uint8_t *p, uint32_t bytes)
{
	uint32_t value = 0;

	for (uint32_t i = 0; i < bytes; i++) {
		value = (value << 8) | p[i];
	}
	return value;
}

static bool
down[FTS_MAX_POINTS];

static void
report(const uint8_t *buffer, uint32_t length)
{
	bool seen[FTS_MAX_POINTS] = { false };
	uint32_t count, used;

	if (layout.head == 0 || length < layout.head) {
		x6886_trace(STAGE_REPORT, 1, NULL, 0);
		return;
	}
	count = buffer[0];
	if (count > FTS_MAX_POINTS) {
		count = FTS_MAX_POINTS;
	}
	used = layout.head;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t at = used + i * layout.point, x, y;

		if (at + layout.point > length) {
			break;
		}
		x = be_value(buffer + at + layout.x_off, layout.xy_bytes);
		y = be_value(buffer + at + layout.y_off, layout.xy_bytes);
		if (x > FTS_X_MAX) {
			x = FTS_X_MAX;
		}
		if (y > FTS_Y_MAX) {
			y = FTS_Y_MAX;
		}
		last_x = x;
		last_y = y;
		seen[i] = true;
		touches++;
		hakios_touch_post(down[i] ? HAKIOS_TOUCH_MOVE : HAKIOS_TOUCH_DOWN, i,
		    x * 65535u / FTS_X_MAX, y * 65535u / FTS_Y_MAX);
		down[i] = true;
	}
	for (uint32_t i = 0; i < FTS_MAX_POINTS; i++) {
		if (down[i] && !seen[i]) {
			hakios_touch_post(HAKIOS_TOUCH_UP, i,
			    last_x * 65535u / FTS_X_MAX, last_y * 65535u / FTS_Y_MAX);
			down[i] = false;
		}
	}
	reports++;
}

/* ---- boot-arg validation: nothing is defaulted when the dump has no value ---------------- */

static bool
arg_u32(const char *name, uint32_t *value, uint32_t low, uint32_t high)
{
	uint32_t v = 0;

	if (!PE_parse_boot_argn(name, &v, sizeof(v))) {
		return false;
	}
	if (v < low || v > high) {
		printf("[X6886-touch] %s=%u outside %u..%u\n", name, v, low, high);
		return false;
	}
	*value = v;
	return true;
}

static bool
fts_layout_from_args(void)
{
	memset(&layout, 0, sizeof(layout));
	if (!arg_u32("fts_reg", &layout.reg, 1, 255)) {
		return false;
	}
	if (!arg_u32("fts_frame", &layout.frame, 1, SPI_FIFO_BYTES - 2u)) {
		return false;
	}
	if (!arg_u32("fts_head", &layout.head, 1, layout.frame - 1u)) {
		return false;
	}
	if (!arg_u32("fts_point", &layout.point, 1, layout.frame - layout.head)) {
		return false;
	}
	if (!arg_u32("fts_x_off", &layout.x_off, 0, layout.point - 1u)) {
		return false;
	}
	if (!arg_u32("fts_y_off", &layout.y_off, 0, layout.point - 1u)) {
		return false;
	}
	if (!arg_u32("fts_xy_bytes", &layout.xy_bytes, 1, 2)) {
		return false;
	}
	if (layout.x_off + layout.xy_bytes > layout.point ||
	    layout.y_off + layout.xy_bytes > layout.point) {
		return false;
	}
	(void)arg_u32("fts_reset_ms", &layout.reset_ms, 0, 1000);
	printf("[X6886-touch] FTS layout reg=0x%x frame=%u head=%u point=%u x=%u y=%u xy=%u\n",
	    layout.reg, layout.frame, layout.head, layout.point,
	    layout.x_off, layout.y_off, layout.xy_bytes);
	return true;
}

/* ---- boot safety ------------------------------------------------------------------------- */

static bool
x6886_ready(void)
{
	uint32_t bad[6], orig, sample;

	spi = ml_io_map(X6886_SPI1_PHYS, X6886_SPI_SIZE);
	gpio = ml_io_map(X6886_GPIO_PHYS, X6886_GPIO_SIZE);
	ramoops = ml_io_map(X6886_RAMOOPS_PHYS, X6886_RAMOOPS_SIZE);
	bad[0] = (uint32_t)spi;
	bad[1] = (uint32_t)gpio;
	if (spi == 0 || gpio == 0) {
		x6886_trace(STAGE_MAP, 1, bad, 2);
		return false;
	}

	/* Power readiness before any SPI transaction: AVDD and IOVDD are driven by the loader. */
	bad[2] = rd(gpio, GPIO_DIN0);
	if (!pin_get(FTS_PIN_AVDD) || !pin_get(FTS_PIN_IOVDD)) {
		x6886_trace(STAGE_POWER, 1, bad, 3);
		return false;
	}

	/* Clock readiness: the SPI window must accept and hold a written pattern. */
	topckgen = ml_io_map(X6886_TOPCKGEN_PHYS, X6886_CLOCK_SIZE);
	infracfg = ml_io_map(X6886_INFRACFG_PHYS, X6886_CLOCK_SIZE);
	if (topckgen == 0 || infracfg == 0) {
		x6886_trace(STAGE_CLOCK, 1, bad, 3);
		return false;
	}
	bad[3] = rd(topckgen, 0);
	bad[4] = rd(infracfg, 0);
	orig = rd(spi, SPI_CFG2);
	wr(spi, SPI_CFG2, 0x5a5aa55au);
	sample = rd(spi, SPI_CFG2);
	wr(spi, SPI_CFG2, orig);
	bad[5] = sample;
	if (sample != 0x5a5aa55au) {
		x6886_trace(STAGE_CLOCK, 2, bad, 6);
		return false;
	}
	pins_to_spi();
	spi_setup();
	return true;
}

static bool
fts_power_reset(void)
{
	uint32_t din = rd(gpio, GPIO_DIN0), status = rd(spi, SPI_STATUS0);

	if (layout.reset_ms > 0) {
		pin_set(FTS_PIN_RESET, false);
		delay((int)layout.reset_ms * 1000);
		pin_set(FTS_PIN_RESET, true);
		delay(10000);
	}
	if (!pin_get(FTS_PIN_IRQ)) {
		/* An asserted line with no report yet is legal; do not fail on it. */
		printf("[X6886-touch] IRQ line asserted before first read\n");
	}
	printf("[X6886-touch] GPIO DIN 0x%x, SPI STATUS0 0x%x\n", din, status);
	return true;
}

static bool
fts_presence(void)
{
	uint8_t buf[2];
	uint32_t bad[2];

	if (!fts_read_frame(buf, sizeof(buf))) {
		return false;
	}
	bad[0] = buf[0];
	bad[1] = buf[1];
	if ((buf[0] == 0x00 && buf[1] == 0x00) || (buf[0] == 0xff && buf[1] == 0xff)) {
		x6886_trace(STAGE_PRESENCE, 1, bad, 2);
		return false;
	}
	printf("[X6886-touch] FT3683G responds on SPI1: 0x%02x 0x%02x\n", buf[0], buf[1]);
	return true;
}

/* ---- bounded poll thread ------------------------------------------------------------------ */

static void
tcm_thread(__unused void *parameter, __unused wait_result_t result)
{
	uint8_t buffer[FTS_BUF_BYTES];
	uint32_t tick;

	for (tick = 1; tick <= max_ticks; tick++) {
		uint32_t spins = 0;

		while (!pin_get(FTS_PIN_IRQ) && spins < X6886_IRQ_SPIN_MAX) {
			spins++;
			if (!fts_read_frame(buffer, layout.frame)) {
				break;
			}
			report(buffer, layout.frame);
		}
		if (tick % 750u == 0u) {
			printf("[X6886-touch] %u ticks, %u reports, %u points, last %u,%u, %u stalls\n",
			    tick, reports, touches, last_x, last_y, stalls);
		}
		delay((int)poll_us);
	}
	printf("[X6886-touch] bounded session ended after %u ticks\n", tick - 1u);
}

void
x6886_touch_init(void)
{
	uint32_t step = 0, one = 0;
	kern_return_t kr;

	if (!PE_parse_boot_argn("x6886_touch", &step, sizeof(step)) || step == 0) {
		return;
	}
	if (step > 3) {
		step = 3;
	}
	(void)arg_u32("x6886_spi_ticks", &spi_ticks, 1, 16);
	(void)arg_u32("x6886_poll_us", &poll_us, 250, 100000);
	(void)arg_u32("x6886_ticks", &max_ticks, 1, 3600000);
	if (PE_parse_boot_argn("x6886_bitbang", &one, sizeof(one)) && one == 1) {
		bitbang_enabled = true;
	}
	printf("[X6886-touch] X6886 FT3683G on SPI1, step %u, ticks=%u, poll=%u us\n",
	    step, spi_ticks, poll_us);
	if (FTS_X_MAX / 16u != FTS_PIXEL_X - 1u || FTS_Y_MAX / 16u != FTS_PIXEL_Y - 1u) {
		printf("[X6886-touch] dump coords and ini pixels disagree; nothing is touched\n");
		return;
	}
	if (!x6886_ready()) {
		return;
	}
	if (step >= 2) {
		if (!fts_layout_from_args()) {
			x6886_trace(STAGE_LAYOUT, 1, NULL, 0);
			printf("[X6886-touch] FTS frame layout is not in the dump; supply fts_* boot args\n");
			return;
		}
		if (!fts_power_reset() || !fts_presence()) {
			return;
		}
	}
	if (step >= 3) {
		kr = kernel_thread_start(tcm_thread, NULL, &touch_thread);
		if (kr == KERN_SUCCESS) {
			thread_deallocate(touch_thread);
			printf("[X6886-touch] poll thread started\n");
		} else {
			x6886_trace(STAGE_THREAD, (uint32_t)kr, NULL, 0);
			printf("[X6886-touch] cannot start the poll thread\n");
		}
	}
}

void
x6886_touch_stop(void)
{
	if (touch_thread != NULL) {
		(void)thread_terminate(touch_thread);
		touch_thread = NULL;
	}
	if (ramoops != 0) {
		ml_io_unmap(ramoops, X6886_RAMOOPS_SIZE);
		ramoops = 0;
	}
	if (spi != 0) {
		ml_io_unmap(spi, X6886_SPI_SIZE);
		spi = 0;
	}
	if (gpio != 0) {
		ml_io_unmap(gpio, X6886_GPIO_SIZE);
		gpio = 0;
	}
	if (topckgen != 0) {
		ml_io_unmap(topckgen, X6886_CLOCK_SIZE);
		topckgen = 0;
	}
	if (infracfg != 0) {
		ml_io_unmap(infracfg, X6886_CLOCK_SIZE);
		infracfg = 0;
	}
}
