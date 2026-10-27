/* Linux x86 host test; -no-pie keeps static DMA addresses below 4 GiB.
 * cc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -fno-pie -no-pie -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
 *    tests/net_tg3_test.c -o /tmp/net_tg3_test && /tmp/net_tg3_test
 * Emulates DMA/MMIO only: physical PCI and MDIO access are not exercised. */
#include <assert.h>
#include <stdio.h>
#include "../src/kernel/net/broadcom/tg3.c"

static uint32_t registers[0x7000 / 4];
static unsigned received;
void debug_print(const char* value) { (void)value; }
void debug_hex(uint32_t value) { (void)value; }
void driver_delay_us(uint32_t us) { (void)us; }
void pci_set_master(const pci_device_t* dev, int enabled) { (void)dev; (void)enabled; }
static uint8_t receive_packet(const uint8_t* data, uint32_t length) {
    assert(data == rx_buffers[3] && length == 60);
    received++;
    return 0;
}
static void reset(void) {
    tg3_mmio_base = (uint32_t)registers;
    tg3_ready = 1;
    tg3_tx_prod = tg3_tx_cons = tg3_rx_rcb_cons = 0;
    tg3_rx_std_prod = TG3_BUFFER_COUNT;
    tg3_diag = (net_diagnostics_t){0};
    status_block.tx_consumer = status_block.rx_producer = 0;
    /* Skip physical MDIO link refresh. */
    tg3_link_poll = 1;
}
static void post_rx(unsigned slot, unsigned flags, unsigned opaque) {
    rx_ret_ring[slot].idx_len = 64;
    rx_ret_ring[slot].type_flags = flags;
    rx_ret_ring[slot].opaque = opaque;
}
int main(void) {
    uint8_t frame[60] = {1, 2, 3};
    reset();
    assert(sizeof(tg3_rx_desc_t) == 32 && sizeof(status_block) == 80);
    assert(TG3_RCV_RET_CONS_IDX_MBOX == 0x284);
    assert(is_tg3_device(0x1677) && is_tg3_device(0x16b5));
    assert(!is_tg3_device(0x4401));
    for (unsigned i = 0; i < TG3_TX_BUFFER_COUNT - 1; i++)
        assert(tg3_send(frame, sizeof(frame)));
    assert(!tg3_send(frame, sizeof(frame)) && tg3_diag.tx_busy == 1);
    assert(tg3_diag.tx_completed == 0);
    status_block.tx_consumer = tg3_tx_prod;
    tg3_reap_tx();
    assert(tg3_diag.tx_completed == TG3_TX_BUFFER_COUNT - 1);
    for (unsigned i = 0; i < 1100; i++) {
        assert(tg3_send(frame, 3));
        unsigned slot = (tg3_tx_prod - 1) & (TG3_TX_RING_SIZE - 1);
        assert(tx_ring[slot].len_flags == ((60 << 16) | TG3_TXD_END));
        assert(tx_buffers[slot % TG3_TX_BUFFER_COUNT][3] == 0);
        status_block.tx_consumer = tg3_tx_prod;
        tg3_reap_tx();
    }
    assert(!tg3_send(frame, NET_MAX_FRAME_SIZE + 1));
    /* Invalid hardware completion must not free buffers still in flight. */
    assert(tg3_send(frame, sizeof(frame)));
    unsigned completed = tg3_diag.tx_completed;
    status_block.tx_consumer = (tg3_tx_prod + 1) & (TG3_TX_RING_SIZE - 1);
    tg3_reap_tx();
    assert(tg3_diag.tx_completed == completed);
    reset();
    tg3_set_receive_handler(receive_packet);
    post_rx(0, TG3_RXD_END, TG3_RXD_STD_RING | 3);
    status_block.rx_producer = 1;
    tg3_poll();
    assert(received == 1 && tg3_diag.rx_frames == 1);
    assert(tg3_rx_rcb_cons == 1 && tg3_rx_std_prod == TG3_BUFFER_COUNT + 1);
    assert(rx_std_ring[TG3_BUFFER_COUNT].addr_lo == (uint32_t)rx_buffers[3]);
    assert(registers[0x284 / 4] == 1);
    /* Stale descriptor contents do not constitute another received packet. */
    tg3_poll();
    assert(received == 1);
    post_rx(1, TG3_RXD_ERROR, TG3_RXD_STD_RING | 3);
    status_block.rx_producer = 2;
    tg3_poll();
    assert(received == 1 && tg3_diag.rx_errors == 1);
    assert(tg3_rx_std_prod == TG3_BUFFER_COUNT + 2);
    post_rx(2, TG3_RXD_END, 0x1ffff);
    status_block.rx_producer = 3;
    tg3_poll();
    assert(tg3_diag.rx_invalid == 1 && tg3_rx_rcb_cons == 3);
    reset();
    tg3_rx_rcb_cons = tg3_rx_std_prod = TG3_RX_RING_SIZE - 1;
    post_rx(TG3_RX_RING_SIZE - 1, TG3_RXD_END, TG3_RXD_STD_RING | 3);
    status_block.rx_producer = 0;
    tg3_poll();
    assert(tg3_rx_rcb_cons == 0 && tg3_rx_std_prod == 0);
    tg3_shutdown();
    assert(!tg3_ready && registers[TG3_RDMAC_MODE / 4] == 0);
    puts("PASS: Tg3 DMA layout, TX pressure/completion/wrap/padding, RX recycling/errors/wrap, shutdown");
}
