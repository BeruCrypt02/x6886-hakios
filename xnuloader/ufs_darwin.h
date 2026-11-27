/* /dev/ufs_darwin: publish stock "super" to XNU's UFS block client.
 *
 * The layout below is the one in refs/xnu-12377-hakios/bsd/dev/board/ufs.c.
 * That driver maps the page from ufs_shared_pa= and creates ufs_<name> for
 * every partition the loader lists, so the partition named "darwin" becomes
 * /dev/ufs_darwin. It checks magic, version, size and parts before using it.
 *
 * The server core stays in the loader while XNU runs on core 0. Both cores map
 * DRAM 1:1 and inner-shareable; every field the other core writes is cleaned
 * and invalidated before it is read, and every field this core writes is
 * cleaned to the point of coherency before request/answered is raised.
 */
#ifndef UFS_DARWIN_H
#define UFS_DARWIN_H

#include <stdint.h>

extern void mmu_on(uint64_t l1_table);
extern void dcache_clean_range(uint64_t va, uint64_t len);
extern void dcache_inval_range(uint64_t va, uint64_t len);
extern void block_server_entry(void);

#define UFS_DARWIN_BLOCK_PAGE   0x4000u
#define UFS_DARWIN_BLOCK_MAGIC  0x4b434f4c42574452ULL   /* "RDWBLOCK" */
#define UFS_DARWIN_BLOCK_VERSION 3u
#define UFS_DARWIN_MAX_PARTS    160u
#define UFS_DARWIN_OP_READ      0u
#define UFS_DARWIN_BUFFER_BYTES (64u << 10)             /* 16 blocks of 4096 */

struct ufs_darwin_part {
	char name[36];
	uint32_t block_size;
	uint64_t blocks;
	uint64_t start_lba;
	uint16_t unit;
	uint8_t lun;
	uint8_t flags;
	uint32_t reserved;
};

struct ufs_darwin_block {
	uint64_t magic;
	uint32_t version, size;
	uint32_t parts, buffer_size;
	uint64_t buffer_pa;
	uint32_t write_part_plus_one, write_reserved;
	uint64_t write_first;
	struct {
		volatile uint32_t request;
		uint32_t part;
		uint64_t lba, count;
		uint32_t op, reserved;
	} ask __attribute__((aligned(128)));
	struct {
		volatile uint32_t answered;
		uint32_t reserved;
		int64_t status;
		uint64_t served, failed, bytes, ticks;
	} answer __attribute__((aligned(128)));
	struct ufs_darwin_part part[UFS_DARWIN_MAX_PARTS] __attribute__((aligned(128)));
	struct {
		uint64_t hc_pa, area_pa;
		uint32_t area_size, ucd_offset, ucd_size, slots;
		uint32_t uefi_utrlba, uefi_utrlbau;
	} native __attribute__((aligned(128)));
	struct {
		volatile uint32_t take_back;
	} loader_native __attribute__((aligned(128)));
	struct {
		volatile uint32_t list_in;
		uint32_t switched_in, switched_out;
		volatile uint32_t cnr_at_switch;
	} kernel_native __attribute__((aligned(128)));
};

_Static_assert(__builtin_offsetof(struct ufs_darwin_block, ask) == 128,
    "the shared page must match the kernel's struct loader_block");
_Static_assert(sizeof(struct ufs_darwin_block) <= UFS_DARWIN_BLOCK_PAGE,
    "the shared page must fit in one 16 KiB page");

static struct ufs_darwin_block ufs_darwin_page __attribute__((aligned(UFS_DARWIN_BLOCK_PAGE)));
static uint8_t ufs_darwin_buffer[UFS_DARWIN_BUFFER_BYTES] __attribute__((aligned(4096)));
static uint64_t ufs_darwin_lba, ufs_darwin_blocks;
static volatile uint64_t ufs_darwin_started __attribute__((aligned(64)));
static uint64_t ufs_darwin_l1[512] __attribute__((aligned(4096)));

/* Filled only after super_apfs_probe() has seen "NXSB" at LBA 0 + 0x20. */
static void ufs_darwin_publish(uint64_t lba, uint64_t blocks)
{
	struct ufs_darwin_block *p = &ufs_darwin_page;

	ufs_darwin_lba = lba;
	ufs_darwin_blocks = blocks;
	for (uint32_t w = 0; w < sizeof(*p) / 4u; w++) {
		((volatile uint32_t *)p)[w] = 0;
	}
	for (uint32_t i = 0; i < UFS_DARWIN_BUFFER_BYTES / 4u; i++) {
		((volatile uint32_t *)ufs_darwin_buffer)[i] = 0;
	}
	p->magic = UFS_DARWIN_BLOCK_MAGIC;
	p->version = UFS_DARWIN_BLOCK_VERSION;
	p->size = sizeof(*p);
	p->parts = 1;
	p->buffer_size = UFS_DARWIN_BUFFER_BYTES;
	p->buffer_pa = (uint64_t)(uintptr_t)ufs_darwin_buffer;
	p->ask.request = 0;
	p->answer.answered = 0;
	p->part[0].block_size = UFS_BLOCK_BYTES;
	p->part[0].blocks = blocks;
	p->part[0].start_lba = 0;       /* the APFS container starts at the partition's LBA 0 */
	p->part[0].unit = 0;
	p->part[0].lun = UFS_MAIN_LUN;
	p->part[0].flags = 0;           /* a partition, not a whole unit */
	for (uint32_t i = 0; i < 7; i++) {
		p->part[0].name[i] = "darwin"[i];
	}
	dcache_clean_range((uint64_t)(uintptr_t)p, sizeof(*p));
}

static uint64_t ufs_darwin_page_pa(void)
{
	return (uint64_t)(uintptr_t)&ufs_darwin_page;
}

/* Entry from start.S: 1:1 inner-shareable mapping, then serve until XNU ends. */
void block_server_main(void)
{
	struct ufs_darwin_block *p = &ufs_darwin_page;
	uint32_t served = 0;

	for (uint32_t i = 0; i < 512; i++) {
		uint64_t pa = (uint64_t)i << 30;
		if (i == 0) {
			ufs_darwin_l1[i] = pa | (1u << 10) | 1u;                        /* device */
		} else if (i < 8) {
			ufs_darwin_l1[i] = pa | (1u << 10) | (3u << 8) | (1u << 2) | 1u; /* WB, inner-shareable */
		}
	}
	mmu_on((uint64_t)(uintptr_t)ufs_darwin_l1);
	ufs_darwin_started = 1;
	dcache_clean_range((uint64_t)(uintptr_t)&ufs_darwin_started, 8);

	for (;;) {
		uint32_t req, part, op;
		uint64_t lba, count, bytes;
		int ok;

		/* XNU writes ask through an uncached mapping: invalidate, do not
		 * clean, or a stale loader cache line overwrites the request. */
		dcache_inval_range((uint64_t)(uintptr_t)&p->ask, sizeof(p->ask));
		req = p->ask.request;
		if (req == served) {
			__asm__ volatile("yield");
			continue;
		}
		part = p->ask.part;
		lba = p->ask.lba;
		count = p->ask.count;
		op = p->ask.op;
		bytes = count * UFS_BLOCK_BYTES;

		ok = 0;
		if (part == 0 && op == UFS_DARWIN_OP_READ && p->buffer_size >= bytes &&
		    bytes != 0 && ufs_darwin_blocks != 0 && lba <= ufs_darwin_blocks &&
		    count <= ufs_darwin_blocks - lba) {
			/* ufs_fail() disarms the list on the first transient error; restore
			 * the validated list and retry so one bad response cannot kill
			 * every later read from XNU. */
			for (uint32_t attempt = 0; attempt < 3 && !ok; attempt++) {
				if (!ufs_list_phys) {
					ufs_list_phys = ufs_gpt_list_phys;
				}
				ok = ufs_read_lun(UFS_MAIN_LUN, ufs_darwin_lba + lba,
				    (uint32_t)count, (void *)(uintptr_t)p->buffer_pa);
				if (!ok) {
					dbg_delay_ms(1);
				}
			}
		}
		if (ok) {
			dcache_clean_range(p->buffer_pa, bytes);
			p->answer.status = 0;
			p->answer.served = ++served;
			p->answer.bytes += bytes;
		} else {
			p->answer.status = -5;      /* EIO */
			p->answer.failed++;
			served = req;
		}
		dcache_clean_range((uint64_t)(uintptr_t)&p->answer, sizeof(p->answer));
		p->answer.answered = req;   /* raised last, after the data is visible */
		dcache_clean_range((uint64_t)(uintptr_t)&p->answer, sizeof(p->answer));
	}
}

#endif
