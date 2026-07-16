#include "io/serial.h"
#include "config.h"

#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

// Single-producer (IRQ) / single-consumer (main) ring. Power-of-two size so
// the mask wraps cheaply; one slot is always left empty to distinguish
// full from empty without a separate count.
#define RX_RING_SIZE 1024
#define RX_RING_MASK (RX_RING_SIZE - 1)

static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head;   // written by IRQ
static volatile uint32_t rx_tail;   // written by main

static void __not_in_flash_func(uart0_rx_irq)(void) {
    while (uart_is_readable(HOST_UART)) {
        uint8_t c = (uint8_t)uart_getc(HOST_UART);
        uint32_t next = (rx_head + 1) & RX_RING_MASK;
        if (next != rx_tail) {          // drop on overflow rather than block IRQ
            rx_ring[rx_head] = c;
            rx_head = next;
        }
    }
}

void serial_init(void) {
    // Host channel: UART0 on GP0/GP1.
    uart_init(HOST_UART, HOST_BAUD);
    gpio_set_function(HOST_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(HOST_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(HOST_UART, false, false);
    uart_set_format(HOST_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(HOST_UART, true);

    // Secondary channel: UART1 on GP4/GP5 (initialised; RX handling in Phase 7).
    uart_init(AUX_UART, AUX_BAUD);
    gpio_set_function(AUX_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(AUX_UART_RX_PIN, GPIO_FUNC_UART);

    rx_head = rx_tail = 0;
    int irq = (HOST_UART == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, uart0_rx_irq);
    irq_set_enabled(irq, true);
    uart_set_irq_enables(HOST_UART, true, false);   // RX only
}

bool serial_rx_ready(void) {
    return rx_head != rx_tail;
}

int serial_rx_level(void) {
    return (int)((rx_head - rx_tail) & RX_RING_MASK);
}

int serial_getc(void) {
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1) & RX_RING_MASK;
    return c;
}

void serial_putc(uint8_t c) {
    uart_putc_raw(HOST_UART, (char)c);
}

void serial_write(const uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i)
        uart_putc_raw(HOST_UART, (char)buf[i]);
}

void serial_set_baud(uint32_t baud) {
    uart_set_baudrate(HOST_UART, baud);
}
