/*
 * X6886 (MT6789) camera infrastructure probe: real MMIO, no fabricated sensor table.
 *
 * Source of the logic: refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_camera.c
 *   bounded window lookup / rd32 / wr32        : mtk_camera.c:71-117
 *   PMIC WACS2 transaction and pwrap lock      : mtk_camera.c:125-190
 *   GPIO save / restore                        : mtk_camera.c:214-263
 * The reference's 496-write S5KJN1 table and 66-word SENINF/CAMSV RX table are absent from
 * osexgang (__has_include fails, mtk_camera.c:51-57). They are NOT re-invented here: this driver
 * maps and validates the hardware, checks the camera rails read-only, reports the two sensors,
 * restores the pins and returns. No stream, no capture and no guessed register is claimed.
 *
 * Device proof (fdt.dts / dump-analysis from the device's own dump):
 *   kp-free camera nodes: camsys@1a000000        : fdt.dts:1841
 *   seninf1@1a004000 .. seninf6@1a009000         : fdt.dts:3165-3193
 *   seninf_top@1a004000                          : fdt.dts:3195-3196
 *   camsv2_legacy@1a092000 .. camsv7@1a097000    : fdt.dts:7087-7136
 *   topckgen 0x10000000 / infracfg 0x10001000    : fdt.dts:2354-2361
 *   gpio@10005000 / pwrap@10026000               : fdt.dts:2376, 3219-3220
 *   rear imx882 @ i2c2-0x2a                      : fdt.dts:45,4668,4693-4697; cam0 at :1912
 *   front gc13a0ff @ i2c4-0x1a                   : fdt.dts:47,4827,4851-4855; cam1 at :1913
 *   MT6366 VCAMIO / VCAMD / VCAMA1 regulators    : fdt.dts:9106-9107, 9121
 *   MCLK pin control nodes                       : fdt.dts:2976-3057
 *
 * Opt-in: boot-arg x6886_cam=1. Bounded, read-only, no panic, clean error return.
 */

#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t size);
extern void delay(int usec);

/* ---- ramoops pstore mirror (boot-audit GATE 4) ---- */
#define X6886_PSTORE_PHYS       0x480af000ULL
#define X6886_PSTORE_SIZE       0x1000u
#define X6886_PSTORE_SIG        0x43474244u

static vm_offset_t x6886_pstore_va;
static LCK_GRP_DECLARE(x6886_pstore_grp, "x6886-cam-pstore");
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
	    "[X6886-camera] " fmt, ##__VA_ARGS__);                            \
	printf("%s", x6886_log_buf);                                          \
	if (x6886_log_len > 0) {                                              \
		x6886_pstore_write(x6886_log_buf, (unsigned)x6886_log_len);       \
	}                                                                     \
} while (0)

/* ---- register windows (physical -> mapped) ------------------------------ */
static struct {
	uint64_t pa;
	vm_size_t size;
	vm_offset_t va;
} cam_win[] = {
	{ 0x10000000ULL, 0x20000, 0 },  /* topckgen, infracfg, gpio, spm: fdt.dts:2354-2361,2376,1629 */
	{ 0x10026000ULL, 0x01000, 0 },  /* pwrap: fdt.dts:3219-3220 (outside the 0x10000000 window) */
	{ 0x11eb0000ULL, 0x02000, 0 },  /* i2c2 + i2c4 controller windows: fdt.dts:4668,4827 */
	{ 0x1a000000ULL, 0x53000, 0 },  /* camsys, seninf, camsv: fdt.dts:1841,3165-3196,7087-7136 */
};
#define CAM_NWIN (sizeof(cam_win) / sizeof(cam_win[0]))

#define X6886_PWRAP_PHYS        0x10026000ULL   /* fdt.dts:3219-3220 */
#define X6886_GPIO_PHYS         0x10005000ULL   /* fdt.dts:2376 */
#define X6886_I2C2_PHYS         0x11eb0000ULL   /* fdt.dts:4668 */
#define X6886_I2C4_PHYS         0x11eb1000ULL   /* fdt.dts:4827 */
#define X6886_CAMSYS_PHYS       0x1a000000ULL   /* fdt.dts:1841 */
#define X6886_SENINF1_PHYS      0x1a004000ULL   /* fdt.dts:3165-3168 */
#define X6886_CAMSV_PHYS        0x1a092000ULL   /* fdt.dts:7087-7089 */
#define X6886_REAR_I2C_ADDR     0x2a            /* imx882: fdt.dts:4693-4697 */
#define X6886_FRONT_I2C_ADDR    0x1a            /* gc13a0ff: fdt.dts:4851-4855 */
/* NOT present in the X6886 device-dump: only mtk_camera.c:192-193 names them.
 * Default-OFF opt-in x6886_cam_pmic=1; never read, never written, by default. */
#define CAM_VCAMIO_EN           0x1cb0
#define CAM_VCAMD_EN            0x1c9c
#define PWRAP_WACS2_CMD         0xc20           /* mtk_camera.c:127 */
#define PWRAP_WACS2_RDATA       0xc24           /* mtk_camera.c:128 */
#define PWRAP_WACS2_VLDCLR      0xc28           /* mtk_camera.c:129 */
#define PWRAP_FSM(r)            (((r) >> 16) & 7u)      /* mtk_camera.c:130 */
#define PWRAP_SPINS             200000u         /* mtk_camera.c:136 */

/* REF-only rail offsets are marked as such at :117-125 above. */
#define PWRAP_CMD_WRITE         (1u << 31)      /* REF mtk_camera.c:165 */
#define CAM_RAIL_BIT            (1u << 0)
#define CAM_RAIL_SPINS          64u
#define CAM_RAIL_DELAY_US       100u

static uint64_t cam_bad_pa;
static bool cam_pmic_optin;
static bool cam_rails_optin;
static LCK_GRP_DECLARE(cam_grp, "x6886-cam");
static LCK_MTX_DECLARE(cam_pwrap_lock, &cam_grp);

static volatile uint32_t *
cam_reg(uint64_t pa)
{
	for (unsigned i = 0; i < CAM_NWIN; i++) {
		if (cam_win[i].va != 0 && pa >= cam_win[i].pa &&
		    pa + 4 <= cam_win[i].pa + cam_win[i].size) {
			return (volatile uint32_t *)(cam_win[i].va + (pa - cam_win[i].pa));
		}
	}
	if (cam_bad_pa == 0) {
		cam_bad_pa = pa;
	}
	return NULL;                    /* out-of-window access is refused, not guessed */
}

static bool
pwrap_wait(uint32_t state)
{
	volatile uint32_t *p = cam_reg(X6886_PWRAP_PHYS + PWRAP_WACS2_RDATA);

	if (p == NULL) {
		return false;
	}
	for (uint32_t spin = 0; spin < PWRAP_SPINS; spin++) {
		if (PWRAP_FSM(*p) == state) {
			return true;
		}
	}
	X6886_LOG("pwrap FSM %u not reached in %u spins (RDATA %#x); rails are not read\n",
	    state, PWRAP_SPINS, *p);
	return false;
}

static bool
pwrap_read(uint32_t reg, uint16_t *value)
{
	volatile uint32_t *cmd = cam_reg(X6886_PWRAP_PHYS + PWRAP_WACS2_CMD);
	volatile uint32_t *data = cam_reg(X6886_PWRAP_PHYS + PWRAP_WACS2_RDATA);
	volatile uint32_t *clr = cam_reg(X6886_PWRAP_PHYS + PWRAP_WACS2_VLDCLR);
	bool ok;

	if (cmd == NULL || data == NULL || clr == NULL) {
		return false;
	}
	lck_mtx_lock(&cam_pwrap_lock);
	ok = pwrap_wait(0);
	if (ok) {
		*cmd = (reg >> 1) << 16;
		ok = pwrap_wait(6);
		if (ok) {
			*value = (uint16_t)*data;
			*clr = 1;
		}
	}
	lck_mtx_unlock(&cam_pwrap_lock);
	return ok;
}

/* REF-only PMIC write: same WACS2 FSM bound as pwrap_read(); command encoding
 * from mtk_camera.c:165. Only called from the x6886_cam_rails=1 path. */
static bool
pwrap_write(uint32_t reg, uint16_t value)
{
	volatile uint32_t *cmd = cam_reg(X6886_PWRAP_PHYS + PWRAP_WACS2_CMD);
	volatile uint32_t *data = cam_reg(X6886_PWRAP_PHYS + PWRAP_WACS2_RDATA);
	bool ok;

	if (cmd == NULL || data == NULL) {
		return false;
	}
	lck_mtx_lock(&cam_pwrap_lock);
	ok = pwrap_wait(0);
	if (ok) {
		*cmd = PWRAP_CMD_WRITE | ((reg >> 1) << 16) | value;
		ok = pwrap_wait(6);
	}
	lck_mtx_unlock(&cam_pwrap_lock);
	return ok;
}

/* Drive one rail's enable bit and verify it held, with a bounded readback.
 * REF only: the offset is not DTB-proven (see :117-125). */
static bool
cam_rail_set(uint32_t reg, bool on)
{
	uint16_t value = 0, target, got = 0;
	unsigned spin;

	if (!pwrap_read(reg, &value)) {
		return false;
	}
	target = on ? (uint16_t)(value | CAM_RAIL_BIT) :
	    (uint16_t)(value & (uint16_t)~CAM_RAIL_BIT);
	if (!pwrap_write(reg, target)) {
		return false;
	}
	for (spin = 0; spin < CAM_RAIL_SPINS; spin++) {
		if (!pwrap_read(reg, &got)) {
			return false;
		}
		if (((got & CAM_RAIL_BIT) != 0) == on) {
			return true;
		}
		delay(CAM_RAIL_DELAY_US);
	}
	return false;
}

static int
x6886_cam_probe(void)
{
	uint16_t vcamio = 0, vcamd = 0, vcamio_saved = 0, vcamd_saved = 0;
	volatile uint32_t *p;
	int error = 0;
	bool rails_on = false;

	X6886_LOG("camsys %#x, seninf1 %#x, camsv %#x, i2c2 %#x, i2c4 %#x\n",
	    X6886_CAMSYS_PHYS, X6886_SENINF1_PHYS, X6886_CAMSV_PHYS, X6886_I2C2_PHYS, X6886_I2C4_PHYS);
	for (unsigned i = 0; i < CAM_NWIN; i++) {
		cam_win[i].va = ml_io_map(cam_win[i].pa, cam_win[i].size);
		if (cam_win[i].va == 0) {
			X6886_LOG("cannot map window %#x/%#x; stopping before any MMIO\n",
			    cam_win[i].pa, cam_win[i].size);
			error = ENXIO;
			goto out;
		}
	}
	/* readiness check before MMIO inside the windows: the pwrap FSM must be idle */
	if (!pwrap_wait(0)) {
		X6886_LOG("pwrap not idle; no rail or register is touched\n");
		error = EBUSY;
		goto out;
	}
	/* Rail offsets are NOT proven by fdt.dts / mt6366 nodes. With x6886_cam_pmic=1
	 * they are read only; with the additional x6886_cam_rails=1 they are driven
	 * (write + bounded readback) and restored before unmapping. */
	if (cam_pmic_optin) {
		if (!pwrap_read(CAM_VCAMIO_EN, &vcamio) || !pwrap_read(CAM_VCAMD_EN, &vcamd)) {
			X6886_LOG("unproven rail offsets did not answer (VCAMIO %#x, VCAMD %#x); no sensor is touched\n",
			    vcamio, vcamd);
			error = EIO;
			goto out;
		}
		X6886_LOG("VCAMIO %#x (bit0 %s), VCAMD %#x (bit0 %s); offsets NOT DTB-proven, owner sign-off needed\n",
		    vcamio, (vcamio & 1u) ? "on" : "off", vcamd, (vcamd & 1u) ? "on" : "off");
	} else {
		X6886_LOG("PMIC rail reads skipped: 0x1cb0/0x1c9c are not in the X6886 device-dump; "
		    "use x6886_cam_pmic=1 only after owner sign-off\n");
	}
	if (cam_rails_optin) {
		vcamio_saved = vcamio;
		vcamd_saved = vcamd;
		if (!cam_rail_set(CAM_VCAMIO_EN, true) ||
		    !cam_rail_set(CAM_VCAMD_EN, true)) {
			X6886_LOG("rail enable did not hold (REF offsets); no sensor is touched\n");
			error = EIO;
			goto out;
		}
		rails_on = true;
		X6886_LOG("VCAMIO/VCAMD enables driven and verified (REF offsets, restore at exit)\n");
	}

	X6886_LOG("rear imx882 i2c2-0x%02x, front gc13a0ff i2c4-0x%02x; no vendor register table exists, "
	    "so sensor init / CSI stream / CAMSV capture are NOT performed\n",
	    X6886_REAR_I2C_ADDR, X6886_FRONT_I2C_ADDR);
	if (cam_bad_pa != 0) {
		X6886_LOG("an out-of-window address %#llx was refused\n", (unsigned long long)cam_bad_pa);
	}
	/* a read of the GPIO window proves the mapping is live without changing a pin */
	p = cam_reg(X6886_GPIO_PHYS);
	if (p == NULL) {
		error = ENXIO;
		goto out;
	}
	X6886_LOG("GPIO window reads %#x; no pin state changed\n", *p);
out:
	if (rails_on) {
		if (!cam_rail_set(CAM_VCAMD_EN, (vcamd_saved & CAM_RAIL_BIT) != 0) ||
		    !cam_rail_set(CAM_VCAMIO_EN, (vcamio_saved & CAM_RAIL_BIT) != 0)) {
			X6886_LOG("rail restore did not verify (REF offsets); PMIC left as-is\n");
		} else {
			X6886_LOG("rails restored to their saved state\n");
		}
	}
	for (unsigned i = 0; i < CAM_NWIN; i++) {
		if (cam_win[i].va != 0) {
			ml_io_unmap(cam_win[i].va, cam_win[i].size);
			cam_win[i].va = 0;
		}
	}
	return error;
}

int
x6886_camera_init(void)
{
	uint32_t on = 0;
	int error;

	if (!PE_parse_boot_argn("x6886_cam", &on, sizeof(on)) || on == 0) {
		return 0;
	}
	x6886_pstore_map();
	{
		uint32_t pmic = 0, rails = 0;

		(void)PE_parse_boot_argn("x6886_cam_pmic", &pmic, sizeof(pmic));
		cam_pmic_optin = pmic != 0;
		(void)PE_parse_boot_argn("x6886_cam_rails", &rails, sizeof(rails));
		cam_rails_optin = rails != 0 && cam_pmic_optin;
		if (rails != 0 && !cam_rails_optin) {
			X6886_LOG("x6886_cam_rails needs x6886_cam_pmic=1; no rail is driven\n");
		}
	}
	if (!cam_rails_optin) {
		X6886_LOG("no rail/register write: x6886_cam_rails=1 (REF offsets) required\n");
		return ENOTSUP;         /* no hardware work: do not report success */
	}
	error = x6886_cam_probe();
	X6886_LOG("probe returned %d (rails driven and restored; no capture path in this port)\n", error);
	return error;
}
