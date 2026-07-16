#include "net/net.h"
#include "net/telnet.h"
#include "config.h"

#include "pico/cyw43_arch.h"
#include "cyw43.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include <stdio.h>
#include <string.h>

// Received clean data bytes -> main loop -> vt100. The smooth-scroll pacing caps
// its own backlog (it falls back to jump scroll past ~512 bytes queued), so this
// stays modest to conserve SRAM (the taller smooth-scroll framebuffer is tight).
#define NET_RX_SIZE 2048
#define NET_RX_MASK (NET_RX_SIZE - 1)
static uint8_t  rx_ring[NET_RX_SIZE];
static volatile uint32_t rx_head, rx_tail;

static struct tcp_pcb *pcb;
static ip_addr_t server_ip;
static enum { NS_IDLE, NS_WIFI, NS_GOTIP, NS_DNS, NS_CONNECTING, NS_CONNECTED, NS_ERROR } state = NS_IDLE;
static const char *status_str = "off";
static absolute_time_t join_deadline;

// Runtime connection parameters (copied from the Setup menu on net_connect).
static bool     chip_ready = false;
static char     cfg_host[64];
static uint16_t cfg_port;

int net_connected(void)     { return state == NS_CONNECTED; }
int net_active(void)        { return chip_ready; }
const char *net_status(void) { return status_str; }

static void rx_push(uint8_t c) {
    uint32_t n = (rx_head + 1) & NET_RX_MASK;
    if (n != rx_tail) { rx_ring[rx_head] = c; rx_head = n; }
}
int net_getc(void) {
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1) & NET_RX_MASK;
    return c;
}
int net_rx_level(void) { return (int)((rx_head - rx_tail) & NET_RX_MASK); }

// ---- Telnet filter callbacks --------------------------------------------
static void tn_emit(uint8_t b) { rx_push(b); }
static void tn_reply(const uint8_t *buf, uint32_t len) {
    if (pcb && state == NS_CONNECTED) {
        tcp_write(pcb, buf, len, TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
    }
}

// ---- lwIP TCP callbacks --------------------------------------------------
static err_t on_recv(void *arg, struct tcp_pcb *tp, struct pbuf *p, err_t err) {
    (void)arg;
    if (!p) { state = NS_ERROR; status_str = "closed"; tcp_close(tp); pcb = NULL; return ERR_OK; }
    if (err == ERR_OK) {
        for (struct pbuf *q = p; q; q = q->next)
            telnet_rx((const uint8_t *)q->payload, q->len);
        tcp_recved(tp, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}
static void on_err(void *arg, err_t err) { (void)arg; (void)err; pcb = NULL; state = NS_ERROR; status_str = "tcp error"; }
static err_t on_connected(void *arg, struct tcp_pcb *tp, err_t err) {
    (void)arg; (void)tp;
    if (err != ERR_OK) { state = NS_ERROR; status_str = "connect failed"; return err; }
    state = NS_CONNECTED; status_str = "connected"; telnet_reset();
    return ERR_OK;
}

static void start_tcp(void) {
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) { state = NS_ERROR; status_str = "no pcb"; return; }
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    state = NS_CONNECTING; status_str = "connecting";
    tcp_connect(pcb, &server_ip, cfg_port, on_connected);
}

static void dns_cb(const char *name, const ip_addr_t *ip, void *arg) {
    (void)name; (void)arg;
    if (ip) { server_ip = *ip; start_tcp(); }
    else    { state = NS_ERROR; status_str = "dns failed"; }
}

// WiFi is up: resolve the host (or parse the literal IP) and open the TCP session.
static void begin_resolve(void) {
    state = NS_DNS; status_str = "resolving host";
    if (ipaddr_aton(cfg_host, &server_ip)) {
        start_tcp();
    } else {
        err_t e = dns_gethostbyname(cfg_host, &server_ip, dns_cb, NULL);
        if (e == ERR_OK)                start_tcp();
        else if (e != ERR_INPROGRESS)  { state = NS_ERROR; status_str = "dns error"; }
    }
}

void net_send(const uint8_t *buf, uint32_t len) {
    if (!chip_ready || !pcb || state != NS_CONNECTED) return;
    uint8_t tmp[64]; uint32_t j = 0;
    cyw43_arch_lwip_begin();
    for (uint32_t i = 0; i < len; ++i) {
        tmp[j++] = buf[i];
        if (buf[i] == 255) tmp[j++] = 255;           // escape IAC
        if (j >= sizeof tmp - 1) { tcp_write(pcb, tmp, j, TCP_WRITE_FLAG_COPY); j = 0; }
    }
    if (j) tcp_write(pcb, tmp, j, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    cyw43_arch_lwip_end();
}

// Bring the radio up on demand and start joining. The first call does the heavy
// cyw43 firmware load over PIO-SPI (a one-time SPI/DMA burst that can briefly
// glitch the live video; the scanout watchdog re-arms it). Subsequent calls just
// re-join. net_poll() then drives the join -> DNS -> TCP state machine.
int net_connect(const char *ssid, const char *pass, const char *host, uint16_t port) {
    strncpy(cfg_host, host, sizeof cfg_host - 1); cfg_host[sizeof cfg_host - 1] = 0;
    cfg_port = port;

    if (!chip_ready) {
        telnet_init(tn_emit, tn_reply);
        status_str = "cyw43 init";
        if (cyw43_arch_init()) { state = NS_ERROR; status_str = "cyw43 init failed"; return 1; }
        cyw43_arch_enable_sta_mode();
        chip_ready = true;
    }
    cyw43_arch_wifi_connect_async(ssid, pass, CYW43_AUTH_WPA2_AES_PSK);
    state = NS_WIFI; status_str = "joining wifi";
    join_deadline = make_timeout_time_ms(40000);
    return 0;
}

// Close the telnet session (if any) and power the radio all the way down, so a
// serial-only session has no wireless activity at all.
void net_stop(void) {
    if (pcb) { tcp_abort(pcb); pcb = NULL; }
    if (chip_ready) { cyw43_arch_deinit(); chip_ready = false; }
    state = NS_IDLE; status_str = "off";
}

void net_poll(void) {
    if (!chip_ready) return;
    cyw43_arch_poll();
    if (state == NS_WIFI) {
        int ls = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (ls == CYW43_LINK_UP) {
            static char ipstr[48];
            snprintf(ipstr, sizeof ipstr, "got IP %s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
            status_str = ipstr;
            state = NS_GOTIP;                 // show the IP, then resolve next poll
        } else if (ls < 0) {
            state = NS_ERROR; status_str = "wifi join failed";
        } else if (absolute_time_diff_us(get_absolute_time(), join_deadline) <= 0) {
            state = NS_ERROR; status_str = "wifi timeout (no link)";
        } else {
            status_str = (ls == CYW43_LINK_JOIN) ? "wifi: joining"
                       : (ls == CYW43_LINK_NOIP) ? "wifi: getting IP"
                       : "wifi: link down";
        }
    } else if (state == NS_GOTIP) {
        begin_resolve();
    }
}
