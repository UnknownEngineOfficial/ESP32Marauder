// ingress_hook.h — read-only data-path instrumentation.
//
// Wraps the AP netif's lwIP input function to count what actually reaches
// lwIP (ARP / IP / ICMP / TCP / UDP) and record a small ring of recent
// packets. For TCP it captures src/dst ports + flags + seq. Observability
// ONLY: every packet is passed through unmodified and un-dropped.
//
// Gated behind API_INGRESS_HOOK (build flag). Parses raw bytes defensively —
// no struct overlay, no endianness assumptions. No serial I/O in the hook
// (runs in lwIP tcpip thread); dump only from loop context.

#pragma once

#ifdef API_INGRESS_HOOK

#include <stdint.h>
#include <string.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>
#include <lwip/tcp.h>
#include <lwip/priv/tcp_priv.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>

// ---- protocol counters (single-writer: lwIP tcpip thread) ----
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

// ---- TCP SYN counters (is any SYN reaching lwIP, and for which port?) ----
static volatile uint32_t g_rx_tcp_syn        = 0;
static volatile uint32_t g_rx_tcp_syn_80     = 0;
static volatile uint32_t g_rx_tcp_syn_8080   = 0;
static volatile uint32_t g_rx_tcp_syn_8081   = 0;
static volatile uint32_t g_rx_tcp_syn_other  = 0;
static volatile uint32_t g_rx_tcp_fin        = 0;
static volatile uint32_t g_rx_tcp_rst        = 0;

// ---- recent-packet ring ----
#define RX_EVENT_N 32
struct rx_event_t {
  char kind[8];      // "ARP_REQ","ARP_REP","ICMP","TCP","UDP","IP"
  uint8_t a[4];      // sender IP (ARP) or src IP (IP)
  uint8_t b[4];      // target IP (ARP) or dst IP (IP)
  uint16_t sport;    // TCP/UDP src port (host order; 0 for ICMP/ARP)
  uint16_t dport;    // TCP/UDP dst port (host order; 0 for ICMP/ARP)
  uint8_t tcpflags;  // TCP flags (SYN=0x02 ACK=0x10 RST=0x04 FIN=0x01 PSH=0x08); 0 for non-TCP
  uint32_t seq;      // TCP seqno (host order); 0 for non-TCP
};
static rx_event_t g_rx_events[RX_EVENT_N];
static volatile uint16_t g_rx_event_head  = 0;
static volatile uint16_t g_rx_event_count = 0;

static netif_input_fn g_orig_ap_input = nullptr;

static void rx_record(const char* kind, const uint8_t* a, const uint8_t* b,
                      uint16_t sport, uint16_t dport, uint8_t tcpflags, uint32_t seq) {
  uint16_t idx = g_rx_event_head;
  rx_event_t& e = g_rx_events[idx];
  strncpy(e.kind, kind, sizeof(e.kind) - 1);
  e.kind[sizeof(e.kind) - 1] = 0;
  if (a) memcpy(e.a, a, 4); else memset(e.a, 0, 4);
  if (b) memcpy(e.b, b, 4); else memset(e.b, 0, 4);
  e.sport = sport;
  e.dport = dport;
  e.tcpflags = tcpflags;
  e.seq = seq;
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
        if (op == 1)      { g_rx_arp_req++; rx_record("ARP_REQ", spa, tpa, 0, 0, 0, 0); }
        else if (op == 2) { g_rx_arp_rep++; rx_record("ARP_REP", spa, tpa, 0, 0, 0, 0); }
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
          if (proto == 1) {
            g_rx_ip_icmp++;
            rx_record("ICMP", src, dst, 0, 0, 0, 0);
          } else if (proto == 6) {
            g_rx_ip_tcp++;
            // TCP header follows the IP header
            if (p->len >= 14 + ihl + 20) {
              const uint8_t* t = ip + ihl;
              uint16_t sport = (uint16_t)((t[0] << 8) | t[1]);
              uint16_t dport = (uint16_t)((t[2] << 8) | t[3]);
              uint8_t  flags = (uint8_t)(t[13] & 0x3F);
              uint32_t seq   = ((uint32_t)t[4] << 24) | ((uint32_t)t[5] << 16) |
                               ((uint32_t)t[6] << 8)  | (uint32_t)t[7];
              rx_record("TCP", src, dst, sport, dport, flags, seq);
              if (flags & 0x02) {                 // SYN
                g_rx_tcp_syn++;
                switch (dport) {
                  case 80:   g_rx_tcp_syn_80++;    break;
                  case 8080: g_rx_tcp_syn_8080++;  break;
                  case 8081: g_rx_tcp_syn_8081++;  break;
                  default:   g_rx_tcp_syn_other++; break;
                }
              }
              if (flags & 0x01) g_rx_tcp_fin++;   // FIN
              if (flags & 0x04) g_rx_tcp_rst++;   // RST
            } else {
              rx_record("TCP", src, dst, 0, 0, 0, 0);
            }
          } else if (proto == 17) {
            g_rx_ip_udp++;
            uint16_t sport = 0, dport = 0;
            if (p->len >= 14 + ihl + 8) {
              const uint8_t* u = ip + ihl;
              sport = (uint16_t)((u[0] << 8) | u[1]);
              dport = (uint16_t)((u[2] << 8) | u[3]);
            }
            rx_record("UDP", src, dst, sport, dport, 0, 0);
          } else {
            g_rx_ip_other++;
            rx_record("IP", src, dst, 0, 0, 0, 0);
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

static void flagsStr(uint8_t f, char* out, size_t n) {
  char tmp[8];
  int i = 0;
  if (f & 0x02) tmp[i++] = 'S';
  if (f & 0x10) tmp[i++] = 'A';
  if (f & 0x04) tmp[i++] = 'R';
  if (f & 0x01) tmp[i++] = 'F';
  if (f & 0x08) tmp[i++] = 'P';
  if (f & 0x40) tmp[i++] = 'E';
  if (f & 0x80) tmp[i++] = 'C';
  if (f & 0x20) tmp[i++] = 'U';
  tmp[i] = 0;
  if (i == 0) { out[0] = '0'; out[1] = 0; return; }
  strncpy(out, tmp, n - 1);
  out[n - 1] = 0;
}

static const char* tcpStateStr(enum tcp_state s) {
  switch (s) {
    case CLOSED:      return "CLOSED";
    case LISTEN:      return "LISTEN";
    case SYN_SENT:    return "SYN_SENT";
    case SYN_RCVD:    return "SYN_RCVD";
    case ESTABLISHED: return "ESTABLISHED";
    case FIN_WAIT_1:  return "FIN_WAIT_1";
    case FIN_WAIT_2:  return "FIN_WAIT_2";
    case CLOSE_WAIT:  return "CLOSE_WAIT";
    case CLOSING:     return "CLOSING";
    case LAST_ACK:    return "LAST_ACK";
    case TIME_WAIT:   return "TIME_WAIT";
    default:          return "?";
  }
}

// Dump counters + ring + full PCB state from loop context.
static void apiIngressDump() {
  Serial.printf("[INGRESS-HOOK] eth=%u arp=%u(req=%u rep=%u) ip=%u(icmp=%u tcp=%u udp=%u o=%u) etho=%u\n",
                g_rx_eth_total, g_rx_arp, g_rx_arp_req, g_rx_arp_rep,
                g_rx_ip, g_rx_ip_icmp, g_rx_ip_tcp, g_rx_ip_udp, g_rx_ip_other,
                g_rx_eth_other);
  Serial.printf("[TCP-RX] syn=%u (80=%u 8080=%u 8081=%u other=%u) fin=%u rst=%u\n",
                g_rx_tcp_syn, g_rx_tcp_syn_80, g_rx_tcp_syn_8080, g_rx_tcp_syn_8081,
                g_rx_tcp_syn_other, g_rx_tcp_fin, g_rx_tcp_rst);

  uint16_t cnt = g_rx_event_count;
  uint16_t start = (uint16_t)((g_rx_event_head + RX_EVENT_N - cnt) % RX_EVENT_N);
  for (uint16_t i = 0; i < cnt; i++) {
    uint16_t idx = (uint16_t)((start + i) % RX_EVENT_N);
    rx_event_t& e = g_rx_events[idx];
    char fl[8];
    flagsStr(e.tcpflags, fl, sizeof(fl));
    if (e.kind[0] == 'T' || e.kind[0] == 'U') {
      Serial.printf("[INGRESS-HOOK] ev %s %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u",
                    e.kind, e.a[0], e.a[1], e.a[2], e.a[3], e.sport,
                    e.b[0], e.b[1], e.b[2], e.b[3], e.dport);
      if (e.kind[0] == 'T') Serial.printf(" flags=%s seq=%u", fl, e.seq);
      Serial.printf("\n");
    } else {
      Serial.printf("[INGRESS-HOOK] ev %s %u.%u.%u.%u -> %u.%u.%u.%u\n",
                    e.kind, e.a[0], e.a[1], e.a[2], e.a[3],
                    e.b[0], e.b[1], e.b[2], e.b[3]);
    }
  }
}

// Full PCB dump (listeners + active + timewait) with IP:port + state.
static void apiIngressPcbDump() {
  // listeners
  int nl = 0;
  for (struct tcp_pcb_listen *l = tcp_listen_pcbs.listen_pcbs; l != NULL; l = l->next) {
    nl++;
    char lip[16];
    if (IP_IS_V4(&l->local_ip)) {
      const ip4_addr_t* a = ip_2_ip4(&l->local_ip);
      snprintf(lip, sizeof(lip), "%u.%u.%u.%u",
               ip4_addr1(a), ip4_addr2(a), ip4_addr3(a), ip4_addr4(a));
    } else {
      strncpy(lip, "v6", sizeof(lip) - 1); lip[sizeof(lip)-1] = 0;
    }
    Serial.printf("[TCP-LISTEN] #%d local=%s:%u state=%s\n",
                  nl, lip, (unsigned)l->local_port, tcpStateStr((enum tcp_state)l->state));
  }
  if (nl == 0) Serial.printf("[TCP-LISTEN] none\n");

  // active
  int na = 0;
  for (struct tcp_pcb *a = tcp_active_pcbs; a != NULL; a = a->next) {
    na++;
    char lip[16], rip[16];
    if (IP_IS_V4(&a->local_ip)) {
      const ip4_addr_t* x = ip_2_ip4(&a->local_ip);
      snprintf(lip, sizeof(lip), "%u.%u.%u.%u", ip4_addr1(x), ip4_addr2(x), ip4_addr3(x), ip4_addr4(x));
    } else { strncpy(lip, "v6", sizeof(lip)-1); lip[sizeof(lip)-1]=0; }
    if (IP_IS_V4(&a->remote_ip)) {
      const ip4_addr_t* x = ip_2_ip4(&a->remote_ip);
      snprintf(rip, sizeof(rip), "%u.%u.%u.%u", ip4_addr1(x), ip4_addr2(x), ip4_addr3(x), ip4_addr4(x));
    } else { strncpy(rip, "v6", sizeof(rip)-1); rip[sizeof(rip)-1]=0; }
    Serial.printf("[TCP-ACTIVE] #%d %s:%u -> %s:%u state=%s\n",
                  na, lip, (unsigned)a->local_port, rip, (unsigned)a->remote_port,
                  tcpStateStr(a->state));
  }
  if (na == 0) Serial.printf("[TCP-ACTIVE] none\n");

  // timewait
  int nt = 0;
  for (struct tcp_pcb *t = tcp_tw_pcbs; t != NULL; t = t->next) { nt++; }
  Serial.printf("[TCP-TW] %d\n", nt);
}

#endif // API_INGRESS_HOOK
