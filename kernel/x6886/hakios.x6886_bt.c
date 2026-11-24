/*
 * The Skywalk channel the system's bluetoothd reaches the Bluetooth controller by. Its HCI transport is "H4BC": a PF_SYSTEM socket
 * on the kernel control com.apple.uart.sk.bluetooth (bsd/dev/mtk_connsys.c), on which it sets a channel specification
 * (IOAOSSKYSETCHANNELSPEC) and asks for a channel's UUID (IOAOSSKYGETCHANNELUUID), and then os_channel_create()s the channel,
 * syncs its rings and waits for them on a kqueue. On a phone the other end is Apple's IOSkywalkFamily, a kernel pipe
 * (NEXUS_TYPE_KERNEL_PIPE) whose "driver" is the Bluetooth UART's; here the other end is the MediaTek controller behind
 * CONNSYS, and this is its provider, made by bsd/skywalk/nexus/kpipe/nx_kpipe_loopback.c's pattern:
 *   user TX ring  -> sync_tx -> the H4 bytes of each packet go to the registered CONNSYS backend
 *   controller    -> x6886_bt_deliver -> a queue -> sync_rx puts them in the user's RX ring, one H4 packet a slot
 * Everything the user does to the channel is said (hakios_host_keep_add), the first few times.
 *
 * X6886 adaptation: the H4/Skywalk transport is real (mtk_btsk.c), but the controller
 * backend is NOT sourced here: the H4 send op lives in mtk_connsys.c and the
 * power-on / WMT function-on / STP send-receive ops are a table the osexgang skeleton
 * mtk_bt_skel.c leaves empty (.name = NULL at mtk_bt_skel.c:105; "has not run on a
 * phone" at :20). No backend is invented in this file.
 *
 * Boot safety: default OFF. x6886_bt_init() returns ENXIO until the CONNSYS owner
 * registers a real H4 send backend with x6886_bt_backend_register(); no power-on,
 * WMT function-on or STP code is invented here. Bounded logging (200 lines) and a
 * bounded 16 KB RX queue; no panic, no guessed register.
 */
#include <sys/errno.h>
#include <kern/locks.h>
#include <kern/kalloc.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>
#include <sys/systm.h>
#include <sys/random.h>
#include <skywalk/os_skywalk_private.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

/* No backend is built in. The CONNSYS owner must register a real H4 send function. */
typedef void (*x6886_bt_send_fn)(const uint8_t *bytes, unsigned length);
static x6886_bt_send_fn x6886_bt_send_backend;
static bool x6886_bt_ready_gate;

void x6886_bt_backend_register(x6886_bt_send_fn send);

/* ---- ramoops pstore mirror (boot-audit GATE 4) ---- */
#define X6886_PSTORE_PHYS       0x480af000ULL
#define X6886_PSTORE_SIZE       0x1000u
#define X6886_PSTORE_SIG        0x43474244u

static vm_offset_t x6886_pstore_va;
static LCK_GRP_DECLARE(btsk_pstore_grp, "x6886-bt-pstore");
static LCK_SPIN_DECLARE(btsk_pstore_lock, &btsk_pstore_grp);

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
	lck_spin_lock(&btsk_pstore_lock);
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
	lck_spin_unlock(&btsk_pstore_lock);
}

#define X6886_LOG(fmt, ...) do {                                              \
	char x6886_log_buf[256];                                              \
	int x6886_log_len = snprintf(x6886_log_buf, sizeof(x6886_log_buf),    \
	    "[X6886-bt] " fmt, ##__VA_ARGS__);                                \
	printf("%s", x6886_log_buf);                                          \
	if (x6886_log_len > 0) {                                              \
		x6886_pstore_write(x6886_log_buf, (unsigned)x6886_log_len);       \
	}                                                                     \
} while (0)


static LCK_GRP_DECLARE(btsk_lck_grp, "hakios-btsk");
static LCK_MTX_DECLARE(btsk_lock, &btsk_lck_grp);

#define BTSK_RXQ        16384u
static uint8_t btsk_rxq[BTSK_RXQ];
static unsigned btsk_rxq_head, btsk_rxq_count;

static nexus_controller_t btsk_ncd;
static uuid_t btsk_dom_uuid, btsk_prov_uuid, btsk_nx_uuid;
static bool btsk_ready;
static uintptr_t btsk_nx_ctx;
static kern_channel_ring_t btsk_txring, btsk_rxring;
static kern_pbufpool_t btsk_tx_pp, btsk_rx_pp;
static uint32_t btsk_said;

#define SAY(...) do { if (btsk_said < 200) { btsk_said++; X6886_LOG(__VA_ARGS__); } } while (0)

static errno_t
btsk_dom_init(__unused kern_nexus_domain_provider_t domprov)
{
	return 0;
}

static void
btsk_dom_fini(__unused kern_nexus_domain_provider_t domprov)
{
}

static errno_t
btsk_pre_connect(__unused kern_nexus_provider_t nxprov, proc_t p, kern_nexus_t nexus, nexus_port_t port,
    kern_channel_t channel, void **ch_ctx)
{
	errno_t error = kern_nexus_get_pbufpool(nexus, &btsk_tx_pp, &btsk_rx_pp);

	*ch_ctx = channel;
	SAY("pre_connect by pid %d, port %u: pbufpools %p %p (%d)", proc_pid(p), port, btsk_tx_pp, btsk_rx_pp, error);
	return 0;
}

static errno_t
btsk_connected(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, __unused kern_channel_t channel)
{
	SAY("connected");
	return 0;
}

static void
btsk_pre_disconnect(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, __unused kern_channel_t channel)
{
	SAY("pre_disconnect");
}

static void
btsk_disconnected(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, __unused kern_channel_t channel)
{
	SAY("disconnected");
}

static errno_t
btsk_ring_init(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, __unused kern_channel_t channel,
    kern_channel_ring_t ring, boolean_t is_tx_ring, void **ring_ctx)
{
	lck_mtx_lock(&btsk_lock);
	if (is_tx_ring) {
		btsk_txring = ring;
	} else {
		btsk_rxring = ring;
	}
	lck_mtx_unlock(&btsk_lock);
	*ring_ctx = ring;
	SAY("%s ring made", is_tx_ring ? "TX" : "RX");
	return 0;
}

static void
btsk_ring_fini(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, kern_channel_ring_t ring)
{
	lck_mtx_lock(&btsk_lock);
	if (ring == btsk_txring) {
		btsk_txring = NULL;
	} else if (ring == btsk_rxring) {
		btsk_rxring = NULL;
	}
	lck_mtx_unlock(&btsk_lock);
}

static errno_t
btsk_slot_init(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, __unused kern_channel_ring_t ring,
    channel_slot_t slot, __unused uint32_t slot_id, struct kern_slot_prop **slot_prop_addr, void **pslot_ctx)
{
	*slot_prop_addr = NULL;
	*pslot_ctx = slot;
	return 0;
}

static void
btsk_slot_fini(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, __unused kern_channel_ring_t ring,
    __unused channel_slot_t slot, __unused uint32_t slot_id)
{
}

/* the user's packets (H4, one a slot) to the controller */
static errno_t
btsk_sync_tx(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, kern_channel_ring_t ring, __unused uint32_t flags)
{
	kern_channel_slot_t slot = kern_channel_get_next_slot(ring, NULL, NULL), last = NULL;

	while (slot != NULL) {
		kern_packet_t ph = kern_channel_slot_get_packet(ring, slot);

		if (ph != 0) {
			kern_buflet_t buf = kern_packet_get_next_buflet(ph, NULL);

			if (buf != NULL) {
				uint8_t *base = kern_buflet_get_data_address(buf);
				uint16_t length = kern_buflet_get_data_length(buf), offset = kern_buflet_get_data_offset(buf);

				if (base != NULL && length > 0) {
					static unsigned slots;

					if (++slots > 90 && slots < 200) {
						X6886_LOG("TX slot %u, %u bytes at offset %u: %02x %02x %02x %02x %02x %02x %02x %02x", slots, length,
						    offset, base[offset], length > 1 ? base[offset + 1] : 0, length > 2 ? base[offset + 2] : 0,
						    length > 3 ? base[offset + 3] : 0, length > 4 ? base[offset + 4] : 0, length > 5 ? base[offset + 5] : 0,
						    length > 6 ? base[offset + 6] : 0, length > 7 ? base[offset + 7] : 0);
					}
					if (x6886_bt_send_backend != NULL) {
						x6886_bt_send_backend(base + offset, length);
					} else {
						SAY("TX dropped: no H4 backend registered");
					}
				}
			}
			(void)kern_channel_slot_detach_packet(ring, slot, ph);
			if (btsk_tx_pp != NULL) {
				kern_pbufpool_free(btsk_tx_pp, ph);
			}
		}
		last = slot;
		slot = kern_channel_get_next_slot(ring, slot, NULL);
	}
	if (last != NULL) {
		kern_channel_advance_slot(ring, last);
	}
	(void)kern_channel_reclaim(ring);
	return 0;
}

/* the controller's bytes to the user's ring, an H4 packet a slot */
static errno_t
btsk_sync_rx(__unused kern_nexus_provider_t nxprov, __unused kern_nexus_t nexus, kern_channel_ring_t ring, __unused uint32_t flags)
{
	kern_channel_slot_t slot, last = NULL;

	(void)kern_channel_reclaim(ring);
	slot = kern_channel_get_next_slot(ring, NULL, NULL);
	lck_mtx_lock(&btsk_lock);
	while (slot != NULL && btsk_rxq_count > 0 && btsk_rx_pp != NULL) {
		kern_packet_t ph = 0;
		kern_buflet_t buf;
		uint8_t *base;
		unsigned room, n = btsk_rxq_count;

		if (kern_pbufpool_alloc_nosleep(btsk_rx_pp, 1, &ph) != 0 || ph == 0) {
			SAY("no packet for an RX slot");
			break;
		}
		buf = kern_packet_get_next_buflet(ph, NULL);
		base = buf != NULL ? kern_buflet_get_data_address(buf) : NULL;
		room = buf != NULL ? kern_buflet_get_data_limit(buf) : 0;
		if (base == NULL || room == 0) {
			kern_pbufpool_free(btsk_rx_pp, ph);
			break;
		}
		if (n > room) {
			n = room;
		}
		for (unsigned i = 0; i < n; i++) {
			base[i] = btsk_rxq[(btsk_rxq_head + i) % BTSK_RXQ];
		}
		btsk_rxq_head = (btsk_rxq_head + n) % BTSK_RXQ;
		btsk_rxq_count -= n;
		(void)kern_buflet_set_data_offset(buf, 0);
		(void)kern_buflet_set_data_length(buf, (uint16_t)n);
		(void)kern_packet_finalize(ph);
		if (kern_channel_slot_attach_packet(ring, slot, ph) != 0) {
			kern_pbufpool_free(btsk_rx_pp, ph);
			break;
		}
		last = slot;
		slot = kern_channel_get_next_slot(ring, slot, NULL);
	}
	lck_mtx_unlock(&btsk_lock);
	if (last != NULL) {
		kern_channel_advance_slot(ring, last);
	}
	return 0;
}

/* what the controller sent; wakes the user's ring */
void
x6886_bt_deliver(const uint8_t *bytes, unsigned length)
{
	kern_channel_ring_t ring;

	lck_mtx_lock(&btsk_lock);
	if (!btsk_ready) {
		lck_mtx_unlock(&btsk_lock);
		return;
	}
	for (unsigned i = 0; i < length && btsk_rxq_count < BTSK_RXQ; i++) {
		btsk_rxq[(btsk_rxq_head + btsk_rxq_count) % BTSK_RXQ] = bytes[i];
		btsk_rxq_count++;
	}
	ring = btsk_rxring;
	lck_mtx_unlock(&btsk_lock);
	if (ring != NULL) {
		(void)kern_channel_notify(ring, 0);
	}
}

/* the channel's UUID, for bluetoothd's getsockopt; false until the nexus is made */
bool
x6886_bt_uuid(uuid_t out)
{
	if (!btsk_ready) {
		return false;
	}
	uuid_copy(out, btsk_nx_uuid);
	return true;
}

int
x6886_bt_init(void)
{
	uint32_t on = 0;
	nexus_attr_t nxa = NULL;
	errno_t error;
	struct kern_nexus_domain_provider_init dom_init = {
		.nxdpi_version = KERN_NEXUS_DOMAIN_PROVIDER_CURRENT_VERSION,
		.nxdpi_flags = 0,
		.nxdpi_init = btsk_dom_init,
		.nxdpi_fini = btsk_dom_fini,
	};
	struct kern_nexus_provider_init prov_init = {
		.nxpi_version = KERN_NEXUS_DOMAIN_PROVIDER_CURRENT_VERSION,
		.nxpi_flags = NXPIF_VIRTUAL_DEVICE,
		.nxpi_pre_connect = btsk_pre_connect,
		.nxpi_connected = btsk_connected,
		.nxpi_pre_disconnect = btsk_pre_disconnect,
		.nxpi_disconnected = btsk_disconnected,
		.nxpi_ring_init = btsk_ring_init,
		.nxpi_ring_fini = btsk_ring_fini,
		.nxpi_slot_init = btsk_slot_init,
		.nxpi_slot_fini = btsk_slot_fini,
		.nxpi_sync_tx = btsk_sync_tx,
		.nxpi_sync_rx = btsk_sync_rx,
		.nxpi_tx_doorbell = NULL,
	};
	struct kern_nexus_init nx_init = {
		.nxi_version = KERN_NEXUS_CURRENT_VERSION,
		.nxi_flags = 0,
		.nxi_tx_pbufpool = NULL,
		.nxi_rx_pbufpool = NULL,
	};

	x6886_pstore_map();
	if (!PE_parse_boot_argn("x6886_bt", &on, sizeof(on)) || on == 0) {
		return 0;               /* default OFF: nothing is registered */
	}
	if (!x6886_bt_ready_gate || x6886_bt_send_backend == NULL) {
		SAY("ENXIO: no registered H4 backend; transport-only, Bluetooth is not available");
		return ENXIO;
	}
	read_random(&btsk_nx_ctx, sizeof(btsk_nx_ctx));
	error = kern_nexus_register_domain_provider(NEXUS_TYPE_KERNEL_PIPE, (const uint8_t *)"hakios_bt", &dom_init, sizeof(dom_init),
	    &btsk_dom_uuid);
	if (error != 0) {
		X6886_LOG("domain provider: %d", error);
		return error;
	}
	if ((error = kern_nexus_controller_create(&btsk_ncd)) != 0) {
		X6886_LOG("controller: %d", error);
		return error;
	}
	if ((error = kern_nexus_attr_create(&nxa)) != 0) {
		X6886_LOG("attr: %d", error);
		return error;
	}
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_ANONYMOUS, 1);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_TX_RINGS, 1);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_RX_RINGS, 1);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_TX_SLOTS, 64);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_RX_SLOTS, 64);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_SLOT_BUF_SIZE, 2048);
	error = kern_nexus_controller_register_provider(btsk_ncd, btsk_dom_uuid, (const uint8_t *)"com.apple.nexus.hakios_bt", &prov_init,
	    sizeof(prov_init), nxa, &btsk_prov_uuid);
	kern_nexus_attr_destroy(nxa);
	if (error != 0) {
		X6886_LOG("provider: %d", error);
		return error;
	}
	error = kern_nexus_controller_alloc_provider_instance(btsk_ncd, btsk_prov_uuid, (const void *)btsk_nx_ctx, NULL, &btsk_nx_uuid, &nx_init);
	if (error != 0) {
		X6886_LOG("instance: %d", error);
		return error;
	}
	lck_mtx_lock(&btsk_lock);
	btsk_ready = true;
	lck_mtx_unlock(&btsk_lock);
	{
		uuid_string_t text;

		uuid_unparse_upper(btsk_nx_uuid, text);
		X6886_LOG("the Skywalk kernel pipe %s is up", text);
	}
	return 0;
}


/* The CONNSYS owner supplies the only path to its H4 controller. NULL unregisters it. */
void
x6886_bt_backend_register(x6886_bt_send_fn send)
{
	x6886_pstore_map();
	x6886_bt_send_backend = send;
	SAY("H4 backend %s", send != NULL ? "registered" : "removed");
}

/*
 * Called by the CONNSYS owner once the combo chip is up and H4 is reachable.
 * It does not implement power-on, WMT or STP: those ops are not in the allowed sources.
 */
void
x6886_bt_ready_set(bool ready)
{
	x6886_pstore_map();
	x6886_bt_ready_gate = ready;
	SAY("controller %s; a send backend must still be registered for TX",
	    ready ? "reported ready by the CONNSYS owner" : "not ready");
}
