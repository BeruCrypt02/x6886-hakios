#pragma once
#include <stdint.h>

/*
 * X6886 LK framebuffer. The reserved region starts at 0xfe0c0000 (from
 * /proc/iomem). The panel is DSI video-mode (nt37706a ...vdo...), so the
 * framebuffer is scanned out continuously: no DSI frame push is needed.
 */
#define FB_REGION	0xfe0c0000UL
#define FB_REGION_SIZE	0x01f30000UL
#define FB_BASE		0xfe0c0000UL	/* LK framebuffer, handed to xnu */
#define FB_W		1080
#define FB_H		2400
#define FB_STRIDE	7680		/* LK pitch from board.toml */

#define RED	0xffff0000U
#define GREEN	0xff00ff00U
#define BLUE	0xff0000ffU

/* Fill the whole splash region, both layers. MMU off: Device stores. */
void fb_fill(uint32_t color);
/* One command-mode frame: MUTEX0_EN self-clears when the frame is done. */
void dsi_push_frame(void);
uint32_t dsi_intsta(void);
/*
 * Power on the last A76 (MPIDR 0x700, kept out of the ADT) to push a frame
 * every 33 ms and kick the MTK watchdog, which xnu knows nothing about.
 * After lifetime_s it resets the board so a hung boot falls back to Android.
 * Returns the PSCI status (0 = started).
 */
int64_t pump_start(unsigned lifetime_s);
