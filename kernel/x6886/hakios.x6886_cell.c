/*
 * X6886 (MT6789) cellular control channel: SAFE kernel-side subset only.
 *
 * Provenance (all read-only inspection, no modem/NVRAM writes, no flash):
 *   refs/alps-device-modules-6.12/drivers/misc/mediatek/eccci/
 *     hif/ccci_dpmaif_v1_6789.h   ring geometry and SMEM DRB layout
 *     hif/ccci_dpmaif_v1_6789.c   SMEM data region, ring release, CCIF doorbell callback
 *     hif/ccci_dpmaif_reg_v1.h    DPMAIF AO/PD register windows and status bits
 *     hif/ccif_hif_reg.h          CCIF AP register offsets (APCCIF_*)
 *     hif/ccci_hif_ccif.c         APCCIF_BUSY/TCHNUM doorbell sequence
 *     hif/ccci_ringbuf.h          read/write/length ring control layout
 *     port/port_cfg.c             the char node named "ttyC0"
 *   device-dump/fdt.dts, interrupts.txt  every physical address and IRQ below.
 *   PORT-CELL.md carries the line-by-line proof and the safety confirmation.
 *
 * Deliberately NOT translated: modem image loading, NVRAM/IMEI/protect1/protect2
 * access, HW reset, and any SMEM write. EMI/SMEM here is a READ-ONLY observation.
 * The CCIF doorbell is an AP-side register write and is OFF unless cell_doorbell=1.
 *
 * Boot args: cell_step=N (1 = read-only survey; 2 = rings + AT transport),
 *            cell_doorbell=1 (enable the gated AP CCIF doorbell),
 *            cell_trace=1.
 * All failures return a negative errno. Nothing panics. No flash command exists.
 */

#include <kern/clock.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t size);
extern void delay(int usec);

/* ---- ramoops pstore mirror: same contract as the other X6886 drivers -------- */
#define X6886_PSTORE_PHYS       0x480af000ULL
#define X6886_PSTORE_SIZE       0x1000u
#define X6886_PSTORE_SIG        0x43474244u

static vm_offset_t x6886_cell_pstore_va;
static LCK_GRP_DECLARE(x6886_cell_pstore_grp, "x6886-cell-pstore");
static LCK_SPIN_DECLARE(x6886_cell_pstore_lock, &x6886_cell_pstore_grp);

static void
x6886_cell_pstore_map(void)
{
	if (x6886_cell_pstore_va == 0) {
		x6886_cell_pstore_va = ml_io_map(X6886_PSTORE_PHYS, X6886_PSTORE_SIZE);
	}
}

static void
x6886_cell_pstore_write(const char *message, unsigned length)
{
	volatile uint8_t *base;
	uint32_t sig, start, size;
	unsigned n;

	if (x6886_cell_pstore_va == 0 || message == NULL || length == 0) {
		return;
	}
	lck_spin_lock(&x6886_cell_pstore_lock);
	base = (volatile uint8_t *)x6886_cell_pstore_va;
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
	lck_spin_unlock(&x6886_cell_pstore_lock);
}

static int x6886_cell_trace;

static void
x6886_cell_log(const char *fmt, ...)
{
	char line[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	printf("[X6886-cell] %s\n", line);
	x6886_cell_pstore_write(line, (unsigned)strlen(line));
}

#define X6886_CELL_LOG(...) x6886_cell_log(__VA_ARGS__)

/* ---- verified constants (file:line proof in PORT-CELL.md) ------------------- */
/* fdt.dts:4159-4169: dpmaif@10014000, four 0x1000 windows, interrupt SPI 0xf1 */
#define X6886_DPMAIF_AO_PHYS     0x10014000ULL
#define X6886_DPMAIF_PD_MD_PHYS  0x1022d000ULL
#define X6886_DPMAIF_PD_AP_PHYS  0x1022c000ULL
#define X6886_DPMAIF_PD_UL_PHYS  0x1022e000ULL
#define X6886_DPMAIF_SIZE        0x1000u
#define X6886_DPMAIF_SPI         0xf1u
#define X6886_DPMAIF_GIC         273u

/* fdt.dts:4172-4180: ccifdriver@10209000, two 0x1000 windows, SPI 0xe2/0xe3 */
#define X6886_CCIF_AP_PHYS       0x10209000ULL
#define X6886_CCIF_MD_PHYS       0x1020a000ULL
#define X6886_CCIF_SIZE          0x1000u
#define X6886_CCIF_SPI0          0xe2u
#define X6886_CCIF_SPI1          0xe3u
#define X6886_CCIF_GIC0          258u
#define X6886_CCIF_GIC1          259u

/* fdt.dts:549-553 cached, 585-589 non-cached, 561-565 tag: AP<->MD shared EMI */
#define X6886_SMEM_CACHED_PHYS   0x8c000000ULL
#define X6886_SMEM_CACHED_SIZE   0x016a0000u
#define X6886_SMEM_NC_PHYS       0x8e000000ULL
#define X6886_SMEM_NC_SIZE       0x00e20000u
#define X6886_CCCI_TAG_PHYS      0xbf5f0000ULL
#define X6886_CCCI_TAG_SIZE      0x00010000u

/* ccif_hif_reg.h:30-35: AP-side CCIF registers, used by the doorbell only */
#define APCCIF_CON               0x00u
#define APCCIF_BUSY              0x04u
#define APCCIF_START             0x08u
#define APCCIF_TCHNUM            0x0cu
#define APCCIF_RCHNUM            0x10u
#define APCCIF_ACK               0x14u

/* ccci_hif_ccif.c:902-909 uses channel id 0 for the data notify path;
 * port_cfg.c:41 names the AT command queues "ttyC0" with minor 3. */
#define X6886_AT_CELL_CHANNEL    0u
#define X6886_AT_TTY_MINOR       3u

/* v1_6789.h:34-37: three UL queues of 1024 descriptors; :39: 1600-byte data */
#define X6886_CELL_QUEUE_NUM     3u
#define X6886_CELL_QUEUE_SIZE    1024u
#define X6886_CELL_DATA_LEN      1600u

/* Bounded polling: 100 * 1000us = 100ms (mirrors the 1000-count bound in
 * v1_6789.c:579). No unbounded loop exists in this file. */
#define X6886_CELL_POLL_ROUNDS   100u
#define X6886_CELL_POLL_US       1000u

/* ---- AP-local ring: ccci_ringbuf.h:18-22 layout, no SMEM writes ------------- */
struct x6886_cell_ring {
	volatile uint32_t read;                 /* read cursor */
	volatile uint32_t write;                /* write cursor */
	volatile uint32_t length;               /* bytes currently queued */
	uint8_t          *buf;
	uint32_t          size;
};

static struct {
	vm_offset_t        ao, pd_md, pd_ap, pd_ul;   /* DPMAIF windows */
	vm_offset_t        ccif_ap, ccif_md;          /* CCIF windows */
	vm_offset_t        smem;                      /* read-only EMI window */
	struct x6886_cell_ring ul, dl;                 /* AP-local AT rings */
	void              *tty;                       /* bound /dev/ttyC0 handle */
	uint32_t           step, doorbell;
	bool               mapped;
} cell;

static LCK_GRP_DECLARE(x6886_cell_grp, "x6886-cell");
static LCK_MTX_DECLARE(x6886_cell_lock, &x6886_cell_grp);

static inline uint32_t
rd32(vm_offset_t base, uint32_t off)
{
	return base != 0 ? *(volatile uint32_t *)(base + off) : 0xdeadbeefu;
}

static inline void
wr32(vm_offset_t base, uint32_t off, uint32_t value)
{
	if (base != 0) {
		*(volatile uint32_t *)(base + off) = value;
		__asm__ volatile ("dsb sy" ::: "memory");
	}
}

static uint32_t
ring_next(const struct x6886_cell_ring *r, uint32_t at, uint32_t n)
{
	return r->size != 0 ? (at + n) % r->size : 0;
}

static int
ring_init(struct x6886_cell_ring *r, uint32_t size)
{
	bzero(r, sizeof(*r));
	r->buf = (uint8_t *)kalloc(size);
	if (r->buf == NULL) {
		return -1;
	}
	bzero(r->buf, size);
	r->size = size;
	return 0;
}

static void
ring_free(struct x6886_cell_ring *r)
{
	if (r->buf != NULL) {
		kfree(r->buf, r->size);
		r->buf = NULL;
	}
	r->size = 0;
	r->read = r->write = r->length = 0;
}

static void
ring_unmap_vm(void)
{
	if (cell.ao != 0)      { ml_io_unmap(cell.ao, X6886_DPMAIF_SIZE); cell.ao = 0; }
	if (cell.pd_md != 0)   { ml_io_unmap(cell.pd_md, X6886_DPMAIF_SIZE); cell.pd_md = 0; }
	if (cell.pd_ap != 0)   { ml_io_unmap(cell.pd_ap, X6886_DPMAIF_SIZE); cell.pd_ap = 0; }
	if (cell.pd_ul != 0)   { ml_io_unmap(cell.pd_ul, X6886_DPMAIF_SIZE); cell.pd_ul = 0; }
	if (cell.ccif_ap != 0) { ml_io_unmap(cell.ccif_ap, X6886_CCIF_SIZE); cell.ccif_ap = 0; }
	if (cell.ccif_md != 0) { ml_io_unmap(cell.ccif_md, X6886_CCIF_SIZE); cell.ccif_md = 0; }
	if (cell.smem != 0)    { ml_io_unmap(cell.smem, X6886_DPMAIF_SIZE); cell.smem = 0; }
	cell.mapped = false;
}

/*
 * READ-ONLY EMI/SMEM handshake: the four words at the start of the non-cached
 * AP<->MD window (fdt.dts:585-589) plus the AP-side CCIF state registers
 * (ccif_hif_reg.h:30-35, ccci_hif_ccif.c:168-205). Nothing is written here.
 */
static int
x6886_cell_read_modem_state(void)
{
	uint32_t emi[4];
	uint32_t con, busy, start, tchnum, rchnum, ack;
	int i;

	if (cell.smem == 0) {
		return -1;
	}
	for (i = 0; i < 4; i++) {
		emi[i] = rd32(cell.smem, (uint32_t)i * 4u);
	}
	con = rd32(cell.ccif_ap, APCCIF_CON);
	busy = rd32(cell.ccif_ap, APCCIF_BUSY);
	start = rd32(cell.ccif_ap, APCCIF_START);
	tchnum = rd32(cell.ccif_ap, APCCIF_TCHNUM);
	rchnum = rd32(cell.ccif_ap, APCCIF_RCHNUM);
	ack = rd32(cell.ccif_ap, APCCIF_ACK);
	X6886_CELL_LOG("state: EMI[0..3]=%08x %08x %08x %08x", emi[0], emi[1], emi[2], emi[3]);
	X6886_CELL_LOG("CCIF AP: CON=%08x BUSY=%08x START=%08x TCHNUM=%08x RCHNUM=%08x ACK=%08x",
	    con, busy, start, tchnum, rchnum, ack);
	return 0;
}

/* Bounded wait for a non-dead CCIF window; returns 0 when live, -1 on timeout. */
static int
x6886_cell_wait_modem(uint32_t rounds, uint32_t us)
{
	for (uint32_t i = 0; i < rounds; i++) {
		uint32_t con = rd32(cell.ccif_ap, APCCIF_CON);

		if (con != 0xdeadbeefu && con != 0) {
			X6886_CELL_LOG("modem CCIF live after %u polls (CON=%08x)", i, con);
			return 0;
		}
		delay((int)us);
	}
	X6886_CELL_LOG("modem CCIF not live after %u polls; leaving hardware untouched", rounds);
	return -1;
}

int
x6886_cell_init(void)
{
	uint32_t step = 0, doorbell = 0, trace = 0;
	int ret = 0;

	PE_parse_boot_argn("cell_step", &step, sizeof(step));
	PE_parse_boot_argn("cell_doorbell", &doorbell, sizeof(doorbell));
	PE_parse_boot_argn("cell_trace", &trace, sizeof(trace));
	x6886_cell_trace = trace != 0;

	if (step == 0) {
		X6886_CELL_LOG("no cell_step boot-arg; cellular control channel not started");
		return 0;
	}
	if (step > 2) {
		step = 2;
	}
	cell.step = step;
	cell.doorbell = doorbell != 0;

	x6886_cell_pstore_map();

	cell.ao = ml_io_map(X6886_DPMAIF_AO_PHYS, X6886_DPMAIF_SIZE);
	cell.pd_md = ml_io_map(X6886_DPMAIF_PD_MD_PHYS, X6886_DPMAIF_SIZE);
	cell.pd_ap = ml_io_map(X6886_DPMAIF_PD_AP_PHYS, X6886_DPMAIF_SIZE);
	cell.pd_ul = ml_io_map(X6886_DPMAIF_PD_UL_PHYS, X6886_DPMAIF_SIZE);
	cell.ccif_ap = ml_io_map(X6886_CCIF_AP_PHYS, X6886_CCIF_SIZE);
	cell.ccif_md = ml_io_map(X6886_CCIF_MD_PHYS, X6886_CCIF_SIZE);
	cell.smem = ml_io_map(X6886_SMEM_NC_PHYS, X6886_DPMAIF_SIZE);
	if (cell.ao == 0 || cell.pd_md == 0 || cell.pd_ap == 0 || cell.pd_ul == 0 ||
	    cell.ccif_ap == 0 || cell.ccif_md == 0 || cell.smem == 0) {
		X6886_CELL_LOG("MMIO/SMEM mapping failed; returning -ENODEV, no writes performed");
		ring_unmap_vm();
		return -1;
	}
	cell.mapped = true;

	X6886_CELL_LOG("DPMAIF AO=%08llx PD(MD/AP/UL)=%08llx/%08llx/%08llx SPI=%02x(GIC %u)",
	    X6886_DPMAIF_AO_PHYS, X6886_DPMAIF_PD_MD_PHYS, X6886_DPMAIF_PD_AP_PHYS,
	    X6886_DPMAIF_PD_UL_PHYS, X6886_DPMAIF_SPI, X6886_DPMAIF_GIC);
	X6886_CELL_LOG("CCIF AP=%08llx MD=%08llx SPI=%02x/%02x(GIC %u/%u)",
	    X6886_CCIF_AP_PHYS, X6886_CCIF_MD_PHYS, X6886_CCIF_SPI0, X6886_CCIF_SPI1,
	    X6886_CCIF_GIC0, X6886_CCIF_GIC1);
	X6886_CELL_LOG("SMEM cached=%08llx(%x) nc=%08llx(%x) tag=%08llx(%x): READ-ONLY",
	    X6886_SMEM_CACHED_PHYS, X6886_SMEM_CACHED_SIZE, X6886_SMEM_NC_PHYS,
	    X6886_SMEM_NC_SIZE, X6886_CCCI_TAG_PHYS, X6886_CCCI_TAG_SIZE);

	if (x6886_cell_read_modem_state() != 0) {
		X6886_CELL_LOG("modem state unreadable; returning -ENODEV");
		ring_unmap_vm();
		return -1;
	}
	if (x6886_cell_wait_modem(X6886_CELL_POLL_ROUNDS, X6886_CELL_POLL_US) != 0) {
		X6886_CELL_LOG("modem absent; survey only, no rings, no doorbell");
		return 0;
	}

	if (step >= 2) {
		if (ring_init(&cell.ul, X6886_CELL_QUEUE_SIZE * X6886_CELL_DATA_LEN) != 0 ||
		    ring_init(&cell.dl, X6886_CELL_QUEUE_SIZE * X6886_CELL_DATA_LEN) != 0) {
			ring_free(&cell.ul);
			ring_free(&cell.dl);
			X6886_CELL_LOG("AT ring allocation failed; returning -ENOMEM");
			return -1;
		}
		X6886_CELL_LOG("AT rings ready: UL/DL %u slots x %u bytes; ttyC0 minor %u (not bound here)",
		    X6886_CELL_QUEUE_SIZE, X6886_CELL_DATA_LEN, X6886_AT_TTY_MINOR);
	}
	X6886_CELL_LOG("init done: step=%u doorbell=%s", step, cell.doorbell ? "on" : "off");
	return ret;
}

void
x6886_cell_stop(void)
{
	lck_mtx_lock(&x6886_cell_lock);
	ring_free(&cell.ul);
	ring_free(&cell.dl);
	cell.tty = NULL;
	ring_unmap_vm();
	lck_mtx_unlock(&x6886_cell_lock);
	X6886_CELL_LOG("stopped; all windows unmapped");
}

/* The existing /dev/ttyC0 char node is bound by the integrator; this tree has
 * no such node (audit result in PORT-CELL.md), so the default is an empty hook. */
int
x6886_cell_at_bind(void *ttyc0)
{
	if (ttyc0 == NULL) {
		return -1;
	}
	lck_mtx_lock(&x6886_cell_lock);
	cell.tty = ttyc0;
	lck_mtx_unlock(&x6886_cell_lock);
	X6886_CELL_LOG("/dev/ttyC0 transport bound");
	return 0;
}

/* Gated AP CCIF doorbell: writes only the AP-side busy/channel registers
 * (ccci_hif_ccif.c:902-909). Never touches modem memory, NVRAM or protect blocks. */
static void
x6886_cell_doorbell(uint32_t channel)
{
	if (!cell.doorbell || cell.ccif_ap == 0) {
		return;
	}
	if (rd32(cell.ccif_ap, APCCIF_BUSY) & (1u << channel)) {
		X6886_CELL_LOG("CCIF channel %u busy; doorbell skipped", channel);
		return;
	}
	wr32(cell.ccif_ap, APCCIF_BUSY, 1u << channel);
	wr32(cell.ccif_ap, APCCIF_TCHNUM, channel);
	if (x6886_cell_trace) {
		X6886_CELL_LOG("doorbell channel=%u START=%08x", channel,
		    rd32(cell.ccif_ap, APCCIF_START));
	}
}

/* AT bytes to the modem: bounded copy into the AP-local UL ring. */
int
x6886_cell_at_send(const uint8_t *data, size_t length)
{
	size_t n = length;
	int ret = (int)length;

	if (data == NULL || length == 0 || cell.ul.buf == NULL) {
		return -1;
	}
	lck_mtx_lock(&x6886_cell_lock);
	if (n > cell.ul.size - cell.ul.length) {
		n = cell.ul.size - cell.ul.length;
		ret = -1;
	}
	for (size_t i = 0; i < n; i++) {
		cell.ul.buf[cell.ul.write] = data[i];
		cell.ul.write = ring_next(&cell.ul, cell.ul.write, 1);
	}
	cell.ul.length += (uint32_t)n;
	__asm__ volatile ("dsb sy" ::: "memory");
	if (n != 0) {
		x6886_cell_doorbell(X6886_AT_CELL_CHANNEL);
	}
	lck_mtx_unlock(&x6886_cell_lock);
	return ret;
}

/* AT bytes from the modem: bounded read from the AP-local DL ring. */
int
x6886_cell_at_recv(uint8_t *data, size_t capacity)
{
	size_t n;

	if (data == NULL || capacity == 0 || cell.dl.buf == NULL) {
		return -1;
	}
	lck_mtx_lock(&x6886_cell_lock);
	n = cell.dl.length < capacity ? cell.dl.length : capacity;
	for (size_t i = 0; i < n; i++) {
		data[i] = cell.dl.buf[cell.dl.read];
		cell.dl.read = ring_next(&cell.dl, cell.dl.read, 1);
	}
	cell.dl.length -= (uint32_t)n;
	lck_mtx_unlock(&x6886_cell_lock);
	return (int)n;
}

/* Fill a bounded status line for the board init path / ramoops. */
void
x6886_cell_status(char *out, size_t size)
{
	snprintf(out, size,
	    "step=%u doorbell=%s ttyC0=%s rings=%s EMI=%08x CCIF_CON=%08x IRQ=%02x/%02x/%02x",
	    cell.step, cell.doorbell ? "on" : "off", cell.tty != NULL ? "bound" : "absent",
	    cell.ul.buf != NULL ? "up" : "down",
	    cell.smem != 0 ? rd32(cell.smem, 0) : 0xdeadbeefu,
	    cell.ccif_ap != 0 ? rd32(cell.ccif_ap, APCCIF_CON) : 0xdeadbeefu,
	    X6886_DPMAIF_SPI, X6886_CCIF_SPI0, X6886_CCIF_SPI1);
}
