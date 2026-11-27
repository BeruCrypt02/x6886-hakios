/* Offline verification of build/boot-hakios-x6886-final.img plus the loader ELF it was built from. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define IMAGE "/home/berucrypt00/x6886-hakios/build/boot-HakiOS-X6886.img"
#define LOADER "/home/berucrypt00/x6886-hakios/devices/x6886/prebuilt/mtkloader.bin"
#define LOADER_ELF "/tmp/opencode/elf-final2/mtkloader.elf"
#define PAGE 4096u
#define IMAGE_SIZE (64u * 1024u * 1024u)
#define CHAIN_OFFSET 0x80000u

static unsigned char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *b = malloc((size_t)n);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
	fclose(f);
	*len = (size_t)n;
	return b;
}

static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const unsigned char *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }

static unsigned char *gunzip(const unsigned char *p, size_t n, size_t *outlen)
{
	z_stream s;
	memset(&s, 0, sizeof(s));
	if (inflateInit2(&s, 15 + 32) != Z_OK) return NULL;
	size_t cap = 1u << 26;
	unsigned char *out = malloc(cap);
	s.next_in = (Bytef *)p;
	s.avail_in = (uInt)n;
	s.next_out = out;
	s.avail_out = (uInt)cap;
	int rc = inflate(&s, Z_FINISH);
	if (rc != Z_STREAM_END) { free(out); out = NULL; }
	else *outlen = s.total_out;
	inflateEnd(&s);
	return out;
}

/* ELF64 little-endian helpers (the loader is built locally by llvm-ld.lld). */
static uint16_t e16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t e32(const unsigned char *p) { return le32(p); }
static uint64_t e64(const unsigned char *p) { return le64(p); }

struct elf {
	const unsigned char *d;
	size_t n;
	uint64_t shoff;
	uint16_t shentsize, shnum, shstrndx;
};

static const unsigned char *shdr(const struct elf *e, uint16_t i) { return e->d + e->shoff + (size_t)i * e->shentsize; }
static const char *shname(const struct elf *e, const unsigned char *s) { return (const char *)e->d + e64(shdr(e, e->shstrndx) + 24) + e32(s); }
static const unsigned char *section(const struct elf *e, const char *name, uint64_t *size)
{
	for (uint16_t i = 0; i < e->shnum; i++) {
		const unsigned char *s = shdr(e, i);
		if (!strcmp(shname(e, s), name)) { if (size) *size = e64(s + 32); return e->d + e64(s + 24); }
	}
	return NULL;
}
static int symbol(const struct elf *e, const char *name, uint64_t *value)
{
	for (uint16_t i = 0; i < e->shnum; i++) {
		const unsigned char *s = shdr(e, i);
		if (e32(s + 4) != 2u) continue; /* SHT_SYMTAB */
		uint64_t stroff = e64(s + 24), symoff = e64(s + 24), strsz = 0;
		uint64_t link = e32(s + 40);
		if (link < e->shnum) { strsz = e64(shdr(e, (uint16_t)link) + 32); stroff = e64(shdr(e, (uint16_t)link) + 24); }
		uint64_t count = e64(s + 32) / e64(s + 56);
		for (uint64_t k = 0; k < count; k++) {
			const unsigned char *sym = e->d + symoff + k * e64(s + 56);
			uint32_t noff = e32(sym);
			if (noff < strsz && !strcmp((const char *)e->d + stroff + noff, name)) { *value = e64(sym + 8); return 1; }
		}
	}
	return 0;
}
static int contains(const unsigned char *hay, size_t hn, const unsigned char *needle, size_t nn)
{
	if (nn == 0 || nn > hn) return 0;
	for (size_t i = 0; i + nn <= hn; i++) if (!memcmp(hay + i, needle, nn)) return 1;
	return 0;
}
static int u32count(const unsigned char *hay, size_t hn, uint32_t v)
{
	unsigned char p[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
	int c = 0;
	for (size_t i = 0; i + 4 <= hn; i++) if (!memcmp(hay + i, p, 4)) c++;
	return c;
}

int main(void)
{
	int pass = 0, fail = 0;
#define CHECK(n, ok, ...) do { if (ok) { pass++; printf("PASS %2d ", n); } else { fail++; printf("FAIL %2d ", n); } printf(__VA_ARGS__); printf("\n"); } while (0)

	size_t imgn = 0, loadern = 0, elfn = 0;
	unsigned char *img = slurp(IMAGE, &imgn), *loader = slurp(LOADER, &loadern), *elf = slurp(LOADER_ELF, &elfn);
	if (!img || !loader || !elf) { fprintf(stderr, "cannot read inputs\n"); return 2; }

	/* 1: AOSP boot image header v4. */
	CHECK(1, imgn > PAGE && !memcmp(img, "ANDROID!", 8) && le32(img + 0x28) == 4,
	      "header v%u magic %.8s", le32(img + 0x28), (char *)img);

	/* 2: page 4096 (v4 keeps no page field; header fits a page, kernel starts on one). */
	CHECK(2, le32(img + 0x14) <= PAGE && (imgn % 4096u) == 0 && (le32(img + 0x08) > 0),
	      "header_size=%u kernel_size=%u page=%u", le32(img + 0x14), le32(img + 0x08), PAGE);

	/* 3: the whole 64 MiB boot partition. */
	CHECK(3, imgn == IMAGE_SIZE, "image=%zu bytes", imgn);

	size_t blogn = 0;
	unsigned char *blob = gunzip(img + PAGE, le32(img + 0x08), &blogn);
	if (!blob) { fprintf(stderr, "kernel gzip failed\n"); return 2; }

	/* 4: arm64 Linux Image, text_offset zero. */
	CHECK(4, blogn >= 64 && !memcmp(blob + 0x38, "ARM\x64", 4) && le64(blob + 0x08) == 0,
	      "magic=%.4s text_offset=%" PRIu64, (char *)(blob + 0x38), le64(blob + 0x08));

	/* 5: res2/res3 point at a well-formed XNUP container. */
	uint64_t off = le64(blob + 0x30);
	int xnup = off != 0 && off + 8 <= blogn && le32(blob + (size_t)off) == 0x50554e58u;
	int entries = 0;
	if (xnup) {
		size_t pos = (size_t)off + 8;
		while (pos + 24 <= blogn) {
			uint32_t sz = le32(blob + pos + 16);
			if (!blob[pos]) { entries++; break; }
			entries++;
			pos += 24 + sz + ((-sz) & 15u);
		}
	}
	CHECK(5, xnup && le64(blob + 0x28) == off - CHAIN_OFFSET && entries == 3,
	      "res2=%" PRIx64 " res3=%" PRIx64 " XNUP=%d entries=%d", le64(blob + 0x28), off, xnup, entries);

	/* 6: the raw loader in the image is exactly the verified mtkloader.bin. */
	int loader_same = loadern <= CHAIN_OFFSET && blogn >= loadern && !memcmp(blob, loader, 0x10) &&
		!memcmp(blob + 0x38, loader + 0x38, loadern - 0x38);
	CHECK(6, loader_same && le64(blob + 0x10) >= blogn && le64(blob + 0x10) <= IMAGE_SIZE,
	      "loader=%zu bytes identical except written res fields", loadern);

	struct elf e = { elf, elfn, 0, 0, 0, 0 };
	if (elfn >= 64 && !memcmp(elf, "\177ELF", 4) && elf[4] == 2 && elf[5] == 1) {
		e.shoff = e64(elf + 40); e.shentsize = e16(elf + 58); e.shnum = e16(elf + 60); e.shstrndx = e16(elf + 62);
	}

	/* 7: no relocations, therefore no R_AARCH64_ABS64. */
	int relocs = 0, abs64 = 0;
	if (e.shnum && e.shoff + (size_t)e.shnum * e.shentsize <= elfn) {
		for (uint16_t i = 0; i < e.shnum; i++) {
			const unsigned char *s = shdr(&e, i);
			uint32_t type = e32(s + 4);
			if (type == 4u || type == 9u) {
				uint64_t n = e64(s + 32) / e64(s + 56);
				relocs += (int)n;
				for (uint64_t k = 0; k < n; k++) {
					const unsigned char *r = elf + e64(s + 24) + k * e64(s + 56);
					if ((e64(r + 8) & 0xffffffffu) == 0x102u) abs64++;
				}
			}
		}
	}
	CHECK(7, e.shnum > 0 && relocs == 0 && abs64 == 0, "relocations=%d ABS64=%d", relocs, abs64);

	/* 8: one GOT slot. */
	uint64_t gotsz = 0;
	const unsigned char *got = e.shnum ? section(&e, ".got", &gotsz) : NULL;
	CHECK(8, got != NULL && gotsz == 8, ".got=%" PRIu64 " bytes (%llu slots)", gotsz, (unsigned long long)(gotsz / 8));

	/* 9: every stack lives in .bss above _start, never below it. */
	uint64_t start = 0, bss = 0, bssend = 0, stack = 0;
	int stacks = 0;
	if (e.shnum) {
		section(&e, ".bss", &bssend);
		symbol(&e, "_start", &start); symbol(&e, "__bss_start", &bss);
		const char *names[] = { "primary_stack_top", "fault_stack_top", "watcher_stack_top", "xnu_fault_stack_top" };
		for (unsigned i = 0; i < 4; i++) if (symbol(&e, names[i], &stack) && stack > start && stack >= bss && stack <= bss + bssend) stacks++;
	}
	CHECK(9, stacks == 4 && bss > start, "_start=%" PRIx64 " bss=%" PRIx64 "..%" PRIx64 " stacks=%d/4", start, bss, bss + bssend, stacks);

	/* 10: the handoff VBAR table has sixteen 0x80-byte vectors. */
	uint64_t vec = 0;
	int vectors = 0;
	if (e.shnum && symbol(&e, "xnu_vectors", &vec)) {
		uint64_t textva = 0, textoff = 0;
		const unsigned char *text = section(&e, ".text", NULL);
		for (uint16_t i = 0; i < e.shnum; i++) {
			const unsigned char *s = shdr(&e, i);
			if (!strcmp(shname(&e, s), ".text")) { textva = e64(s + 16); textoff = e64(s + 24); }
		}
		if (text && vec >= textva) {
			size_t base = (size_t)(textoff + (vec - textva));
			for (unsigned i = 0; i < 16 && base + i * 0x80u + 8 <= elfn; i++) {
				uint32_t w1 = e32(elf + base + i * 0x80u), w2 = e32(elf + base + i * 0x80u + 4);
				if (w1 == (0xd2800000u | (i << 12)) && (w2 >> 24) == 0x17u) vectors++;
			}
		}
	}
	CHECK(10, vectors == 16, "xnu_vectors=%" PRIx64 " valid_vectors=%d/16", vec, vectors);

	/* 11: ramoops 0x480af000 is built in the linked kernel as 0x48000000 + 0xaf000. */
	size_t pos = (size_t)off + 8;
	unsigned char *k = NULL;
	size_t kn = 0;
	while (pos + 24 <= blogn) {
		uint32_t sz = le32(blob + pos + 16);
		if (!blob[pos]) break;
		if (le32(blob + pos) == 0x6e72656bU) { k = blob + pos + 24; kn = sz; } /* "kern" */
		pos += 24 + sz + ((-sz) & 15u);
	}
	int ramoops = k && u32count(k, kn, 0x9142bea0u) >= 1 &&
		(u32count(k, kn, 0x52a9001au) + u32count(k, kn, 0x52a9000au)) >= 1;
	CHECK(11, ramoops, "kernel=0x48000000+0xaf000 instructions present");

	/* 12: the loader knows the phone's UFS root and arms TOPRGU. */
	int ufs = contains(loader, loadern, (const unsigned char *)"ufs_darwin", 10);
	int wdt = contains(loader, loadern, (const unsigned char *)"TOPRGU", 6) && contains(loader, loadern, (const unsigned char *)"watchdog", 8);
	CHECK(12, ufs && wdt, "ufs_darwin=%d TOPRGU/watchdog=%d", ufs, wdt);

	/* 13: the loader jumps straight into a Mach-O kernel, with no stale AVB footer. */
	int macho = k && kn >= 4 && le32(k) == 0xfeedfacfu;
	int avb = !contains(img, imgn, (const unsigned char *)"AVBf", 4) && !contains(img, imgn, (const unsigned char *)"AVB0", 4);
	int tailzero = 1;
	for (size_t i = imgn - 64; i < imgn; i++) if (img[i]) tailzero = 0;
	CHECK(13, macho && avb && tailzero, "MH_MAGIC_64=%d no_AVB=%d tail_zero=%d", macho, avb, tailzero);

	printf("\n%d/%d checks passed\n", pass, pass + fail);
	return fail ? 1 : 0;
}
