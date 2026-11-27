/*
 * X6886 (MT6789) USB2.0: real MediaTek MUSB controller + generic T-PHY v2.
 *
 * Provenance (all local, offline):
 *   refs/alps-device-modules-6.12/drivers/misc/mediatek/usb/usb20/
 *     mtk_musb_reg.h, musb_core.c, musb_core.h, musb_gadget.c
 *   refs/alps-device-modules-6.12/drivers/phy/mediatek/phy-mtk-tphy.c
 *   device-dump/fdt.dts, iomem.txt, interrupts.txt, modules.txt
 * Every MMIO base, IRQ, clock index and register below is cited with its
 * file:line. PORT-USB.md is the manifest (proof table + gaps).
 *
 * This is NOT DWC3. The handset's live node is mediatek,mt6789-usb20
 * (fdt.dts:7458) and its IRQ is reported as musb-hdrc (interrupts.txt:49).
 *
 * Role: the debug/data channel is a minimal MUSB peripheral-mode bulk pair
 * (EP1 IN/OUT). No ADB, no Android userspace, no .so/.ko, no modem or NVRAM
 * access. A weak x6886_usb_notify_data() hook is the iOS-side notification;
 * the default implementation only logs and must be wired by the consumer.
 *
 * Boot args: x6886_usb=N (0 off, 1 readiness, 2 + PHY/MUSB init + bulk),
 * x6886_usb_spin=N (64..65536, default 1024) bounded transfer polls.
 */
#include <kern/clock.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t size);
extern void delay(int usec);

/* ---- device blocks: device-dump/fdt.dts and device-dump/iomem.txt ------------- */

#define X6886_MUSB_PHYS         0x11200000u     /* fdt.dts:7459; iomem.txt:104 */
#define X6886_MUSB_SIZE         0x10000u        /* fdt.dts:7459; iomem.txt:104 */
#define X6886_PHY_PHYS          0x11f40000u     /* fdt.dts:7459,7494; iomem.txt:114-115 */
#define X6886_PHY_SIZE          0x700u          /* fdt.dts:7494; iomem.txt:115 */
#define X6886_TOPCKGEN_PHYS     0x10000000u     /* fdt.dts:1582-1584 (phandle 0x2b, ref_clk) */
#define X6886_INFRACFG_PHYS     0x10001000u     /* fdt.dts:1589-1593 (phandle 0x2c, sys_clk) */
#define X6886_APMIXED_PHYS      0x1000C000u     /* fdt.dts:1639-1643 (phandle 0x3c, src_clk) */
#define X6886_CLOCK_SIZE        0x1000u         /* fdt.dts:1584,1591,1641 */
#define X6886_USB_IRQ           160u            /* fdt.dts:7460 (SPI 0x80+32); interrupts.txt:49 */
#define X6886_RAMOOPS_PHYS      0x480af000u     /* fdt.dts:92,712-713; task-supplied window */
#define X6886_RAMOOPS_SIZE      0x1000u
#define X6886_PSTORE_SIG        0x43474244u
#define X6886_PSTORE_HEAD       12u
#define X6886_USB_NUM_EPS       16u             /* fdt.dts:7466 (num_eps = 0x10) */
#define X6886_USB_EP            1u              /* first bulk pair after EP0; FIFOSIZE read back */
#define X6886_USB_MAX_PACKET    64u             /* MUSB_EP0_FIFOSIZE, mtk_musb_reg.h:11; bulk capped here */
#define X6886_USB_FIFO_SIZE_CODE 6u             /* 64 B: 1<<(6+3), mtk_musb_reg.h:72-73 */
#define X6886_USB_RXQ           4096u
#define X6886_USB_SPIN_MAX      1024u
#define X6886_USB_SPIN_MIN      64u
#define X6886_USB_SPIN_CAP      65536u

/* DT clock indices — logged only; no gate register is proven in the dump */
#define X6886_CLK_SYS_IDX       0x22u           /* fdt.dts:7461-7462, "sys_clk" = <0x2c 0x22> */
#define X6886_CLK_REF_IDX       0x1du           /* fdt.dts:7461-7462, "ref_clk" = <0x2b 0x1d> */
#define X6886_CLK_SRC_IDX       0x0du           /* fdt.dts:7461-7462, "src_clk" = <0x3c 0x0d> */
#define X6886_CLK_PHY_REF_HZ    26000000u       /* fdt.dts:1541-1545 (clk26m, phandle 0x54) */

/* MUSB controller registers — refs/.../usb20/mtk_musb_reg.h (line in comment) */
#define MUSB_FADDR              0x00u           /* :201 */
#define MUSB_POWER              0x01u           /* :202 */
#define MUSB_INTRTX             0x02u           /* :204 */
#define MUSB_INTRRX             0x04u           /* :205 */
#define MUSB_INTRTXE            0x06u           /* :206 */
#define MUSB_INTRRXE            0x08u           /* :207 */
#define MUSB_INTRUSB            0x0Au           /* :208 */
#define MUSB_INTRUSBE           0x0Bu           /* :209 */
#define MUSB_INDEX              0x0Eu           /* :211 */
#define MUSB_FIFO_OFFSET(e)     (0x20u + ((e) * 4u))             /* :215 */
#define MUSB_DEVCTL             0x60u           /* :221 */
#define MUSB_OPSTATE            0x620u          /* :223 */
#define MUSB_TXFIFOSZ           0x62u           /* :237 */
#define MUSB_RXFIFOSZ           0x63u           /* :238 */
#define MUSB_HWVERS             0x6Cu           /* :243 */
#define MUSB_ULPI_REG_DATA      0x74u           /* :247 */
#define MUSB_SWRST              0x74u           /* :301 */
#define MUSB_EP_OFFSET(o)       (0x10u + (o))                    /* :277-279 */
#define MUSB_EP_FIFOSZ          MUSB_EP_OFFSET(0x0Fu)            /* :274,277-279 */
#define MUSB_TXMAXP             MUSB_EP_OFFSET(0x00u)            /* :261 */
#define MUSB_TXCSR              MUSB_EP_OFFSET(0x02u)            /* :262 */
#define MUSB_RXMAXP             MUSB_EP_OFFSET(0x04u)            /* :264 */
#define MUSB_RXCSR              MUSB_EP_OFFSET(0x06u)            /* :266 */
#define MUSB_RXCOUNT            MUSB_EP_OFFSET(0x08u)            /* :268 */
#define MUSB_TXTYPE             MUSB_EP_OFFSET(0x0Au)            /* :269 */
#define MUSB_RXTYPE             MUSB_EP_OFFSET(0x0Cu)            /* :272 */

#define MUSB_POWER_SOFTCONN     0x40u           /* mtk_musb_reg.h:19 */
#define MUSB_POWER_HSENAB       0x20u           /* mtk_musb_reg.h:20 */
#define MUSB_POWER_RESET        0x08u           /* mtk_musb_reg.h:22 */
#define MUSB_POWER_ENSUSPEND    0x01u           /* mtk_musb_reg.h:25 */
#define MUSB_INTR_SUSPEND       0x01u           /* mtk_musb_reg.h:28 */
#define MUSB_INTR_RESUME        0x02u           /* mtk_musb_reg.h:29 */
#define MUSB_INTR_RESET         0x04u           /* mtk_musb_reg.h:30 */
#define MUSB_INTR_CONNECT       0x10u           /* mtk_musb_reg.h:33 */
#define MUSB_INTR_DISCONNECT    0x20u           /* mtk_musb_reg.h:34 */
#define MUSB_DEVCTL_BDEVICE     0x80u           /* mtk_musb_reg.h:39 */
#define MUSB_DEVCTL_VBUS        0x18u           /* mtk_musb_reg.h:42 */
#define MUSB_DEVCTL_VBUS_SHIFT  3u              /* mtk_musb_reg.h:43 */
#define MUSB_DEVCTL_HM          0x04u           /* mtk_musb_reg.h:44 */
#define MUSB_DEVCTL_SESSION     0x01u           /* mtk_musb_reg.h:46 */
#define MUSB_CONFIGDATA_DYNFIFO 0x04u           /* mtk_musb_reg.h:119 */
#define MUSB_FIFOSZ_DPB         0x10u           /* mtk_musb_reg.h:71 */
#define MUSB_FIFOSZ_SIZE        0x0Fu           /* mtk_musb_reg.h:73 */
#define MUSB_TXCSR_TXPKTRDY     0x0001u         /* mtk_musb_reg.h:131 */
#define MUSB_TXCSR_FLUSHFIFO    0x0008u         /* mtk_musb_reg.h:129 */
#define MUSB_TXCSR_P_SENTSTALL  0x0020u         /* mtk_musb_reg.h:136 */
#define MUSB_TXCSR_P_SENDSTALL  0x0010u         /* mtk_musb_reg.h:137 */
#define MUSB_RXCSR_RXPKTRDY     0x0001u         /* mtk_musb_reg.h:166 */
#define MUSB_RXCSR_FLUSHFIFO    0x0010u         /* mtk_musb_reg.h:163 */
#define MUSB_RXCSR_AUTOCLEAR    0x8000u         /* mtk_musb_reg.h:156 */
#define MUSB_RXCSR_P_SENTSTALL  0x0040u         /* mtk_musb_reg.h:174 */
#define MUSB_SWRST_SWRST        (1u << 1)       /* mtk_musb_reg.h:308 */
#define MUSB_SWRST_DISUSBRESET  (1u << 0)       /* mtk_musb_reg.h:309 */
#define MUSB_TYPE_PROTO_BULK    2u              /* USB_ENDPOINT_XFER_BULK from usb/ch9.h */
#define MUSB_TYPE_PROTO_SHIFT   4u              /* mtk_musb_reg.h:110 */

/* MediaTek generic T-PHY v2 USB2 banks — refs/.../phy/mediatek/phy-mtk-tphy.c */
#define U3P_U2PHY_COM           0x300u          /* :42 (SSUSB_SIFSLV_V2_U2PHY_COM), :1766 */
#define U3P_USBPHYACR0          0x000u          /* :52 */
#define PA0_RG_USB20_INTR_EN    (1u << 5)       /* :55 */
#define U3P_USBPHYACR4          0x020u          /* :107 */
#define P2C_U2_GPIO_CTR_MSK     ((1u << 9) | (1u << 8))         /* :108-110 */
#define U3P_USBPHYACR5          0x014u          /* :79 */
#define PA5_RG_U2_HS_100U_U3_EN (1u << 11)      /* :82 */
#define U3P_USBPHYACR6          0x018u          /* :84 */
#define PA6_RG_U2_BC11_SW_EN    (1u << 23)      /* :94 */
#define PA6_RG_U2_OTG_VBUSCMP_EN (1u << 20)     /* :95 */
#define PA6_RG_U2_DISCTH        (0xFu << 4)     /* :96 */
#define PA6_RG_U2_SQTH          0x0Fu           /* :99 */
#define PA6_RG_U2_PHY_REV6      (0x3u << 30)    /* :86-87 */
#define U3P_U2PHYDTM0           0x068u          /* :123 */
#define P2C_FORCE_UART_EN       (1u << 26)      /* :124 */
#define P2C_FORCE_SUSPENDM      (1u << 18)      /* :129 */
#define P2C_RG_DATAIN           (0xFu << 10)    /* :131 */
#define P2C_RG_XCVRSEL          (0x3u << 4)     /* :134-135 */
#define P2C_RG_SUSPENDM         (1u << 3)       /* :136 */
#define P2C_DTM0_PART_MASK      ((1u << 23) | (1u << 21) | (1u << 20) | \
                                 (1u << 19) | (1u << 18) | (1u << 17) | \
                                 (1u << 7)  | (1u << 6)  | (1u << 2))   /* :138-143 */
#define U3P_U2PHYDTM1           0x06Cu          /* :151 */
#define P2C_RG_UART_EN          (1u << 16)      /* :152 */
#define P2C_RG_VBUSVALID        (1u << 5)       /* :159 */
#define P2C_RG_SESSEND          (1u << 4)       /* :160 */
#define P2C_RG_AVALID           (1u << 2)       /* :162 */

/* ---- state ------------------------------------------------------------------- */

static vm_offset_t musb_mmio, phy_mmio, topckgen_mmio, infracfg_mmio, apmixed_mmio;
static vm_offset_t x6886_usb_pstore_va;
static LCK_GRP_DECLARE(x6886_usb_pstore_grp, "x6886-usb-pstore");
static LCK_SPIN_DECLARE(x6886_usb_pstore_lock, &x6886_usb_pstore_grp);

static bool x6886_usb_ready;
static bool x6886_usb_peripheral;
static bool x6886_usb_connected;
static uint32_t x6886_usb_spin = X6886_USB_SPIN_MAX;
static uint32_t x6886_usb_seq;
static uint8_t x6886_usb_rxq[X6886_USB_RXQ];
static uint32_t x6886_usb_rxq_head, x6886_usb_rxq_count;

#define X6886_STAGE_MAP         1u
#define X6886_STAGE_CLOCK       2u
#define X6886_STAGE_POWER       3u
#define X6886_STAGE_PHY         4u
#define X6886_STAGE_RESET       5u
#define X6886_STAGE_MODE        6u
#define X6886_STAGE_EP          7u
#define X6886_STAGE_BULK        8u

static inline uint8_t
rd8(vm_offset_t base, uint32_t off)
{
	return *(volatile uint8_t *)(base + off);
}

static inline uint16_t
rd16(vm_offset_t base, uint32_t off)
{
	return *(volatile uint16_t *)(base + off);
}

static inline uint32_t
rd32(vm_offset_t base, uint32_t off)
{
	return *(volatile uint32_t *)(base + off);
}

static inline void
wr8(vm_offset_t base, uint32_t off, uint8_t v)
{
	*(volatile uint8_t *)(base + off) = v;
}

static inline void
wr16(vm_offset_t base, uint32_t off, uint16_t v)
{
	*(volatile uint16_t *)(base + off) = v;
}

static inline void
wr32(vm_offset_t base, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(base + off) = v;
}

/* ---- shared ramoops pstore ring (same header as sibling X6886 drivers) -------- */

static void
x6886_usb_pstore_map(void)
{
	if (x6886_usb_pstore_va == 0) {
		x6886_usb_pstore_va = ml_io_map(X6886_RAMOOPS_PHYS, X6886_RAMOOPS_SIZE);
	}
}

static void
x6886_usb_pstore_write(const char *message, unsigned length)
{
	volatile uint8_t *base;
	uint32_t sig, start, size;
	unsigned n, i;

	if (x6886_usb_pstore_va == 0 || message == NULL || length == 0) {
		return;
	}
	lck_spin_lock(&x6886_usb_pstore_lock);
	base = (volatile uint8_t *)x6886_usb_pstore_va;
	sig = rd32((vm_offset_t)base, 0);
	start = rd32((vm_offset_t)base, 4);
	size = rd32((vm_offset_t)base, 8);
	if (sig == X6886_PSTORE_SIG && size >= 16 && size <= X6886_RAMOOPS_SIZE - X6886_PSTORE_HEAD) {
		if (start >= size) {
			start = 0;
		}
		n = length < size - 2 ? length : size - 2;
		for (i = 0; i < n; i++) {
			base[X6886_PSTORE_HEAD + ((start + i) % size)] = (uint8_t)message[i];
		}
		base[X6886_PSTORE_HEAD + ((start + n) % size)] = '\n';
		wr32((vm_offset_t)base, 4, (start + n + 1) % size);
	}
	lck_spin_unlock(&x6886_usb_pstore_lock);
}

#define X6886_LOG(fmt, ...) do {                                              \
	char x6886_log_buf[256];                                              \
	int x6886_log_len = snprintf(x6886_log_buf, sizeof(x6886_log_buf),    \
	    "[X6886-usb] " fmt, ##__VA_ARGS__);                               \
	printf("%s", x6886_log_buf);                                          \
	if (x6886_log_len > 0) {                                              \
		x6886_usb_pstore_write(x6886_log_buf, (unsigned)x6886_log_len);   \
	}                                                                     \
} while (0)

/* Notification hook: the iOS consumer overrides this; default is log only. */
__attribute__((weak)) void
x6886_usb_notify_data(const uint8_t *data, unsigned length)
{
	(void)data;
	X6886_LOG("bulk OUT %u bytes (no consumer wired)", length);
}

static void
x6886_usb_fail(uint32_t stage, int status)
{
	X6886_LOG("FAIL stage %u status %d regs hwvers=0x%04x cfg=0x%02x devctl=0x%02x "
	    "power=0x%02x swrst=0x%04x dtm0=0x%08x dtm1=0x%08x seq=%u",
	    stage, status,
	    musb_mmio ? rd16(musb_mmio, MUSB_HWVERS) : 0,
	    musb_mmio ? rd8(musb_mmio, MUSB_EP_FIFOSZ) : 0,
	    musb_mmio ? rd8(musb_mmio, MUSB_DEVCTL) : 0,
	    musb_mmio ? rd8(musb_mmio, MUSB_POWER) : 0,
	    musb_mmio ? rd16(musb_mmio, MUSB_SWRST) : 0,
	    phy_mmio ? rd32(phy_mmio, U3P_U2PHY_COM + U3P_U2PHYDTM0) : 0,
	    phy_mmio ? rd32(phy_mmio, U3P_U2PHY_COM + U3P_U2PHYDTM1) : 0,
	    ++x6886_usb_seq);
}

static void
x6886_usb_unmap(void)
{
	if (musb_mmio) {
		ml_io_unmap(musb_mmio, X6886_MUSB_SIZE);
		musb_mmio = 0;
	}
	if (phy_mmio) {
		ml_io_unmap(phy_mmio, X6886_PHY_SIZE);
		phy_mmio = 0;
	}
	if (topckgen_mmio) {
		ml_io_unmap(topckgen_mmio, X6886_CLOCK_SIZE);
		topckgen_mmio = 0;
	}
	if (infracfg_mmio) {
		ml_io_unmap(infracfg_mmio, X6886_CLOCK_SIZE);
		infracfg_mmio = 0;
	}
	if (apmixed_mmio) {
		ml_io_unmap(apmixed_mmio, X6886_CLOCK_SIZE);
		apmixed_mmio = 0;
	}
}

/* ---- power / clock readiness: presence only, no guessed gate writes ---------- */

static int
x6886_usb_clocks_ready(void)
{
	topckgen_mmio = ml_io_map(X6886_TOPCKGEN_PHYS, X6886_CLOCK_SIZE);
	infracfg_mmio = ml_io_map(X6886_INFRACFG_PHYS, X6886_CLOCK_SIZE);
	apmixed_mmio = ml_io_map(X6886_APMIXED_PHYS, X6886_CLOCK_SIZE);
	if (topckgen_mmio == 0 || infracfg_mmio == 0 || apmixed_mmio == 0) {
		return -ENODEV;
	}

	/* The dump names the three clocks but gives no gate offset/bit; presence of
	 * the three syscon windows is the only honest readiness proof here. The
	 * controller/PHY readbacks below are the functional proof. */
	X6886_LOG("clocks present topckgen=0x%x idx=0x%x infracfg=0x%x idx=0x%x "
	    "apmixed=0x%x idx=0x%x phy_ref=%uHz",
	    X6886_TOPCKGEN_PHYS, X6886_CLK_REF_IDX, X6886_INFRACFG_PHYS,
	    X6886_CLK_SYS_IDX, X6886_APMIXED_PHYS, X6886_CLK_SRC_IDX,
	    X6886_CLK_PHY_REF_HZ);

	/* vusb is regulator-always-on (fdt.dts:3498-3504) and usb-otg-vbus belongs
	 * to a disabled node (fdt.dts:4919-4929); no regulator write is made. */
	return 0;
}

/* ---- real MediaTek T-PHY v2 USB2 init (phy-mtk-tphy.c:1303-1410) -------------- */

static int
x6886_usb_phy_init(void)
{
	vm_offset_t com = phy_mmio + U3P_U2PHY_COM;
	uint32_t v, dtm1;
	unsigned i;

	/* u2_phy_instance_init(): switch to USB, enable PLL (phy-mtk-tphy.c:1310-1342) */
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) &
	    ~(P2C_FORCE_UART_EN | P2C_FORCE_SUSPENDM));
	wr32(com, U3P_U2PHYDTM0, (rd32(com, U3P_U2PHYDTM0) & ~P2C_RG_XCVRSEL) |
	    (1u << 4));
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) & ~P2C_RG_DATAIN);
	wr32(com, U3P_U2PHYDTM1, rd32(com, U3P_U2PHYDTM1) & ~P2C_RG_UART_EN);
	wr32(com, U3P_USBPHYACR0, rd32(com, U3P_USBPHYACR0) | PA0_RG_USB20_INTR_EN);
	wr32(com, U3P_USBPHYACR5, rd32(com, U3P_USBPHYACR5) & ~PA5_RG_U2_HS_100U_U3_EN);
	wr32(com, U3P_USBPHYACR4, rd32(com, U3P_USBPHYACR4) & ~P2C_U2_GPIO_CTR_MSK);
	wr32(com, U3P_USBPHYACR6, rd32(com, U3P_USBPHYACR6) & ~PA6_RG_U2_BC11_SW_EN);
	/* fdt.dts:7501 rx-sqth=5, :7502 discth=0x0c; fields from :96,99 */
	v = rd32(com, U3P_USBPHYACR6) & ~(PA6_RG_U2_SQTH | PA6_RG_U2_DISCTH);
	v |= (5u & 0xFu) | ((0x0cu & 0xFu) << 4);
	wr32(com, U3P_USBPHYACR6, v);

	/* u2_phy_instance_power_on() (phy-mtk-tphy.c:1357-1391) */
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) | P2C_FORCE_SUSPENDM);
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) & ~P2C_RG_SUSPENDM);
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) | P2C_RG_SUSPENDM);
	delay(30);
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) & ~P2C_FORCE_SUSPENDM);
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) & ~P2C_RG_SUSPENDM);
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) & ~P2C_FORCE_UART_EN);
	wr32(com, U3P_U2PHYDTM1, rd32(com, U3P_U2PHYDTM1) & ~P2C_RG_UART_EN);
	wr32(com, U3P_USBPHYACR4, rd32(com, U3P_USBPHYACR4) & ~P2C_U2_GPIO_CTR_MSK);
	wr32(com, U3P_U2PHYDTM0, rd32(com, U3P_U2PHYDTM0) & ~P2C_DTM0_PART_MASK);
	wr32(com, U3P_USBPHYACR6, rd32(com, U3P_USBPHYACR6) & ~PA6_RG_U2_BC11_SW_EN);
	wr32(com, U3P_USBPHYACR6, rd32(com, U3P_USBPHYACR6) | PA6_RG_U2_OTG_VBUSCMP_EN);
	wr32(com, U3P_U2PHYDTM1, (rd32(com, U3P_U2PHYDTM1) |
	    P2C_RG_VBUSVALID | P2C_RG_AVALID) & ~P2C_RG_SESSEND);
	/* fdt.dts:7510 mediatek,rev6 = 2; field from phy-mtk-tphy.c:86-87 */
	v = rd32(com, U3P_USBPHYACR6) & ~PA6_RG_U2_PHY_REV6;
	wr32(com, U3P_USBPHYACR6, v | ((2u & 0x3u) << 30));
	delay(800);

	/* bounded readiness readback: DTM1 VBUSVALID/AVALID set, SESSEND clear */
	for (i = 0; i < 64; i++) {
		dtm1 = rd32(com, U3P_U2PHYDTM1);
		if ((dtm1 & (P2C_RG_VBUSVALID | P2C_RG_AVALID)) ==
		    (P2C_RG_VBUSVALID | P2C_RG_AVALID) && !(dtm1 & P2C_RG_SESSEND)) {
			X6886_LOG("PHY ready dtm0=0x%08x dtm1=0x%08x acr6=0x%08x polls=%u",
			    rd32(com, U3P_U2PHYDTM0), dtm1, rd32(com, U3P_USBPHYACR6), i + 1);
			return 0;
		}
		delay(10);
	}
	X6886_LOG("PHY not ready dtm1=0x%08x after 64 polls", dtm1);
	return -ETIMEDOUT;
}

/* ---- MUSB controller reset and role detection -------------------------------- */

static int
x6886_usb_ctrl_ready(void)
{
	uint16_t hwvers, swrst;
	unsigned i;

	/* Presence proof before any functional write: HWVERS at 0x6c
	 * (mtk_musb_reg.h:243, musb_core.c:1896). 0x0000/0xffff means no clock. */
	hwvers = rd16(musb_mmio, MUSB_HWVERS);
	for (i = 0; i < 32 && (hwvers == 0 || hwvers == 0xffff); i++) {
		delay(100);
		hwvers = rd16(musb_mmio, MUSB_HWVERS);
	}
	if (hwvers == 0 || hwvers == 0xffff) {
		return -ENODEV;
	}

	/* MTK software reset (musb_core.c:4218-4220): DISUSBRESET | SWRST */
	swrst = rd16(musb_mmio, MUSB_SWRST);
	wr16(musb_mmio, MUSB_SWRST, swrst | MUSB_SWRST_DISUSBRESET | MUSB_SWRST_SWRST);
	delay(100);
	for (i = 0; i < 64; i++) {
		if (rd16(musb_mmio, MUSB_HWVERS) == hwvers) {
			break;
		}
		delay(10);
	}
	if (i == 64) {
		return -EIO;
	}

	X6886_LOG("MUSB ready hwvers=0x%04x cfg=0x%02x irq=%u polls=%u",
	    hwvers, rd8(musb_mmio, MUSB_EP_FIFOSZ), X6886_USB_IRQ, i + 1);
	return 0;
}

static int
x6886_usb_detect_role(void)
{
	uint8_t devctl = rd8(musb_mmio, MUSB_DEVCTL);

	/* musb_core.c:3718-3725: BDEVICE -> device (B_IDLE), else A/host.
	 * musb_core.c:1981: HM bit also reports host. */
	X6886_LOG("DEVCTL=0x%02x BDEVICE=%u HM=%u VBUS=%u (dr_mode=otg fdt.dts:7468)",
	    devctl, (devctl & MUSB_DEVCTL_BDEVICE) ? 1 : 0,
	    (devctl & MUSB_DEVCTL_HM) ? 1 : 0,
	    (devctl & MUSB_DEVCTL_VBUS) >> MUSB_DEVCTL_VBUS_SHIFT);

	if (!(devctl & MUSB_DEVCTL_BDEVICE) || (devctl & MUSB_DEVCTL_HM)) {
		X6886_LOG("A-device/host role: bulk debug channel not started");
		x6886_usb_peripheral = false;
		return -EOPNOTSUPP;
	}
	x6886_usb_peripheral = true;
	return 0;
}

/* ---- peripheral bulk endpoint (EP1 IN/OUT) ----------------------------------- */

static int
x6886_usb_gadget_setup(void)
{
	uint8_t fifosz, type;

	if (X6886_USB_EP >= X6886_USB_NUM_EPS) {
		return -EINVAL;
	}

	/* INDEX selects the endpoint register window: musb_core.h:190-192,
	 * mtk_musb_reg.h:211,277-279. */
	wr8(musb_mmio, MUSB_INDEX, (uint8_t)X6886_USB_EP);
	fifosz = rd8(musb_mmio, MUSB_EP_FIFOSZ);
	if (fifosz == 0) {
		X6886_LOG("EP%u absent (FIFOSIZE=0x%02x)", X6886_USB_EP, fifosz);
		return -ENODEV;
	}
	if ((fifosz & MUSB_FIFOSZ_SIZE) < X6886_USB_FIFO_SIZE_CODE) {
		X6886_LOG("EP%u FIFO too small (FIFOSIZE=0x%02x)", X6886_USB_EP, fifosz);
		return -ENODEV;
	}

	/* 64-byte max packet, bulk protocol: mtk_musb_reg.h:261-272,109-110 */
	wr16(musb_mmio, MUSB_TXMAXP, X6886_USB_MAX_PACKET);
	wr16(musb_mmio, MUSB_RXMAXP, X6886_USB_MAX_PACKET);
	type = (MUSB_TYPE_PROTO_BULK << MUSB_TYPE_PROTO_SHIFT);
	wr8(musb_mmio, MUSB_TXTYPE, type);
	wr8(musb_mmio, MUSB_RXTYPE, type);

	/* Double-buffered 64-byte FIFO: mtk_musb_reg.h:71-73,237-238 */
	wr8(musb_mmio, MUSB_TXFIFOSZ, MUSB_FIFOSZ_DPB | X6886_USB_FIFO_SIZE_CODE);
	wr8(musb_mmio, MUSB_RXFIFOSZ, MUSB_FIFOSZ_DPB | X6886_USB_FIFO_SIZE_CODE);

	/* EP1 interrupt enables + USB reset/connect/disconnect:
	 * mtk_musb_reg.h:204-209,28-36; musb_core.c:1270-1283 */
	wr16(musb_mmio, MUSB_INTRTXE, (uint16_t)(1u << X6886_USB_EP));
	wr16(musb_mmio, MUSB_INTRRXE, (uint16_t)(1u << X6886_USB_EP));
	wr8(musb_mmio, MUSB_INTRUSBE, MUSB_INTR_RESET | MUSB_INTR_CONNECT |
	    MUSB_INTR_DISCONNECT | MUSB_INTR_SUSPEND | MUSB_INTR_RESUME);
	wr16(musb_mmio, MUSB_INTRTX, 0xffff);
	wr16(musb_mmio, MUSB_INTRRX, 0xffff);
	wr8(musb_mmio, MUSB_INTRUSB, 0xef);      /* musb_core.c:1342 */

	/* Keep SOF on noise (musb_core.c:1293-1296) then soft-connect,
	 * high-speed enable and suspend-enable (musb_core.c:1301-1309). */
	wr8(musb_mmio, MUSB_ULPI_REG_DATA,
	    (uint8_t)((rd8(musb_mmio, MUSB_ULPI_REG_DATA) | 0x80u) & 0xbfu));
	wr8(musb_mmio, MUSB_POWER, MUSB_POWER_ENSUSPEND | MUSB_POWER_HSENAB |
	    MUSB_POWER_SOFTCONN);
	if (!(rd8(musb_mmio, MUSB_POWER) & MUSB_POWER_SOFTCONN)) {
		return -EIO;
	}

	X6886_LOG("gadget bulk EP%u ready fifosz=0x%02x txtype=0x%02x power=0x%02x",
	    X6886_USB_EP, fifosz, type, rd8(musb_mmio, MUSB_POWER));
	return 0;
}

static void
x6886_usb_fifo_write(uint32_t ep, uint16_t len, const uint8_t *src)
{
	vm_offset_t fifo = musb_mmio + MUSB_FIFO_OFFSET(ep);
	uint16_t i = 0;

	/* PIO width strategy from musb_core.c:401-426 */
	if (len >= 4 && (((uintptr_t)src & 3u) == 0)) {
		for (; i + 4 <= len; i += 4) {
			wr32(fifo, 0, *(const volatile uint32_t *)(src + i));
		}
	}
	if (len - i >= 2 && (((uintptr_t)src & 1u) == 0)) {
		for (; i + 2 <= len; i += 2) {
			wr16(fifo, 0, *(const volatile uint16_t *)(src + i));
		}
	}
	for (; i < len; i++) {
		wr8(fifo, 0, src[i]);
	}
}

static void
x6886_usb_fifo_read(uint32_t ep, uint16_t len, uint8_t *dst)
{
	vm_offset_t fifo = musb_mmio + MUSB_FIFO_OFFSET(ep);
	uint16_t i = 0;

	if (len >= 4 && (((uintptr_t)dst & 3u) == 0)) {
		for (; i + 4 <= len; i += 4) {
			*(volatile uint32_t *)(dst + i) = rd32(fifo, 0);
		}
	}
	if (len - i >= 2 && (((uintptr_t)dst & 1u) == 0)) {
		for (; i + 2 <= len; i += 2) {
			*(volatile uint16_t *)(dst + i) = rd16(fifo, 0);
		}
	}
	for (; i < len; i++) {
		dst[i] = rd8(fifo, 0);
	}
}

/* ---- public channel API ------------------------------------------------------ */

int
x6886_usb_channel_send(const uint8_t *data, unsigned length)
{
	uint16_t csr;
	unsigned i;

	if (!x6886_usb_ready || !x6886_usb_peripheral || data == NULL ||
	    length == 0 || length > X6886_USB_MAX_PACKET) {
		return -ENODEV;
	}
	wr8(musb_mmio, MUSB_INDEX, (uint8_t)X6886_USB_EP);
	for (i = 0; i < x6886_usb_spin; i++) {
		csr = rd16(musb_mmio, MUSB_TXCSR);
		if (!(csr & MUSB_TXCSR_TXPKTRDY)) {
			break;
		}
		delay(10);
	}
	if (i == x6886_usb_spin) {
		x6886_usb_fail(X6886_STAGE_BULK, -EBUSY);
		return -EBUSY;
	}
	x6886_usb_fifo_write(X6886_USB_EP, (uint16_t)length, data);
	csr = rd16(musb_mmio, MUSB_TXCSR);
	wr16(musb_mmio, MUSB_TXCSR, (uint16_t)(csr | MUSB_TXCSR_TXPKTRDY));
	x6886_usb_notify_data(data, length);
	return 0;
}

int
x6886_usb_channel_poll(void)
{
	uint8_t staged[X6886_USB_MAX_PACKET];
	uint16_t csr, count;
	unsigned i, room, tail;

	if (!x6886_usb_ready || !x6886_usb_peripheral) {
		return -ENODEV;
	}
	wr8(musb_mmio, MUSB_INDEX, (uint8_t)X6886_USB_EP);
	for (i = 0; i < x6886_usb_spin; i++) {
		csr = rd16(musb_mmio, MUSB_RXCSR);
		if (csr & MUSB_RXCSR_RXPKTRDY) {
			break;
		}
		delay(10);
	}
	if (i == x6886_usb_spin) {
		return 0;
	}
	count = rd16(musb_mmio, MUSB_RXCOUNT);
	if (count == 0) {
		return 0;
	}
	if (count > X6886_USB_MAX_PACKET) {
		x6886_usb_fail(X6886_STAGE_BULK, -EIO);
		return -EIO;
	}
	room = X6886_USB_RXQ - x6886_usb_rxq_count;
	if (count > room) {
		return -ENOSPC;
	}
	x6886_usb_fifo_read(X6886_USB_EP, count, staged);
	tail = x6886_usb_rxq_head + x6886_usb_rxq_count;
	for (i = 0; i < count; i++) {
		x6886_usb_rxq[(tail + i) % X6886_USB_RXQ] = staged[i];
	}
	x6886_usb_rxq_count += count;
	wr16(musb_mmio, MUSB_RXCSR, (uint16_t)(csr | MUSB_RXCSR_AUTOCLEAR));
	x6886_usb_connected = true;
	x6886_usb_notify_data(&x6886_usb_rxq[x6886_usb_rxq_head], count);
	return (int)count;
}

int
x6886_usb_channel_receive(uint8_t *dst, unsigned capacity)
{
	unsigned n;

	if (dst == NULL || capacity == 0) {
		return -EINVAL;
	}
	n = x6886_usb_rxq_count < capacity ? x6886_usb_rxq_count : capacity;
	for (unsigned i = 0; i < n; i++) {
		dst[i] = x6886_usb_rxq[(x6886_usb_rxq_head + i) % X6886_USB_RXQ];
	}
	x6886_usb_rxq_head = (x6886_usb_rxq_head + n) % X6886_USB_RXQ;
	x6886_usb_rxq_count -= n;
	return (int)n;
}

void
x6886_usb_stop(void)
{
	if (musb_mmio) {
		/* Disable interrupts and drop soft-connect (musb_core.c:1326-1331,1354-1356) */
		wr8(musb_mmio, MUSB_INTRUSBE, 0);
		wr16(musb_mmio, MUSB_INTRTXE, 0);
		wr16(musb_mmio, MUSB_INTRRXE, 0);
		wr8(musb_mmio, MUSB_POWER, MUSB_POWER_ENSUSPEND);
	}
	x6886_usb_ready = false;
	x6886_usb_peripheral = false;
	x6886_usb_connected = false;
	x6886_usb_rxq_head = 0;
	x6886_usb_rxq_count = 0;
	x6886_usb_unmap();
	X6886_LOG("stopped, all MMIO unmapped");
	if (x6886_usb_pstore_va) {
		ml_io_unmap(x6886_usb_pstore_va, X6886_RAMOOPS_SIZE);
		x6886_usb_pstore_va = 0;
	}
}

int
x6886_usb_init(void)
{
	int step = 0, status;
	int spin = (int)X6886_USB_SPIN_MAX;

	if (!PE_parse_boot_argn("x6886_usb", &step, sizeof(step)) || step <= 0) {
		return 0;
	}
	if (PE_parse_boot_argn("x6886_usb_spin", &spin, sizeof(spin))) {
		if (spin < (int)X6886_USB_SPIN_MIN || spin > (int)X6886_USB_SPIN_CAP) {
			X6886_LOG("x6886_usb_spin=%d outside %u..%u; rejected", spin,
			    X6886_USB_SPIN_MIN, X6886_USB_SPIN_CAP);
			return -EINVAL;
		}
		x6886_usb_spin = (uint32_t)spin;
	}
	if (step > 2) {
		X6886_LOG("x6886_usb=%d outside 0..2; rejected", step);
		return -EINVAL;
	}

	x6886_usb_pstore_map();
	X6886_LOG("start step=%d musb=0x%x phy=0x%x irq=%u spin=%u", step,
	    X6886_MUSB_PHYS, X6886_PHY_PHYS, X6886_USB_IRQ, x6886_usb_spin);

	status = x6886_usb_clocks_ready();
	if (status != 0) {
		x6886_usb_fail(X6886_STAGE_CLOCK, status);
		x6886_usb_stop();
		return status;
	}
	phy_mmio = ml_io_map(X6886_PHY_PHYS, X6886_PHY_SIZE);
	musb_mmio = ml_io_map(X6886_MUSB_PHYS, X6886_MUSB_SIZE);
	if (phy_mmio == 0 || musb_mmio == 0) {
		x6886_usb_fail(X6886_STAGE_MAP, -ENODEV);
		x6886_usb_stop();
		return -ENODEV;
	}

	status = x6886_usb_phy_init();
	if (status != 0) {
		x6886_usb_fail(X6886_STAGE_PHY, status);
		x6886_usb_stop();
		return status;
	}
	status = x6886_usb_ctrl_ready();
	if (status != 0) {
		x6886_usb_fail(X6886_STAGE_RESET, status);
		x6886_usb_stop();
		return status;
	}
	status = x6886_usb_detect_role();
	if (status != 0) {
		x6886_usb_fail(X6886_STAGE_MODE, status);
		x6886_usb_stop();
		return status;
	}
	if (step < 2) {
		X6886_LOG("readiness only (x6886_usb=1); no endpoint configured");
		x6886_usb_stop();
		return 0;
	}

	status = x6886_usb_gadget_setup();
	if (status != 0) {
		x6886_usb_fail(X6886_STAGE_EP, status);
		x6886_usb_stop();
		return status;
	}
	x6886_usb_ready = true;
	X6886_LOG("ready: MUSB peripheral bulk EP%u, no ADB, notification hook %s",
	    X6886_USB_EP, x6886_usb_notify_data ? "present" : "missing");
	return 0;
}
