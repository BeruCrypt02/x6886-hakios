/*
 * X6886 (MT6789) buttons: the SoC keypad controller and the MT6366 PMIC keys.
 *
 * Source of the logic: refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_keys.c
 *   keypad bit-per-key and two-look debounce  : mtk_keys.c:10-12, 28, 355-372
 *   PMIC wrapper WACS2 transaction            : mtk_keys.c:30-36, 63-99
 * Nothing of the reference's CPU/GPU voltage code is kept: those register offsets are MT6358
 * values for another board and writing them here would be a guess.
 *
 * Physical constants are from the device's own dump, not from the reference board:
 *   kp@10010000, reg 0x10010000/0x1000, hw-init-map 0x73/0x72 : fdt.dts:4002-4006
 *   pwrap@10026000, reg 0x10026000/0x1000                     : fdt.dts:3219-3220
 *   mt6366keys / power linux keycode 0x74                     : fdt.dts:3356-3362
 *
 * Opt-in: boot-arg x6886_keys=1; x6886_pwrkey=0 opts out of the PMIC power key.
 * MT6366 TOPSTATUS 0x28 / PWRKEY debounce mask 0x1 (bit 0, active low) are proven from the
 * device-family primary source refs/alps-device-modules-6.12:
 *   TOPSTATUS + mask and mt6366_regs wiring : drivers/input/keyboard/mtk-pmic-keys.c:44,47,170-175
 *   of_match mediatek,mt6366-keys           : mtk-pmic-keys.c:681-682
 *   active-low debounce read                : mtk-pmic-keys.c:464-470
 *   node for this SoC                       : arch/arm64/boot/dts/mediatek/mt6366_mt6789.dtsi:123-124
 * Bounded polling, no panic, clean error return on any failed map or pwrap transaction.
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
static LCK_GRP_DECLARE(x6886_pstore_grp, "x6886-keys-pstore");
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
	    "[X6886-keys] " fmt, ##__VA_ARGS__);                              \
	printf("%s", x6886_log_buf);                                          \
	if (x6886_log_len > 0) {                                              \
		x6886_pstore_write(x6886_log_buf, (unsigned)x6886_log_len);       \
	}                                                                     \
} while (0)

/* ---- device constants --------------------------------------------------- */
#define X6886_KPD_PHYS          0x10010000u     /* fdt.dts:4002-4004 */
#define X6886_KPD_SIZE          0x1000u
#define KP_MEM1                 0x0004u         /* mtk_keys.c:28 */
#define KP_VOL_UP_BIT           0u              /* map[0] 0x73: fdt.dts:4006, mtk_keys.c:7 */
#define KP_VOL_DOWN_BIT         1u              /* map[1] 0x72: fdt.dts:4006, mtk_keys.c:8 */

#define X6886_PWRAP_PHYS        0x10026000u     /* fdt.dts:3219-3220 */
#define X6886_PWRAP_SIZE        0x1000u
#define PWRAP_WACS2_CMD         0xc20u          /* mtk_keys.c:31 */
#define PWRAP_WACS2_RDATA       0xc24u          /* mtk_keys.c:32 */
#define PWRAP_WACS2_VLDCLR      0xc28u          /* mtk_keys.c:33 */
#define PWRAP_FSM(r)            (((r) >> 16) & 7u)      /* mtk_keys.c:34 */
#define PWRAP_FSM_IDLE          0u              /* mtk_keys.c:35 */
#define PWRAP_FSM_WFVLDCLR      6u              /* mtk_keys.c:36 */
#define PWRAP_SPINS             100000u         /* mtk_keys.c:66: bounded wait */
#define MT6366_TOPSTATUS        0x28u           /* mtk-pmic-keys.c:44 (MT6366_TOPSTATUS) */
#define PWRKEY_DEB              0x1u            /* mtk-pmic-keys.c:47,469-470: bit 0, active low */

#define X6886_POLL_US           20000u          /* mtk_keys.c:12,376: 20 ms */
#define X6886_KEYS_TICKS        0x00ffffffu     /* hard bound on the polling thread */

static volatile uint32_t *kpd, *pwrap;
static LCK_GRP_DECLARE(keys_grp, "x6886-keys");
static LCK_MTX_DECLARE(keys_pwrap_lock, &keys_grp);
static bool pwrkey_enabled;

static inline uint32_t
rd(volatile uint32_t *base, uint32_t at)
{
	return base[at / 4];
}

static inline void
wr(volatile uint32_t *base, uint32_t at, uint32_t value)
{
	base[at / 4] = value;
}

static bool
pwrap_wait(uint32_t state)
{
	for (uint32_t spin = 0; spin < PWRAP_SPINS; spin++) {
		if (PWRAP_FSM(rd(pwrap, PWRAP_WACS2_RDATA)) == state) {
			return true;
		}
	}
	X6886_LOG("pwrap FSM %u never reached in %u spins (RDATA %#x); leaving the register alone\n",
	    state, PWRAP_SPINS, rd(pwrap, PWRAP_WACS2_RDATA));
	return false;
}

static bool
pmic_read(uint32_t reg, uint32_t *value)
{
	bool ok;

	lck_mtx_lock(&keys_pwrap_lock);
	ok = pwrap_wait(PWRAP_FSM_IDLE);
	if (ok) {
		wr(pwrap, PWRAP_WACS2_CMD, (reg >> 1) << 16);
		ok = pwrap_wait(PWRAP_FSM_WFVLDCLR);
		if (ok) {
			*value = rd(pwrap, PWRAP_WACS2_RDATA) & 0xffffu;
			wr(pwrap, PWRAP_WACS2_VLDCLR, 1);
		}
	}
	lck_mtx_unlock(&keys_pwrap_lock);
	return ok;
}

struct key {
	const char *name;
	uint32_t button;
	bool down, seen;
};

static struct key keys[3] = {
	{ "volume up", HAKIOS_BUTTON_VOLUME_UP, false, false },
	{ "volume down", HAKIOS_BUTTON_VOLUME_DOWN, false, false },
	{ "power", HAKIOS_BUTTON_LOCK, false, false },
};

static void
keys_thread(__unused void *parameter, __unused wait_result_t result)
{
	uint32_t caps = 0, log = 0, presses = 0;

	(void)PE_parse_boot_argn("x6886_keys_ticks", &caps, sizeof(caps));
	if (caps == 0 || caps > X6886_KEYS_TICKS) {
		caps = X6886_KEYS_TICKS;
	}
	(void)PE_parse_boot_argn("keylog", &log, sizeof(log));
	X6886_LOG("keypad %#x, pwrap %s, MT6366 power key %s; %u ticks\n",
	    rd(kpd, KP_MEM1), pwrap_wait(PWRAP_FSM_IDLE) ? "ready" : "NOT READY",
	    pwrkey_enabled ? "enabled (verified alps MT6366)" : "disabled", caps);

	for (unsigned tick = 1; tick <= caps; tick++) {
		uint32_t mem = rd(kpd, KP_MEM1), top = PWRKEY_DEB;
		bool now[3];

		if (pwrkey_enabled && !pmic_read(MT6366_TOPSTATUS, &top)) {
			top = PWRKEY_DEB;       /* a failed read is "not pressed" */
		}
		now[0] = !(mem & (1u << KP_VOL_UP_BIT));
		now[1] = !(mem & (1u << KP_VOL_DOWN_BIT));
		now[2] = pwrkey_enabled ? !(top & PWRKEY_DEB) : false;

		for (unsigned i = 0; i < 3; i++) {
			struct key *k = &keys[i];

			if (now[i] == k->seen && now[i] != k->down) {
				k->down = now[i];
				hakios_touch_post(k->down ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP, k->button, 0, 0);
				presses += k->down;
				if (log) {
					X6886_LOG("%s %s (keypad %#x, PMIC status %#x)\n", k->name,
					    k->down ? "down" : "up", mem, top);
				}
			}
			k->seen = now[i];
		}
		if (tick % 3000u == 0) {
			X6886_LOG("%u presses so far; keypad %#x, PMIC status %#x\n", presses, mem, top);
		}
		delay(X6886_POLL_US);
	}
	X6886_LOG("polling bound reached (%u ticks); thread stops\n", caps);
}

int
x6886_keys_init(void)
{
	uint32_t on = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("x6886_keys", &on, sizeof(on)) || on == 0) {
		return 0;
	}
	x6886_pstore_map();
	kpd = (volatile uint32_t *)ml_io_map(X6886_KPD_PHYS, X6886_KPD_SIZE);
	pwrap = (volatile uint32_t *)ml_io_map(X6886_PWRAP_PHYS, X6886_PWRAP_SIZE);
	if (kpd == NULL || pwrap == NULL) {
		X6886_LOG("cannot map keypad %#x / pwrap %#x; no buttons\n", X6886_KPD_PHYS, X6886_PWRAP_PHYS);
		return ENXIO;
	}
	uint32_t pwrkey_off = 0;

	(void)PE_parse_boot_argn("x6886_pwrkey", &pwrkey_off, sizeof(pwrkey_off));
	pwrkey_enabled = pwrkey_off == 0;       /* on by default; x6886_pwrkey=0 opts out */
	if (pwrkey_enabled) {
		X6886_LOG("MT6366 power key enabled: TOPSTATUS %#x, debounce mask %#x (active low)\n",
		    MT6366_TOPSTATUS, PWRKEY_DEB);
	}
	if (kernel_thread_start(keys_thread, NULL, &thread) != KERN_SUCCESS) {
		X6886_LOG("cannot start the 20 ms polling thread; no buttons\n");
		return ENOMEM;
	}
	thread_deallocate(thread);
	return 0;
}
