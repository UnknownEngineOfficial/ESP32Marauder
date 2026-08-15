// ingress_hook.h — read-only data-path instrumentation BELOW TCP.
//
// Wraps the AP netif's lwIP input function to count what actually reaches
// lwIP after DHCP (ARP / IP / ICMP / TCP / UDP), and record a small ring of
// recent packets. Observability ONLY: every packet is passed through
// unmodified and un-dropped.
//
// Gated behind API_INGRESS_HOOK (build flag). Parses raw bytes defensively —
// no struct overlay, no endianness assumptions.

#pragma once

#ifdef API_INGRESS_HOOK

#include <stdint.h>
#include <string.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>

// ---- counters (single-writer: lwIP tcpip thread) ----
static volatile uint32_t g_rx_eth_total  = 0;
static volatile uint32_t g_rx_arp        = 0;
static volatile uint32_t g_rx_arp_req    = 0;
static volatile uint32_t g_rx_arp_rep    = 0;
static volatile uint32_t g_rx_ip         = 0;
static volatile uint32_t g_rx_ip_icmp    = 0;
static volatile uint32_t g_rx_ip_tcp     = 0;
static volatile uint32_t g_rx_ip_udp     = 0;
static volatile uint32_t g_rx_ip_other   = 0;
static volatile uint32_t g_rx_eth_other  = 0;

// ---- recent-packet ring ----
#define RX_EVENT_N 16
struct rx_event_t {
  char kind[8];   // "ARP_REQ","ARP_REP","ICMP","TCP","UDP","IP"
  uint8_t a[4];   // sender IP (ARP) or src IP (IP)
  uint8_t b[4];   // target IP (ARP) or dst IP (IP)
};
static rx_event_t g_rx_events[RX_EVENT_N];
static volatile uint16_t g_rx_event_head  = 0;
static volatile uint16_t g_rx_event_count = 0;

static netif_input_fn g_orig_ap_input = nullptr;

static void rx_record(const char* kind, const uint8_t* a, const uint8_t* b) {
  uint16_t idx = g_rx_event_head;
  rx_event_t& e = g_rx_events[idx];
  strncpy(e.kind, kind, sizeof(e.kind) - 1);
  e.kind[sizeof(e.kind) - 1] = 0;
  if (a) memcpy(e.a, a, 4); else memset(e.a, 0, 4);
  if (b) memcpy(e.b, b, 4); else memset(e.b, 0, 4);
  g_rx_event_head = (uint16_t)((idx + 1) % RX_EVENT_N);
  if (g_rx_event_count < RX_EVENT_N) g_rx_event_count++;
}

// The wrapped input function. Runs in the same context as the original
// (lwIP tcpip thread). No serial I/O here — only counters + ring.
static err_t apiIngressInput(struct pbuf* p, struct netif* inp) {
  g_rx_eth_total++;
  if (p && p->payload && p->len >= 14) {
    const uint8_t* b = (const uint8_t*)p->payload;
    uint16_t etype = (uint16_t)((b[12] << 8) | b[13]);
    if (etype == 0x0806) {                       // ARP
      g_rx_arp++;
      if (p->len >= 14 + 28) {
        const uint8_t* a = b + 14;
        uint16_t op = (uint16_t)((a[6] << 8) | a[7]);
        const uint8_t* spa = a + 14;             // sender protocol addr
        const uint8_t* tpa = a + 24;             // target protocol addr
        if (op == 1)      { g_rx_arp_req++; rx_record("ARP_REQ", spa, tpa); }
        else if (op == 2) { g_rx_arp_rep++; rx_record("ARP_REP", spa, tpa); }
      }
    } else if (etype == 0x0800) {                // IPv4
      g_rx_ip++;
      if (p->len >= 14 + 20) {
        const uint8_t* ip = b + 14;
        uint8_t ihl = (uint8_t)((ip[0] & 0x0F) * 4);
        if (ihl >= 20 && p->len >= 14 + ihl) {
          uint8_t proto = ip[9];
          const uint8_t* src = ip + 12;
          const uint8_t* dst = ip + 16;
          switch (proto) {
            case 1:  g_rx_ip_icmp++; rx_record("ICMP", src, dst); break;
            case 6:  g_rx_ip_tcp++;  rx_record("TCP",  src, dst); break;
            case 17: g_rx_ip_udp++;  rx_record("UDP",  src, dst); break;
            default: g_rx_ip_other++; rx_record("IP", src, dst); break;
          }
        }
      }
    } else {
      g_rx_eth_other++;
    }
  }
  if (g_orig_ap_input) return g_orig_ap_input(p, inp);
  return tcpip_input(p, inp);   // fallback (should not happen)
}

// Install the wrapper on the AP netif. Call after softAP is up.
static void apiIngressInstall() {
  if (g_orig_ap_input) return;   // already installed
  esp_netif_t* apn = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!apn) { Serial.println("[INGRESS-HOOK] AP netif not found"); return; }
  struct netif* n = (struct netif*)esp_netif_get_netif_impl(apn);
  if (!n) { Serial.println("[INGRESS-HOOK] netif impl NULL"); return; }
  g_orig_ap_input = n->input;
  n->input = apiIngressInput;
  Serial.printf("[INGRESS-HOOK] wrapped AP netif->input (netif=%p)\n", (void*)n);
}

// Dump counters + ring from loop context (call from runHealthCheck).
static void apiIngressDump() {
  Serial.printf("[INGRESS-HOOK] eth_total=%u · arp=%u(req=%u rep=%u) · ip=%u(icmp=%u tcp=%u udp=%u other=%u) · eth_other=%u\n",
                g_rx_eth_total, g_rx_arp, g_rx_arp_req, g_rx_arp_rep,
                g_rx_ip, g_rx_ip_icmp, g_rx_ip_tcp, g_rx_ip_udp, g_rx_ip_other,
                g_rx_eth_other);
  uint16_t cnt = g_rx_event_count;
  if (cnt == 0) return;
  uint16_t start = (uint16_t)((g_rx_event_head + RX_EVENT_N - cnt) % RX_EVENT_N);
  for (uint16_t i = 0; i < cnt; i++) {
    uint16_t idx = (uint16_t)((start + i) % RX_EVENT_N);
    rx_event_t& e = g_rx_events[idx];
    Serial.printf("[INGRESS-HOOK] ev %s %u.%u.%u.%u -> %u.%u.%u.%u\n",
                  e.kind, e.a[0], e.a[1], e.a[2], e.a[3],
                  e.b[0], e.b[1], e.b[2], e.b[3]);
  }
}

#endif // API_INGRESS_HOOK
