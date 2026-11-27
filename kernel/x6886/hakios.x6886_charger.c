/*
 * X6886 (MT6789) SC8950 + SC8548 charging: SAFE 5 V path, NO pump, NO writes to SC8548.
 *
 * Both parts and their buses come from the device's own FDT:
 *   SC8950  : i2c5 0x6b, "southchip,sc8950"       : fdt.dts:5125-5127
 *   SC8548  : i2c7 0x6f, "sc,sc8548-standalone"   : fdt.dts:5245-5247
 *   i2c5 MMIO 0x11017000 + 0x1000                 : fdt.dts:4860, iomem.txt:41,100
 *   i2c7 MMIO 0x11f00000 + 0x1000                 : fdt.dts:5233, iomem.txt:43,113
 * The primary SC8548 is only probed with a READ, at a register offset supplied by
 * x6886_sc8548_reg=N: no SC8548 register offset exists in the dump or in the
 * alps source tree, so it is REF and never enabled without that opt-in.
 * SC8950 regulator programming stays unchanged and is summarised below.
 * Register source (genuine, no invented offsets):
 *   refs/alps-device-modules-6.12/drivers/power/supply/bq2589x_reg.h
 *     REG_00 / EN_HIZ / EN_ILIM / IINLIM            : 21-34
 *     REG_03 / WDT_RESET / OTG_CONFIG / CHG_CONFIG  : 117-135
 *     REG_04 / ICHG / SC89890H ICHG scale           : 146-156
 *     REG_05 / ITERM / SC89890H ITERM scale         : 159-171
 *     REG_06 / VREG base + LSB                      : 174-186
 *     REG_07 / EN_TERM / WDT mask                   : 189-205
 *     REG_0B / VBUS_STAT / CHRG_STAT / PG_STAT      : 298-318
 *     REG_0C / fault bits                           : 325-344
 *     REG_0D / FORCE_VINDPM / VINDPM scale          : 347-361
 *     REG_11 / VBUS_GD                              : 390-391
 *   Scaling helpers: bq2589x_charger.c:390-405 (set charge current),
 *     :407-423 (set termination current), :1289-1300 (bounded safe init),
 *     :136-155 (byte read/write).
 *   Charger-presence interface: transsion/tc_charger_intf.c:94-119
 *     (ONLINE property is asked before any charger action is taken).
 *
 * Device proof (fdt.dts and dmesg.txt from the device's own dump):
 *   sc-swchg@6b, "southchip,sc8950\0mediatek,sgm41516d" : fdt.dts:5125-5126
 *   I2C5 controller                                     : fdt.dts:4859-4862
 *   sc8950 5-006b, charge enabled, hiz off              : dmesg.txt:48-53
 *   IINLIM 500 mA from code 8                           : dmesg.txt:65-66
 *   ICHG 480000 uA from code 8 (60 mA LSB)              : dmesg.txt:69-70,141
 *   ITERM 270000 uA from code 4 (30 + 4*60)             : dmesg.txt:135-136
 *   VREG 4528000 uV from code 0x2b (3840 + 43*16)      : dmesg.txt:74-75,143
 *   FORCE_VINDPM code 0x14 = 4600 mV, reg 0x0d = 0x94  : dmesg.txt:59-60,146-149
 *   full register dump                                  : dmesg.txt:157-180
 *   cell limit vbat-cv = 0x10fe = 4350 mV              : fdt.dts:8378,8488
 *   HL7139A pump node exists but is NOT used here      : fdt.dts:5259-5265
 *
 * Hard boundaries of this driver:
 *  - SC8950 (0x6b) is the ONLY address writes are ever passed to. chg_write()
 *    refuses every other address. chg_read() additionally allows SC8548 (0x6f)
 *    for a single bounded, read-only presence probe; it never writes to 0x6f.
 *    The HL7139A pump (0x5f) and every other I2C address are refused.
 *  - Cell voltage never exceeds 4.35 V: VREG code 31 = 4336 mV is used,
 *    code 32 would be 4352 mV and is therefore not written.
 *  - Charge current is capped at 1500 mA (code 25 with the 60 mA LSB).
 *  - Input current stays at 500 mA (code 8) for a standard 5 V adapter.
 *  - The MT6366 gauge in hakios.x6886_battery.c stays read-only and untouched.
 *  - No panic, no unbounded loop, no write before a presence read succeeds.
 *  - Boot arg x6886_chg=1 opt-in; default is "do nothing".
 *
 * The I2C5 serial engine has no source in the local trees (only the prebuilt
 * i2c-mt65xx.ko), and no SC8950 GPIO bit-bang pins are declared for X6886.
 * No controller offsets or pin numbers are guessed: a board provider must
 * publish x6886_charger_i2c_ops. With no provider the probe logs and returns
 * ENXIO without touching a single register.
 */

#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void delay(int usec);

/* ---- ramoops pstore mirror (same contract as the battery port) ---- */
#define X6886_PSTORE_PHYS       0x480af000ULL
#define X6886_PSTORE_SIZE       0x1000u
#define X6886_PSTORE_SIG        0x43474244u
#define X6886_PSTORE_DATA       12      /* sig +4, start +4, size +4, data +12 */

static vm_offset_t x6886_pstore_va;
static LCK_GRP_DECLARE(x6886_chg_pstore_grp, "x6886-chg-pstore");
static LCK_SPIN_DECLARE(x6886_chg_pstore_lock, &x6886_chg_pstore_grp);

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
	lck_spin_lock(&x6886_chg_pstore_lock);
	base = (volatile uint8_t *)x6886_pstore_va;
	sig = *(volatile uint32_t *)(base + 0);
	start = *(volatile uint32_t *)(base + 4);
	size = *(volatile uint32_t *)(base + 8);
	if (sig == X6886_PSTORE_SIG && size >= 16 && size <= X6886_PSTORE_SIZE - X6886_PSTORE_DATA) {
		if (start >= size) {
			start = 0;
		}
		n = length < size - 2 ? length : size - 2;
		for (unsigned i = 0; i < n; i++) {
			base[X6886_PSTORE_DATA + ((start + i) % size)] = (uint8_t)message[i];
		}
		base[X6886_PSTORE_DATA + ((start + n) % size)] = '\n';
		*(volatile uint32_t *)(base + 4) = (start + n + 1) % size;
	}
	lck_spin_unlock(&x6886_chg_pstore_lock);
}

#define X6886_LOG(fmt, ...) do {                                              \
	char x6886_log_buf[256];                                              \
	int x6886_log_len = snprintf(x6886_log_buf, sizeof(x6886_log_buf),    \
	    "[X6886-charger] " fmt, ##__VA_ARGS__);                           \
	printf("%s", x6886_log_buf);                                          \
	if (x6886_log_len > 0) {                                              \
		x6886_pstore_write(x6886_log_buf, (unsigned)x6886_log_len);       \
	}                                                                     \
} while (0)

/* ---- SC8950 / SC89890H register map, every offset cited to alps ------- */
#define SC8950_ADDR             0x6bu          /* fdt.dts:5126; dmesg.txt:48 */

/* SC8548 (primary charger, i2c7) is read-only here. No register offset for it
 * exists in fdt.dts, iomem.txt or the alps tree: the only one this driver ever
 * uses comes from the x6886_sc8548_reg=N REF boot argument, range-checked. */
#define SC8548_ADDR             0x6fu          /* fdt.dts:5247 (reg = <0x6f>) */
#define X6886_I2C5_PHYS         0x11017000u    /* fdt.dts:4860; iomem.txt:41,100 */
#define X6886_I2C5_SIZE         0x1000u        /* fdt.dts:4860; iomem.txt:100 */
#define X6886_I2C7_PHYS         0x11f00000u    /* fdt.dts:5233; iomem.txt:43,113 */
#define X6886_I2C7_SIZE         0x1000u        /* fdt.dts:5233; iomem.txt:113 */

#define SC8548_PROBE_TRIES      2u             /* bounded: 2 * 50 ms */

#define REG_00                  0x00u          /* bq2589x_reg.h:21 */
#define EN_HIZ_MASK             0x80u          /* bq2589x_reg.h:22 */
#define EN_HIZ_SHIFT            7u             /* bq2589x_reg.h:23 */
#define EN_ILIM_MASK            0x40u          /* bq2589x_reg.h:26 */
#define EN_ILIM_SHIFT           6u             /* bq2589x_reg.h:27 */
#define IINLIM_MASK             0x3fu          /* bq2589x_reg.h:31 */
#define IINLIM_SHIFT            0u             /* bq2589x_reg.h:32 */
#define IINLIM_BASE             100u           /* bq2589x_reg.h:33 */
#define IINLIM_LSB              50u            /* bq2589x_reg.h:34 */

#define REG_03                  0x03u          /* bq2589x_reg.h:117 */
#define WDT_RESET_MASK          0x40u          /* bq2589x_reg.h:123 */
#define OTG_CONFIG_MASK         0x20u          /* bq2589x_reg.h:127 */
#define CHG_CONFIG_MASK         0x10u          /* bq2589x_reg.h:132 */

#define REG_04                  0x04u          /* bq2589x_reg.h:146 */
#define ICHG_MASK               0x7fu          /* bq2589x_reg.h:151 */
#define ICHG_SHIFT              0u             /* bq2589x_reg.h:152 */
#define ICHG_BASE               0u             /* bq2589x_reg.h:155 (SC89890H) */
#define ICHG_LSB                60u            /* bq2589x_reg.h:156 (SC89890H) */

#define REG_05                  0x05u          /* bq2589x_reg.h:159 */
#define ITERM_MASK              0x0fu          /* bq2589x_reg.h:162 */
#define ITERM_SHIFT             0u             /* bq2589x_reg.h:163 */
#define ITERM_BASE              30u            /* bq2589x_reg.h:170 (SC89890H) */
#define ITERM_LSB               60u            /* bq2589x_reg.h:171 (SC89890H) */

#define REG_06                  0x06u          /* bq2589x_reg.h:174 */
#define VREG_MASK               0xfcu          /* bq2589x_reg.h:175 */
#define VREG_SHIFT              2u             /* bq2589x_reg.h:176 */
#define VREG_BASE               3840u          /* bq2589x_reg.h:185 */
#define VREG_LSB                16u            /* bq2589x_reg.h:186 */

#define REG_07                  0x07u          /* bq2589x_reg.h:189 */
#define EN_TERM_MASK            0x80u          /* bq2589x_reg.h:190 */
#define WDT_MASK                0x30u          /* bq2589x_reg.h:195 */

#define REG_0B                  0x0bu          /* bq2589x_reg.h:298 */
#define VBUS_STAT_MASK          0xe0u          /* bq2589x_reg.h:299 */
#define VBUS_STAT_NONE          0u             /* bq2589x_reg.h:303 */
#define CHRG_STAT_MASK          0x18u          /* bq2589x_reg.h:310 */
#define PG_STAT_MASK            0x04u          /* bq2589x_reg.h:317 */

#define REG_0C                  0x0cu          /* bq2589x_reg.h:325 */
#define FAULT_WDT_MASK          0x80u          /* bq2589x_reg.h:327 */
#define FAULT_CHRG_MASK         0x30u          /* bq2589x_reg.h:331 */
#define FAULT_BAT_MASK          0x08u          /* bq2589x_reg.h:338 */
#define FAULT_NTC_MASK          0x07u          /* bq2589x_reg.h:340 */
#define HARD_FAULT_MASK         (FAULT_CHRG_MASK | FAULT_BAT_MASK | FAULT_NTC_MASK)

#define REG_0D                  0x0du          /* bq2589x_reg.h:347 */
#define FORCE_VINDPM_MASK       0x80u          /* bq2589x_reg.h:353 */
#define VINDPM_MASK             0x7fu          /* bq2589x_reg.h:357 */
#define VINDPM_BASE             2600u          /* bq2589x_reg.h:360 */
#define VINDPM_LSB              100u           /* bq2589x_reg.h:361 */

#define REG_11                  0x11u          /* bq2589x_reg.h:390 */
#define VBUS_GD_MASK            0x80u          /* bq2589x_reg.h:391 */

/* ---- owner caps, never raised by a boot argument ----------------------- */
#define X6886_CV_UV             4350000u       /* fdt.dts:8378,8488 (0x10fe mV) */
#define X6886_ICHG_UA           1500000u       /* hard cap, 500mA stock -> 1.5A */
#define X6886_IINLIM_UA         500000u        /* dmesg.txt:65-66: code 8 */
#define X6886_ITERM_UA          270000u        /* dmesg.txt:135-136: code 4 */
#define X6886_VINDPM_UV         4600000u       /* dmesg.txt:58-59, code 0x14 */

#define PRESENCE_TRIES          8u             /* bounded: 8 * 50 ms = 400 ms */
#define VERIFY_TRIES            4u             /* bounded post-init verification */
#define POLL_DELAY_US           50000u
#define VERIFY_DELAY_US         100000u

/* floor to a code that cannot cross the cell limit (code 32 would be 4352 mV) */
#define VREG_CODE_FOR_CV_UV (((X6886_CV_UV / 1000u) - VREG_BASE) / VREG_LSB)
#define ICHG_CODE_FOR_UA    (X6886_ICHG_UA / 1000u / ICHG_LSB)
#define IINLIM_CODE_FOR_UA  ((X6886_IINLIM_UA / 1000u - IINLIM_BASE) / IINLIM_LSB)
#define ITERM_CODE_FOR_UA   ((X6886_ITERM_UA / 1000u - ITERM_BASE) / ITERM_LSB)
#define VINDPM_CODE_FOR_UV  ((X6886_VINDPM_UV / 1000u - VINDPM_BASE) / VINDPM_LSB)

/* ---- I2C transport: published by board glue, absent by default -------- */
struct x6886_chg_i2c_ops {
	int (*read)(uint8_t addr, uint8_t reg, uint8_t *value);
	int (*write)(uint8_t addr, uint8_t reg, uint8_t value);
};

struct x6886_chg_i2c_ops *x6886_charger_i2c_ops __attribute__((weak));

static LCK_GRP_DECLARE(chg_grp, "x6886-chg");
static LCK_MTX_DECLARE(chg_lock, &chg_grp);

/* Address gate: 0x6b may be read and written; 0x6f may only be read (probe).
 * No path can reach the HL7139A pump (0x5f) or any other part. */
static int
chg_read(uint8_t addr, uint8_t reg, uint8_t *value)
{
	if ((addr != SC8950_ADDR && addr != SC8548_ADDR) ||
	    x6886_charger_i2c_ops == NULL ||
	    x6886_charger_i2c_ops->read == NULL || value == NULL) {
		return EPERM;
	}
	return x6886_charger_i2c_ops->read(addr, reg, value);
}

static int
chg_write(uint8_t addr, uint8_t reg, uint8_t value)
{
	if (addr != SC8950_ADDR || x6886_charger_i2c_ops == NULL ||
	    x6886_charger_i2c_ops->write == NULL) {
		return EPERM;
	}
	return x6886_charger_i2c_ops->write(addr, reg, value);
}

static int
chg_update(uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t old, next;
	int error;

	error = chg_read(SC8950_ADDR, reg, &old);
	if (error != 0) {
		return error;
	}
	next = (uint8_t)((old & (uint8_t)~mask) | (value & mask));
	if (next == old) {
		return 0;
	}
	return chg_write(SC8950_ADDR, reg, next);
}

static void
x6886_chg_log_regs(const char *tag, uint8_t r00, uint8_t r03, uint8_t r04,
    uint8_t r05, uint8_t r06, uint8_t r07, uint8_t r0b, uint8_t r0c,
    uint8_t r0d, uint8_t r11)
{
	X6886_LOG("%s reg00=%#x reg03=%#x reg04=%#x reg05=%#x reg06=%#x "
	    "reg07=%#x reg0b=%#x reg0c=%#x reg0d=%#x reg11=%#x\n",
	    tag, r00, r03, r04, r05, r06, r07, r0b, r0c, r0d, r11);
}

static int
x6886_chg_read_status(uint8_t *r0b, uint8_t *r0c, uint8_t *r0d, uint8_t *r11)
{
	int error;

	error = chg_read(SC8950_ADDR, REG_0B, r0b);
	if (error != 0) {
		return error;
	}
	error = chg_read(SC8950_ADDR, REG_0C, r0c);
	if (error != 0) {
		return error;
	}
	error = chg_read(SC8950_ADDR, REG_0D, r0d);
	if (error != 0) {
		return error;
	}
	return chg_read(SC8950_ADDR, REG_11, r11);
}

/* presence FIRST: no write happens until VBUS is reported by the part itself */
static int
x6886_chg_wait_present(uint8_t *r0b, uint8_t *r11)
{
	uint8_t fault = 0, vindpm = 0;

	for (uint32_t try = 1; try <= PRESENCE_TRIES; try++) {
		int error = x6886_chg_read_status(r0b, &fault, &vindpm, r11);

		if (error != 0) {
			X6886_LOG("presence read %u/%u failed: %d; no register is written\n",
			    try, PRESENCE_TRIES, error);
			return error;
		}
		if (((*r0b & (VBUS_STAT_MASK | PG_STAT_MASK)) != 0) ||
		    ((*r11 & VBUS_GD_MASK) != 0)) {
			X6886_LOG("charger present on try %u: reg0b=%#x reg11=%#x\n",
			    try, *r0b, *r11);
			return 0;
		}
		if (try == 1) {
			X6886_LOG("no VBUS yet: reg0b=%#x reg11=%#x; polling at most %u tries\n",
			    *r0b, *r11, PRESENCE_TRIES);
		}
		delay(POLL_DELAY_US);
	}
	X6886_LOG("no charger after %u tries (reg0b=%#x reg11=%#x); charging stays off\n",
	    PRESENCE_TRIES, *r0b, *r11);
	return ENXIO;
}

static int
x6886_chg_safe_init(void)
{
	int error;
	uint8_t r00 = 0, r03 = 0, r04 = 0, r05 = 0, r06 = 0, r07 = 0;
	uint8_t r0b = 0, r0c = 0, r0d = 0, r11 = 0;
	const uint8_t vreg_code = (uint8_t)VREG_CODE_FOR_CV_UV;
	const uint8_t ichg_code = (uint8_t)ICHG_CODE_FOR_UA;
	const uint8_t iinlim_code = (uint8_t)IINLIM_CODE_FOR_UA;
	const uint8_t iterm_code = (uint8_t)ITERM_CODE_FOR_UA;
	const uint8_t vindpm_code = (uint8_t)VINDPM_CODE_FOR_UV;

	/* 1. presence, before any write */
	error = x6886_chg_wait_present(&r0b, &r11);
	if (error != 0) {
		return error;
	}

	/* 2. snapshot + hard fault gate, all reads */
	error = x6886_chg_read_status(&r0b, &r0c, &r0d, &r11);
	if (error != 0) {
		X6886_LOG("status read after presence failed: %d; no register is written\n", error);
		return error;
	}
	(void)chg_read(SC8950_ADDR, REG_00, &r00);
	(void)chg_read(SC8950_ADDR, REG_03, &r03);
	(void)chg_read(SC8950_ADDR, REG_04, &r04);
	(void)chg_read(SC8950_ADDR, REG_05, &r05);
	(void)chg_read(SC8950_ADDR, REG_06, &r06);
	(void)chg_read(SC8950_ADDR, REG_07, &r07);
	x6886_chg_log_regs("before", r00, r03, r04, r05, r06, r07, r0b, r0c, r0d, r11);
	if ((r0c & HARD_FAULT_MASK) != 0) {
		X6886_LOG("fault reg0c=%#x (charge/bat/NTC); refusing to enable charging\n", r0c);
		return EIO;
	}

	/* 3. safe init. All values are floors inside the declared caps. */
	/* REG_00: HIZ off, ILIM enabled, input 500 mA (code 8) - dmesg.txt:48-49,65-66 */
	error = chg_update(REG_00, (uint8_t)(EN_HIZ_MASK | EN_ILIM_MASK | IINLIM_MASK),
	    (uint8_t)((1u << EN_ILIM_SHIFT) | (iinlim_code << IINLIM_SHIFT)));
	if (error != 0) {
		goto disable;
	}
	/* REG_06: CV floor 4336 mV, never 4352 mV - bq2589x_reg.h:185-186 */
	error = chg_update(REG_06, VREG_MASK, (uint8_t)(vreg_code << VREG_SHIFT));
	if (error != 0) {
		goto disable;
	}
	/* REG_04: 1500 mA max (code 25 * 60 mA) - bq2589x_reg.h:155-156 */
	error = chg_update(REG_04, ICHG_MASK, (uint8_t)(ichg_code << ICHG_SHIFT));
	if (error != 0) {
		goto disable;
	}
	/* REG_05: termination 270 mA (30 + 4*60) - bq2589x_reg.h:170-171 */
	error = chg_update(REG_05, ITERM_MASK, (uint8_t)(iterm_code << ITERM_SHIFT));
	if (error != 0) {
		goto disable;
	}
	/* REG_0D: forced VINDPM 4600 mV, code 0x14 - dmesg.txt:59-60 */
	error = chg_update(REG_0D, (uint8_t)(FORCE_VINDPM_MASK | VINDPM_MASK),
	    (uint8_t)(FORCE_VINDPM_MASK | vindpm_code));
	if (error != 0) {
		goto disable;
	}
	/* REG_07: termination on, watchdog off (no timeout reboot path) */
	error = chg_update(REG_07, (uint8_t)(EN_TERM_MASK | WDT_MASK), EN_TERM_MASK);
	if (error != 0) {
		goto disable;
	}
	/* REG_03: charge on, boost/OTG off. This is the only enable write. */
	error = chg_update(REG_03, (uint8_t)(CHG_CONFIG_MASK | OTG_CONFIG_MASK | WDT_RESET_MASK),
	    CHG_CONFIG_MASK);
	if (error != 0) {
		goto disable;
	}
	/* REG_03 bit6: one watchdog kick after all limits are in place */
	error = chg_update(REG_03, WDT_RESET_MASK, WDT_RESET_MASK);
	if (error != 0) {
		goto disable;
	}
	X6886_LOG("safe init: CV code %u (%u mV), ICHG code %u (%u mA), "
	    "IINLIM code %u (%u mA), ITERM code %u (%u mA), VINDPM code %u (%u mV)\n",
	    (unsigned)vreg_code, VREG_BASE + vreg_code * VREG_LSB,
	    (unsigned)ichg_code, ichg_code * ICHG_LSB,
	    (unsigned)iinlim_code, IINLIM_BASE + iinlim_code * IINLIM_LSB,
	    (unsigned)iterm_code, ITERM_BASE + iterm_code * ITERM_LSB,
	    (unsigned)vindpm_code, VINDPM_BASE + vindpm_code * VINDPM_LSB);

	/* 4. bounded verification; a new hard fault turns charging back off */
	for (uint32_t try = 1; try <= VERIFY_TRIES; try++) {
		error = x6886_chg_read_status(&r0b, &r0c, &r0d, &r11);
		if (error != 0) {
			X6886_LOG("verify %u/%u failed: %d\n", try, VERIFY_TRIES, error);
			goto disable;
		}
		if ((r0c & HARD_FAULT_MASK) != 0) {
			X6886_LOG("verify %u/%u fault reg0c=%#x; disabling charge\n",
			    try, VERIFY_TRIES, r0c);
			error = EIO;
			goto disable;
		}
		delay(VERIFY_DELAY_US);
	}
	(void)chg_read(SC8950_ADDR, REG_00, &r00);
	(void)chg_read(SC8950_ADDR, REG_03, &r03);
	(void)chg_read(SC8950_ADDR, REG_04, &r04);
	(void)chg_read(SC8950_ADDR, REG_05, &r05);
	(void)chg_read(SC8950_ADDR, REG_06, &r06);
	(void)chg_read(SC8950_ADDR, REG_07, &r07);
	x6886_chg_log_regs("after", r00, r03, r04, r05, r06, r07, r0b, r0c, r0d, r11);
	X6886_LOG("SC8950 only, standard 5 V; HL7139A pump is not enabled\n");
	return 0;

disable:
	(void)chg_update(REG_03, (uint8_t)(CHG_CONFIG_MASK | OTG_CONFIG_MASK), 0);
	X6886_LOG("safe init aborted: %d; charge enable was not left on\n", error);
	return error;
}

/* SC8548 is the primary charger on i2c7 (fdt.dts:5245-5247). This driver has no
 * SC8548 register definitions - neither the dump nor the alps tree carries them -
 * so the one register offset it may read is an explicit REF boot argument. There
 * is no SC8548 write path: the pump and primary charger stay untouched.
 */
static int
x6886_chg_sc8548_probe(uint8_t *value)
{
	uint32_t reg = 0;
	int error = ENODEV;

	if (!PE_parse_boot_argn("x6886_sc8548_reg", &reg, sizeof(reg)) || reg > 0xffu) {
		X6886_LOG("SC8548 7-0x%02x on i2c7 %#x+%#x: set x6886_sc8548_reg=0..255 "
		    "(REF offset) to probe; no write path exists\n", SC8548_ADDR,
		    X6886_I2C7_PHYS, X6886_I2C7_SIZE);
		return ENODEV;
	}
	for (uint32_t try = 1; try <= SC8548_PROBE_TRIES; try++) {
		error = chg_read(SC8548_ADDR, (uint8_t)reg, value);
		if (error == 0) {
			X6886_LOG("SC8548 7-0x%02x present on try %u: REF reg %u = %#x "
			    "(read-only)\n", SC8548_ADDR, try, (unsigned)reg, *value);
			return 0;
		}
		delay(POLL_DELAY_US);
	}
	X6886_LOG("SC8548 7-0x%02x probe failed after %u tries: %d; SC8950 path unchanged\n",
	    SC8548_ADDR, SC8548_PROBE_TRIES, error);
	return error;
}

int
x6886_charger_init(void)
{
	uint32_t on = 0;
	uint8_t sc8548 = 0;
	int error;

	if (!PE_parse_boot_argn("x6886_chg", &on, sizeof(on)) || on == 0) {
		return 0;                       /* default: touch nothing */
	}
	x6886_pstore_map();
	X6886_LOG("SC8950 on i2c5 %#x+%#x-0x%02x, caps %u mV / %u mA; HL7139A disabled\n",
	    X6886_I2C5_PHYS, X6886_I2C5_SIZE, SC8950_ADDR,
	    X6886_CV_UV / 1000u, X6886_ICHG_UA / 1000u);

	if (x6886_charger_i2c_ops == NULL || x6886_charger_i2c_ops->read == NULL ||
	    x6886_charger_i2c_ops->write == NULL) {
		X6886_LOG("no I2C5 provider for 0x%02x; no register is read or written\n",
		    SC8950_ADDR);
		return ENXIO;
	}
	/* Bounded, read-only SC8548 probe. Its result is diagnostic only: this
	 * driver never writes the primary charger or the pump. */
	(void)x6886_chg_sc8548_probe(&sc8548);
	lck_mtx_lock(&chg_lock);
	error = x6886_chg_safe_init();
	lck_mtx_unlock(&chg_lock);
	return error;
}
