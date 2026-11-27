/*
 * X6886 (MT6789) battery monitor: READ-ONLY.
 *
 * Source of the logic: refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_battery.c
 *   PMIC WACS2 read transaction and bounded wait : mtk_battery.c:42-46, 82-109
 *   RTC_AL_MTH bits 14:8 hold Android's percent  : mtk_battery.c:48, 230-231
 *   BATADC count scale and voltage sanity        : mtk_battery.c:59-62, 255-259
 * No charging or gauge-control register is written here: the reference's FGADC latch and AUXADC
 * request writes (mtk_battery.c:145, 174-180) are deliberately NOT ported.
 *
 * Device proof (fdt.dts from the device's own dump):
 *   pwrap@10026000 / mt6366 / mtk-gauge          : fdt.dts:3219-3220, 3226-3227, 3868-3870
 *   r-fg-value = <0x32> = 50 mOhm                : fdt.dts:3883
 *   fg-meter-resistance = <0x64>                 : fdt.dts:3887
 *   BATADC channel name "pmic_battery_voltage"   : fdt.dts:3874-3875
 *   vbat-cv = <0x10fe> = 4350 mV (4.35 V limit)  : fdt.dts:8378, 8488
 *
 * Register offsets inside the PMIC (RTC_AL_MTH 0x5aa, CHR_CON0 0xa88, AUXADC_ADC0 0x1088)
 * are NOT proven by the X6886 device-dump: fdt.dts:3868-3883 proves the gauge node, the
 * r-fg-value and the channel names, but carries no register numbers. They come only from
 * mtk_battery.c:48-60 and need owner sign-off. All PMIC reads are behind the separate,
 * default-OFF opt-in x6886_batt_pmic=1; without it no register is read and no thread starts.
 *
 * Opt-in: x6886_batt=1 and x6886_batt_pmic=1. Bounded polling, no panic, no writes.
 */

#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/board/input.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void delay(int usec);

/* ---- ramoops pstore mirror (boot-audit GATE 4) ---- */
#define X6886_PSTORE_PHYS       0x480af000ULL
#define X6886_PSTORE_SIZE       0x1000u
#define X6886_PSTORE_SIG        0x43474244u

static vm_offset_t x6886_pstore_va;
static LCK_GRP_DECLARE(x6886_pstore_grp, "x6886-batt-pstore");
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
	    "[X6886-battery] " fmt, ##__VA_ARGS__);                           \
	printf("%s", x6886_log_buf);                                          \
	if (x6886_log_len > 0) {                                              \
		x6886_pstore_write(x6886_log_buf, (unsigned)x6886_log_len);       \
	}                                                                     \
} while (0)

/* ---- device constants --------------------------------------------------- */
#define X6886_PWRAP_PHYS        0x10026000u     /* fdt.dts:3219-3220 */
#define X6886_PWRAP_SIZE        0x1000u
#define PWRAP_WACS2_CMD         0xc20u          /* mtk_battery.c:43 */
#define PWRAP_WACS2_RDATA       0xc24u          /* mtk_battery.c:44 */
#define PWRAP_WACS2_VLDCLR      0xc28u          /* mtk_battery.c:45 */
#define PWRAP_FSM(r)            (((r) >> 16) & 7u)      /* mtk_battery.c:46 */
#define PWRAP_SPINS             200000u         /* mtk_battery.c:85 */

#define RTC_AL_MTH_SOC          0x05aau         /* mtk_battery.c:48; bits 14:8 */
#define CHR_CON0                0x0a88u         /* mtk_battery.c:49 */
#define CHR_CON0_CHRDET         (1u << 5)       /* mtk_battery.c:50 */
#define AUXADC_ADC0             0x1088u         /* mtk_battery.c:59 */
#define AUXADC_READY            (1u << 15)      /* mtk_battery.c:19-20 */

/* REF-only gauge-latch registers/scale. None of these appear in the X6886 dump;
 * sources: mtk_battery.c:47,51-54,60 and (corroboration only)
 * alps.../drivers/power/supply/mt6358-gauge.c:51,66,69,140. */
#define MT6358_FGADC_CON1       0x0d0au
#define FGADC_CON1_READ         (1u << 0)
#define FGADC_CON1_CLEAR        (1u << 3)
#define FGADC_CON1_LATCHED      (1u << 15)
#define MT6358_FGADC_CUR_CON0   0x0d8au
#define MT6358_FGADC_CAR_CON0   0x0d14u
#define MT6358_FGADC_CAR_CON1   0x0d16u
#define MT6358_AUXADC_RQST0     0x1108u
#define FG_COUNT_NA             762939ULL      /* REF mtk_battery.c:62: 250 mV/65536 over 5 mOhm */
#define BATADC_CONVERT_SPINS    100u            /* REF mtk_battery.c:148/175 wait counts */

#define COUNT_NV                164795ULL      /* mtk_battery.c:62: 1.8 V * 3 / 32768, nV per count */
#define X6886_FULL_MV           4350u           /* fdt.dts:8378/8488: 0x10fe = 4.35 V */
#define X6886_MIN_MV            2500u           /* mtk_battery.c:256: sanity floor */
#define X6886_MAX_MV            X6886_FULL_MV   /* never report above the cell limit */
#define X6886_R_FG_MOHM         50u             /* fdt.dts:3883: r-fg-value <0x32> */
#define X6886_TICKS             0x00ffffffu     /* hard bound on the polling thread */

static volatile uint32_t *pwrap;
static bool batt_pmic_optin;
static bool batt_fg_optin;
static LCK_GRP_DECLARE(batt_grp, "x6886-batt");
static LCK_MTX_DECLARE(batt_pwrap_lock, &batt_grp);

static bool
pwrap_wait(uint32_t state)
{
	for (uint32_t spin = 0; spin < PWRAP_SPINS; spin++) {
		if (PWRAP_FSM(pwrap[PWRAP_WACS2_RDATA / 4]) == state) {
			return true;
		}
	}
	X6886_LOG("pwrap FSM %u never reached in %u spins (RDATA %#x); no value is reported\n",
	    state, PWRAP_SPINS, pwrap[PWRAP_WACS2_RDATA / 4]);
	return false;
}

/* read only: the reference's pmic_write is not ported */
static bool
pmic_read(uint32_t reg, uint16_t *value)
{
	bool ok;

	lck_mtx_lock(&batt_pwrap_lock);
	ok = pwrap_wait(0);
	if (ok) {
		pwrap[PWRAP_WACS2_CMD / 4] = (reg >> 1) << 16;
		ok = pwrap_wait(6);
		if (ok) {
			*value = (uint16_t)pwrap[PWRAP_WACS2_RDATA / 4];
			pwrap[PWRAP_WACS2_VLDCLR / 4] = 1;
		}
	}
	lck_mtx_unlock(&batt_pwrap_lock);
	return ok;
}

/* REF-only PMIC write, same WACS2 transaction/FSM bound as pmic_read(). The
 * command encoding is from mtk_camera.c:165 (the camera and battery share the
 * pwrap). Only called from the x6886_batt_fg=1 path. */
static bool
pmic_write(uint32_t reg, uint16_t value)
{
	bool ok;

	lck_mtx_lock(&batt_pwrap_lock);
	ok = pwrap_wait(0);
	if (ok) {
		pwrap[PWRAP_WACS2_CMD / 4] = (1u << 31) | ((reg >> 1) << 16) | value;
		ok = pwrap_wait(6);
	}
	lck_mtx_unlock(&batt_pwrap_lock);
	return ok;
}

/* Latch FGADC once to obtain current and the coulomb counter, transcribed from
 * mtk_battery.c:163-194. Every wait is bounded; the saved CON1 value is always
 * restored, even on failure. Returns false (no fabricated value) on any miss. */
static bool
gauge_latch_ref(int32_t *current_ua, uint32_t *charge)
{
	uint16_t con1 = 0, got = 0, low = 0, high = 0, cur = 0;
	bool ok;

	if (!pmic_read(MT6358_FGADC_CON1, &con1)) {
		return false;
	}
	con1 &= (uint16_t)~(FGADC_CON1_READ | FGADC_CON1_CLEAR | FGADC_CON1_LATCHED);
	ok = pmic_write(MT6358_FGADC_CON1, (uint16_t)(con1 | FGADC_CON1_READ));
	for (unsigned spin = 0; ok && spin < BATADC_CONVERT_SPINS; spin++) {
		if (!pmic_read(MT6358_FGADC_CON1, &got)) {
			ok = false;
			break;
		}
		if ((got & FGADC_CON1_LATCHED) != 0) {
			break;
		}
		delay(1000);
	}
	if (ok && (got & FGADC_CON1_LATCHED) == 0) {
		ok = false;
	}
	if (ok) {
		ok = pmic_read(MT6358_FGADC_CUR_CON0, &cur) &&
		    pmic_read(MT6358_FGADC_CAR_CON0, &low) &&
		    pmic_read(MT6358_FGADC_CAR_CON1, &high);
	}
	(void)pmic_write(MT6358_FGADC_CON1, (uint16_t)(con1 | FGADC_CON1_CLEAR));
	(void)pmic_write(MT6358_FGADC_CON1, con1);
	if (!ok) {
		return false;
	}
	*current_ua = (int32_t)(((int64_t)(int16_t)cur * (int64_t)FG_COUNT_NA) / 1000LL);
	*charge = ((uint32_t)high << 16) | low;
	return true;
}

/* Ask for a fresh BATADC conversion, then read ADC0 (REF mtk_battery.c:138-157).
 * Bounded: BATADC_CONVERT_SPINS reads at 1 ms each. */
static bool
batadc_request_ref(uint16_t *adc)
{
	if (!pmic_write(MT6358_AUXADC_RQST0, 1)) {
		return false;
	}
	delay(3000);                    /* REF mtk_battery.c:147: let the first result clear */
	for (unsigned spin = 0; spin < BATADC_CONVERT_SPINS; spin++) {
		if (!pmic_read(AUXADC_ADC0, adc)) {
			return false;
		}
		if ((*adc & AUXADC_READY) != 0) {
			return true;
		}
		delay(1000);
	}
	return false;
}

static void
batt_thread(__unused void *arg, __unused wait_result_t wr)
{
	uint32_t caps = 0;
	uint16_t rtc = 0, adc = 0, chr = 0;
	int32_t percent_hundredths = -1, mv = 0, current_ua = 0;
	uint32_t ticks = 0;
	static bool said_fg;

	(void)PE_parse_boot_argn("x6886_batt_ticks", &caps, sizeof(caps));
	if (caps == 0 || caps > X6886_TICKS) {
		caps = X6886_TICKS;
	}
	X6886_LOG("MT6366 gauge (r-fg %u mOhm), cell limit %u mV; read-only, %u ticks\n",
	    X6886_R_FG_MOHM, X6886_FULL_MV, caps);

	for (ticks = 1; ticks <= caps; ticks++) {
		struct hakios_battery_state b = { .size = sizeof(b) };
		bool have_soc = batt_pmic_optin && pmic_read(RTC_AL_MTH_SOC, &rtc) && ((rtc >> 8) & 0x7f) >= 1 &&
		    ((rtc >> 8) & 0x7f) <= 100;
		bool have_adc = batt_pmic_optin && pmic_read(AUXADC_ADC0, &adc) && (adc & AUXADC_READY) != 0;
		bool charger = batt_pmic_optin && pmic_read(CHR_CON0, &chr) && (chr & CHR_CON0_CHRDET) != 0;
		uint32_t charge = 0;

		current_ua = 0;                 /* never publish a stale latch on a later miss */
		/* REF-only: latch the gauge for a real current/coulomb reading and
		 * request a fresh BATADC conversion. Both are bounded and restore
		 * the PMIC state; failure leaves the read-only values in place. */
		if (batt_fg_optin) {
			if (gauge_latch_ref(&current_ua, &charge)) {
				if (!said_fg || ticks % 300u == 0) {
					X6886_LOG("FGADC latched: %d uA, charge count %#x (REF offsets)\n",
					    current_ua, charge);
					said_fg = true;
				}
			} else if (!said_fg) {
				said_fg = true;
				X6886_LOG("FGADC did not latch; current remains 0 (no fabricated value)\n");
			}
			if (batadc_request_ref(&adc)) {
				have_adc = true;
			}
		}

		if (!have_adc && !have_soc) {
			if (ticks == 1) {
				X6886_LOG("PMIC answers neither SOC nor BATADC; no state is published\n");
			}
			delay(1000000);
			continue;
		}
		if (have_adc) {
			uint64_t nv = (uint64_t)(adc & 0x7fffu) * COUNT_NV / 1000000ULL;

			mv = (int32_t)(nv / 1000ULL);
			if (mv < X6886_MIN_MV || mv > 4700) {
				X6886_LOG("BATADC %#x would be %d mV: not a cell voltage; SOC only\n", adc, mv);
				mv = 0;
			} else if (mv > (int32_t)X6886_MAX_MV) {
				X6886_LOG("BATADC %d mV above the %u mV cell limit; clamped\n", mv, X6886_MAX_MV);
				mv = (int32_t)X6886_MAX_MV;
			}
		}
		percent_hundredths = have_soc ? (int32_t)((rtc >> 8) & 0x7fu) * 100 : -1;
		b.percent_hundredths = percent_hundredths;
		b.voltage_uv = mv * 1000;
		b.current_ua = current_ua;      /* latched FGADC when x6886_batt_fg=1, else 0 */
		b.temperature_hundredths = 2500;        /* mtk_battery.c:294: not read */
		b.present = 1;
		b.external = charger;
		b.status = !charger ? 2 : (percent_hundredths >= 9950 ? 4 : 3);
		hakios_battery_report(&b);
		if (ticks == 1 || ticks % 300u == 0) {
			X6886_LOG("%d.%02d%% (SOC %s), %d mV (ADC %#x, %s), charger %s (CHR_CON0 %#x)\n",
			    b.percent_hundredths / 100, b.percent_hundredths % 100, have_soc ? "PMIC" : "voltage only",
			    mv, adc, have_adc ? "ready" : "stale", charger ? "in" : "out", chr);
		}
		delay(1000000);
	}
	X6886_LOG("monitoring bound reached (%u ticks); thread stops\n", caps);
}

int
x6886_battery_init(void)
{
	uint32_t on = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("x6886_batt", &on, sizeof(on)) || on == 0) {
		return 0;
	}
	x6886_pstore_map();
	{
		uint32_t pmic = 0;

		(void)PE_parse_boot_argn("x6886_batt_pmic", &pmic, sizeof(pmic));
		batt_pmic_optin = pmic != 0;
	}
	if (!batt_pmic_optin) {
		X6886_LOG("PMIC reads skipped: offsets 0x5aa/0xa88/0x1088 are not DTB-proven; "
		    "use x6886_batt_pmic=1 only after owner sign-off\n");
		return ENOTSUP;         /* no hardware work: do not report success */
	}
	{
		uint32_t fg = 0;

		(void)PE_parse_boot_argn("x6886_batt_fg", &fg, sizeof(fg));
		batt_fg_optin = fg != 0;
		if (batt_fg_optin) {
			X6886_LOG("REF gauge latch enabled (x6886_batt_fg=1); offsets not DTB-proven\n");
		}
	}
	pwrap = (volatile uint32_t *)ml_io_map(X6886_PWRAP_PHYS, X6886_PWRAP_SIZE);
	if (pwrap == NULL) {
		X6886_LOG("cannot map pwrap %#x; no battery state\n", X6886_PWRAP_PHYS);
		return ENXIO;
	}
	if (!pwrap_wait(0)) {
		X6886_LOG("pwrap is not idle at start; no register is touched\n");
		return EBUSY;
	}
	if (kernel_thread_start(batt_thread, NULL, &thread) != KERN_SUCCESS) {
		X6886_LOG("cannot start the battery thread; no state is published\n");
		return ENOMEM;
	}
	thread_deallocate(thread);
	return 0;
}
