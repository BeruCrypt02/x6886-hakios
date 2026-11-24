/*
 * X6886 (MT6789) audio: real MediaTek AFE scaffold, bus-read-only.
 *
 * Device proof (device-dump of this handset):
 *   AFE MMIO 0x11210000..0x11211fff        : iomem.txt:105, fdt.dts:5582-5584
 *   DT SPI IRQ 0xea = 234                  : fdt.dts:5585
 *   Linux GICv3 IRQ 266 (SPI 234 + 32)     : interrupts.txt:147
 *   AFE is its own clock controller (0x3a) : fdt.dts:1776-1780, 5590-5591
 *   topckgen / infracfg_ao / apmixedsys    : fdt.dts:1582-1593, 1639-1643
 *   scpsys power controller, domain 0x0b   : fdt.dts:1629-1636, 5589
 *   mt6366 PMIC / mt6358codec on pwrap     : fdt.dts:3228-3229, 3832-3840
 *   vaud28 = 0x2ab980 uV = 2.8 V, always on: fdt.dts:3727-3734
 *   RT5512 speaker amp, I2C 0x5c           : fdt.dts:5186-5192, 5149
 *   sound card / spk-i2s = <3 0>           : fdt.dts:7626-7635
 *
 * Real MediaTek register/bit definitions:
 *   mt6789-reg.h:31-70, 2154-2184, 2267-2296, 2309-2310, 2644, 2884-2891,
 *               2949-2951, 3085-3087
 *   mt6789-afe-pcm.c:2588-2591, 2835-2926, 2953-2967, 3335-3404
 *   mt6789-afe-clk.c:41-63, 241-330, 711-772
 *   mt6789-mt6366.c:27-49, 116-122
 * Audio paths (stock Transsion kctl map):
 *   otadump/vendor/etc/audio_device.xml:26-32, 50-56, 85-96, 109-120, 193-204
 *
 * No Android .so/.ko is used. No modem/NVRAM/IMEI write, no iscle. No panic.
 * Opt-in: boot-arg x6886_audio=1. All polling is bounded; failure is a clean errno.
 */

#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void delay(int usec);

/* ---- ramoops pstore mirror (shared X6886 convention, sig +0, start +4,
 *      size +8, data +12) ---- */
#define X6886_PSTORE_PHYS       0x480af000ULL
#define X6886_PSTORE_SIZE       0x1000u
#define X6886_PSTORE_SIG        0x43474244u

static vm_offset_t x6886_pstore_va;
static LCK_GRP_DECLARE(x6886_audio_pstore_grp, "x6886-audio-pstore");
static LCK_SPIN_DECLARE(x6886_audio_pstore_lock, &x6886_audio_pstore_grp);

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
	lck_spin_lock(&x6886_audio_pstore_lock);
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
	lck_spin_unlock(&x6886_audio_pstore_lock);
}

#define X6886_LOG(fmt, ...) do {                                              \
	char x6886_log_buf[256];                                              \
	int x6886_log_len = snprintf(x6886_log_buf, sizeof(x6886_log_buf),    \
	    "[X6886-audio] " fmt, ##__VA_ARGS__);                             \
	printf("%s", x6886_log_buf);                                          \
	if (x6886_log_len > 0) {                                              \
		x6886_pstore_write(x6886_log_buf, (unsigned)x6886_log_len);       \
	}                                                                     \
} while (0)

/* ---- device constants --------------------------------------------------- */
#define X6886_AFE_PHYS          0x11210000u     /* iomem.txt:105; fdt.dts:5584 */
#define X6886_AFE_SIZE          0x2000u         /* iomem.txt:105; fdt.dts:5584 */
#define X6886_AFE_SPI           234u            /* fdt.dts:5585: 0xea */
#define X6886_AFE_LINUX_IRQ     266u            /* interrupts.txt:147: GICv3 266 */
#define X6886_AUDCLK_PHYS       0x11210000u     /* fdt.dts:1776-1780: phandle 0x3a */
#define X6886_TOPCHGEN_PHYS     0x10000000u     /* fdt.dts:1582-1586: phandle 0x2b */
#define X6886_INFRACFG_PHYS     0x10001000u     /* fdt.dts:1589-1593: phandle 0x2c */
#define X6886_APMIXED_PHYS      0x1000c000u     /* fdt.dts:1639-1643: phandle 0x3c */
#define X6886_SCPSYS_PHYS       0x10006000u     /* fdt.dts:1629-1636: phandle 0x2e */
#define X6886_PWRAP_PHYS        0x10026000u     /* fdt.dts:3219-3220: pwrap@10026000 */
#define X6886_I2C6_PHYS         0x1101a000u     /* fdt.dts:5149: i2c@1101a000 */
#define X6886_SPK_AMP_I2C       0x5cu           /* fdt.dts:5189: rt5512 reg <0x5c> */
#define X6886_VAUD28_UV         2800000u        /* fdt.dts:3729: 0x2ab980 = 2.8 V */
#define X6886_SPK_I2S           3u              /* fdt.dts:7631: spk-i2s <3 0> */
#define X6886_AUDIO_DOMAIN      0x0bu           /* fdt.dts:5589: power-domains <0x2e 0x0b> */

/* AFE register offsets (genuine MTK map) */
#define AUDIO_TOP_CON0          0x0000u         /* mt6789-reg.h:2154 */
#define AUDIO_TOP_CON1          0x0004u         /* mt6789-reg.h:2155 */
#define AUDIO_TOP_CON2          0x0008u         /* mt6789-reg.h:2156 */
#define AUDIO_TOP_CON3          0x000cu         /* mt6789-reg.h:2157 */
#define AFE_DAC_CON0            0x0010u         /* mt6789-reg.h:2158 */
#define AFE_DL1_CON0            0x004cu         /* mt6789-reg.h:2171 */
#define AFE_DL1_BASE_MSB        0x0050u         /* mt6789-reg.h:2172 */
#define AFE_DL1_BASE            0x0054u         /* mt6789-reg.h:2173 */
#define AFE_DL1_CUR_MSB         0x0058u         /* mt6789-reg.h:2174 */
#define AFE_DL1_CUR             0x005cu         /* mt6789-reg.h:2175 */
#define AFE_DL1_END_MSB         0x0060u         /* mt6789-reg.h:2176 */
#define AFE_DL1_END             0x0064u         /* mt6789-reg.h:2177 */
#define AFE_VUL12_CON0          0x0374u         /* mt6789-reg.h:2282 */
#define AFE_VUL12_BASE_MSB      0x0378u         /* mt6789-reg.h:2283 */
#define AFE_VUL12_BASE          0x037cu         /* mt6789-reg.h:2284 */
#define AFE_VUL12_CUR_MSB       0x0380u         /* mt6789-reg.h:2285 */
#define AFE_VUL12_CUR           0x0384u         /* mt6789-reg.h:2286 */
#define AFE_VUL12_END_MSB       0x0388u         /* mt6789-reg.h:2287 */
#define AFE_VUL12_END           0x038cu         /* mt6789-reg.h:2288 */
#define AFE_IRQ_MCU_CNT0        0x0300u         /* mt6789-reg.h:2267 */
#define AFE_IRQ_MCU_CON0        0x03a0u         /* mt6789-reg.h:2291 */
#define AFE_IRQ_MCU_STATUS      0x03a4u         /* mt6789-reg.h:2292 */
#define AFE_IRQ_MCU_CLR         0x03a8u         /* mt6789-reg.h:2293 */
#define AFE_IRQ_MCU_EN          0x03b4u         /* mt6789-reg.h:2296 */
#define AFE_APLL1_TUNER_CFG     0x03f0u         /* mt6789-reg.h:2309 */
#define AFE_APLL2_TUNER_CFG     0x03f4u         /* mt6789-reg.h:2310 */
#define AFE_HD_ENGEN_ENABLE     0x0dd0u         /* mt6789-reg.h:2644 */
#define AFE_AGENT_ON            0x1b00u         /* mt6789-reg.h:2884 */

/* AFE bits (genuine MTK map) */
#define PDN_22M_SFT             8u              /* mt6789-reg.h:70 */
#define PDN_24M_SFT             9u              /* mt6789-reg.h:67 */
#define PDN_APLL2_TUNER_SFT     18u             /* mt6789-reg.h:55 */
#define PDN_ADC_SFT             24u             /* mt6789-reg.h:46 */
#define PDN_DAC_SFT             25u             /* mt6789-reg.h:43 */
#define PDN_DAC_PREDIS_SFT      26u             /* mt6789-reg.h:40 */
#define PDN_TML_SFT             27u             /* mt6789-reg.h:37 */
#define PDN_NLE_SFT             28u             /* mt6789-reg.h:34 */
#define AHB_IDLE_EN_EXT_SFT     29u             /* mt6789-reg.h:31; pcm.c:2967 */
#define DL1_ON_SFT              4u              /* mt6789-reg.h:2949 */
#define VUL12_ON_SFT            31u             /* mt6789-reg.h:2889 */
#define IRQ0_MCU_ON_SFT         0u              /* mt6789-reg.h:1622 */
#define IRQ0_MCU_CLR_SFT        0u              /* mt6789-reg.h:1797 */
#define IRQ0_MCU_EN_SFT         0u              /* mt6789-reg.h:1833 */
#define AFE_IRQ_STATUS_BITS     0x87ffffffu     /* mt6789-reg.h:3085 */
#define AFE_IRQ_CNT_MASK        0x3ffffu        /* mt6789-reg.h:3087 */

#define X6886_PDN_MASK ((1u << PDN_ADC_SFT) | (1u << PDN_DAC_SFT) |          \
    (1u << PDN_DAC_PREDIS_SFT) | (1u << PDN_TML_SFT) | (1u << PDN_NLE_SFT) | \
    (1u << PDN_22M_SFT) | (1u << PDN_24M_SFT))
#define X6886_DEAD_BUS          0xffffffffu     /* unclocked/unmapped bus sentinel */
#define X6886_READY_SPINS       64u             /* scaffold hard bound, not a device constant */
#define X6886_READY_DELAY_US    100u            /* scaffold hard bound, not a device constant */

static volatile uint32_t *afe;

static uint32_t
afe_read(uint32_t reg)
{
	if (afe == NULL || reg >= X6886_AFE_SIZE || (reg & 3u) != 0) {
		return X6886_DEAD_BUS;
	}
	return afe[reg / 4];
}

/* All failure registers are mirrored to ramoops before returning an errno. */
static void
x6886_afe_dump(const char *stage)
{
	X6886_LOG("%s: TOP0 %#x TOP1 %#x TOP2 %#x TOP3 %#x IRQ_STATUS %#x IRQ_EN %#x\n",
	    stage, afe_read(AUDIO_TOP_CON0), afe_read(AUDIO_TOP_CON1),
	    afe_read(AUDIO_TOP_CON2), afe_read(AUDIO_TOP_CON3),
	    afe_read(AFE_IRQ_MCU_STATUS), afe_read(AFE_IRQ_MCU_EN));
	X6886_LOG("%s: DL1_CON0 %#x DL1_BASE %#x DL1_CUR %#x DL1_END %#x\n",
	    stage, afe_read(AFE_DL1_CON0), afe_read(AFE_DL1_BASE),
	    afe_read(AFE_DL1_CUR), afe_read(AFE_DL1_END));
	X6886_LOG("%s: VUL12_CON0 %#x VUL12_BASE %#x VUL12_CUR %#x VUL12_END %#x AGENT_ON %#x\n",
	    stage, afe_read(AFE_VUL12_CON0), afe_read(AFE_VUL12_BASE),
	    afe_read(AFE_VUL12_CUR), afe_read(AFE_VUL12_END), afe_read(AFE_AGENT_ON));
}

/* Clock/power readiness: the source says AUDIO_TOP_CON[0-1] CCF-controlled
 * (mt6789-afe-pcm.c:2588-2589), so XNU does not drive CCF or the PLLs. It
 * verifies that power-down bits are already clear and that the bus answers.
 * Bounded: at most X6886_READY_SPINS reads, no infinite loop.
 */
static int
x6886_afe_ready(void)
{
	uint32_t top0 = 0, prev = 0;
	unsigned spin;

	for (spin = 0; spin < X6886_READY_SPINS; spin++) {
		top0 = afe_read(AUDIO_TOP_CON0);
		if (top0 == X6886_DEAD_BUS ||
		    afe_read(AUDIO_TOP_CON1) == X6886_DEAD_BUS) {
			x6886_afe_dump("dead AFE bus");
			return ENXIO;
		}
		if (spin > 0 && top0 == prev) {
			break;
		}
		prev = top0;
		delay(X6886_READY_DELAY_US);
	}
	if (spin == X6886_READY_SPINS) {
		x6886_afe_dump("AFE TOP0 never settled");
		return EBUSY;
	}
	if ((top0 & X6886_PDN_MASK) != 0 || (top0 & (1u << AHB_IDLE_EN_EXT_SFT)) == 0) {
		X6886_LOG("AFE power/clock gates not ready: TOP0 %#x PDN mask %#x; no MMIO write issued\n",
		    top0, top0 & X6886_PDN_MASK);
		x6886_afe_dump("AFE not ready");
		return EBUSY;
	}
	X6886_LOG("AFE ready: TOP0 %#x (PDN clear), TOP1 %#x; SPI %u / Linux IRQ %u\n",
	    top0, afe_read(AUDIO_TOP_CON1), X6886_AFE_SPI, X6886_AFE_LINUX_IRQ);
	return 0;
}

/* DMA/WPDMA window check. Read-only: no buffer is started, no IRQ is cleared.
 * Reference register roles: mt6789-afe-pcm.c:2108-2116, 3335-3404.
 */
static void
x6886_afe_dma_check(void)
{
	uint32_t base = afe_read(AFE_DL1_BASE);
	uint32_t end = afe_read(AFE_DL1_END);
	uint32_t cur = afe_read(AFE_DL1_CUR);
	uint32_t vbase = afe_read(AFE_VUL12_BASE);
	uint32_t vend = afe_read(AFE_VUL12_END);
	uint32_t vcur = afe_read(AFE_VUL12_CUR);

	if (base == 0 && end == 0) {
		X6886_LOG("DMA idle: DL1 base/end zero, VUL12 base %#x end %#x\n", vbase, vend);
		return;
	}
	if (base <= end && (cur < base || cur > end)) {
		X6886_LOG("DL1 cursor %#x outside [%#x,%#x]; playback not started\n", cur, base, end);
		x6886_afe_dump("DL1 cursor");
		return;
	}
	if (vbase <= vend && (vcur < vbase || vcur > vend)) {
		X6886_LOG("VUL12 cursor %#x outside [%#x,%#x]; capture not started\n", vcur, vbase, vend);
		x6886_afe_dump("VUL12 cursor");
		return;
	}
	X6886_LOG("DMA windows coherent: DL1 %#x-%#x cur %#x, VUL12 %#x-%#x cur %#x\n",
	    base, end, cur, vbase, vend, vcur);
}

/* MT6366 DAPM paths, transcribed from the stock kctl map. XNU has no ALSA
 * kcontrols, so these are declarative routes only; no PMIC register is written
 * (the MT6358 codec register map is not present in refs/).
 */
struct x6886_route {
	const char *name;
	uint32_t memif;
	const char *ctl[4];
	const char *val[4];
};

static const struct x6886_route x6886_routes[] = {
	{ "speaker", AFE_DL1_CON0,
	  { "DAC In Mux", "LOL Mux", NULL, NULL },
	  { "Normal Path", "Playback", NULL, NULL } },       /* audio_device.xml:50-56 */
	{ "earpiece", AFE_DL1_CON0,
	  { "DAC In Mux", "RCV Mux", NULL, NULL },
	  { "Normal Path", "Voice Playback", NULL, NULL } }, /* audio_device.xml:26-32 */
	{ "main mic (AIN0)", AFE_VUL12_CON0,
	  { "ADC L Mux", "ADC R Mux", "PGA L Mux", "PGA R Mux" },
	  { "Left Preamplifier", "Right Preamplifier", "AIN0", "AIN0" } }, /* :85-96 */
	{ "rear mic (AIN2)", AFE_VUL12_CON0,
	  { "ADC L Mux", "ADC R Mux", "PGA L Mux", "PGA R Mux" },
	  { "Left Preamplifier", "Right Preamplifier", "AIN2", "AIN2" } }, /* :109-120 */
	{ "headset mic (AIN1)", AFE_VUL12_CON0,
	  { "ADC L Mux", "ADC R Mux", "PGA L Mux", "PGA R Mux" },
	  { "Left Preamplifier", "Right Preamplifier", "AIN1", "AIN1" } }, /* :193-204 */
};

static void
x6886_route_dump(void)
{
	for (unsigned i = 0; i < sizeof(x6886_routes) / sizeof(x6886_routes[0]); i++) {
		const struct x6886_route *r = &x6886_routes[i];

		X6886_LOG("route %s memif %#x: %s=%s %s=%s\n",
		    r->name, r->memif, r->ctl[0], r->val[0], r->ctl[1], r->val[1]);
	}
	X6886_LOG("codec mt6366 on pwrap %#x, vaud28 %u uV, spk amp I2C %#x on %#x, spk-i2s %u\n",
	    X6886_PWRAP_PHYS, X6886_VAUD28_UV, X6886_SPK_AMP_I2C, X6886_I2C6_PHYS, X6886_SPK_I2S);
}

int
x6886_audio_init(void)
{
	uint32_t on = 0;
	int err;

	if (!PE_parse_boot_argn("x6886_audio", &on, sizeof(on)) || on == 0) {
		return 0;
	}
	x6886_pstore_map();
	afe = (volatile uint32_t *)ml_io_map(X6886_AFE_PHYS, X6886_AFE_SIZE);
	if (afe == NULL) {
		X6886_LOG("cannot map AFE %#x; no audio state\n", X6886_AFE_PHYS);
		return ENXIO;
	}
	err = x6886_afe_ready();
	if (err != 0) {
		return err;
	}
	x6886_afe_dma_check();
	x6886_route_dump();
	X6886_LOG("AFE scaffold online (read-only); no DMA start, no PMIC write, no IRQ handler\n");
	return 0;
}
