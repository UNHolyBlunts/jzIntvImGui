#include "netplay.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
typedef int socklen_t;
# define CLOSESOCK closesocket
# define NOW_MS() ((uint64_t)GetTickCount64())
# define SLEEP_MS(x) Sleep(x)
#else
# include <unistd.h>
# include <time.h>
# include <fcntl.h>
# include <errno.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# define CLOSESOCK close
static uint64_t now_ms_(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
# define NOW_MS() now_ms_()
# define SLEEP_MS(x) usleep((x)*1000)
#endif

#define RING 256
#define MASK (RING-1)
#define REDUND 8
#define MAGIC 0x504E5A4Au  /* 'JZNP' */
#define T_HELLO 0
#define T_INPUT 1
#define DISCONNECT_MS 5000

typedef struct {
    uint32_t magic; uint8_t type, count; uint16_t pad;
    uint32_t frame, check_frame, check;
    uint8_t  data[REDUND][NP_INPUT_BYTES];
} pkt_t;   /* 144 bytes; assumes little-endian hosts (ARM, x86) */

static struct {
    int active, is_host, delay, synced, desynced, peer_known, hello_seen;
    int sock; struct sockaddr_in peer;
    uint32_t frame;
    uint8_t  loc[RING][NP_INPUT_BYTES];  uint32_t loc_tag[RING];
    uint8_t  rem[RING][NP_INPUT_BYTES];  uint32_t rem_tag[RING]; uint8_t rem_ok[RING];
    uint32_t my_check[RING]; uint32_t my_check_tag[RING]; uint8_t my_check_ok[RING];
    uint32_t rem_check[RING]; uint32_t rem_check_tag[RING]; uint8_t rem_check_ok[RING];
    netplay_log_fn log;
    pkt_t last;  int have_last;
} S;

static void say(const char *m){ if (S.log) S.log(m); else fprintf(stderr,"[netplay] %s\n",m); }

#ifdef NETPLAY_TEST
static int drop_pct = 0;
void netplay_test_drop(int pct){ drop_pct = pct; }
#endif

static void send_pkt(const pkt_t *p)
{
    if (!S.peer_known) return;
#ifdef NETPLAY_TEST
    if (drop_pct && (rand() % 100) < drop_pct) return;
#endif
    sendto(S.sock, (const char *)p, sizeof *p, 0,
           (struct sockaddr *)&S.peer, sizeof S.peer);
}

static void send_hello(void)
{
    pkt_t p; memset(&p, 0, sizeof p);
    p.magic = MAGIC; p.type = T_HELLO;
    send_pkt(&p);
}

static void compare_check(uint32_t f)
{
    int i = f & MASK;
    if (S.my_check_ok[i] && S.my_check_tag[i] == f &&
        S.rem_check_ok[i] && S.rem_check_tag[i] == f &&
        S.my_check[i] != S.rem_check[i] && !S.desynced) {
        S.desynced = 1; say("DESYNC: emulator states differ between players");
    }
}

static void poll_net(void)
{
    for (;;) {
        pkt_t p; struct sockaddr_in from; socklen_t fl = sizeof from;
        int n = (int)recvfrom(S.sock, (char *)&p, sizeof p, 0,
                              (struct sockaddr *)&from, &fl);
        if (n != (int)sizeof p || p.magic != MAGIC) break;
        if (!S.peer_known) { S.peer = from; S.peer_known = 1; }
        else if (from.sin_addr.s_addr != S.peer.sin_addr.s_addr ||
                 from.sin_port != S.peer.sin_port) continue;
        if (p.type == T_HELLO) { S.hello_seen = 1; continue; }
        if (p.type != T_INPUT || p.count == 0 || p.count > REDUND) continue;
        S.hello_seen = 1;
        for (int i = 0; i < p.count; i++) {
            uint32_t f = p.frame - (uint32_t)(p.count - 1 - i);
            if ((int32_t)(f - S.frame) < 0) continue;      /* already used */
            if ((int32_t)(f - S.frame) >= RING - 1) continue;
            memcpy(S.rem[f & MASK], p.data[i], NP_INPUT_BYTES);
            S.rem_tag[f & MASK] = f; S.rem_ok[f & MASK] = 1;
        }
        S.rem_check[p.check_frame & MASK] = p.check;
        S.rem_check_tag[p.check_frame & MASK] = p.check_frame;
        S.rem_check_ok[p.check_frame & MASK] = 1;
        compare_check(p.check_frame);
    }
}

int netplay_start(int is_host, const char *ip, uint16_t port, int delay,
                  netplay_log_fn log_fn)
{
    memset(&S, 0, sizeof S);
    S.log = log_fn; S.is_host = is_host; S.delay = delay < 1 ? 1 : delay > 6 ? 6 : delay;
#ifdef _WIN32
    { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
#endif
    S.sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (S.sock < 0) { say("socket() failed"); return -1; }
#ifdef _WIN32
    { u_long nb = 1; ioctlsocket(S.sock, FIONBIO, &nb); }
#else
    fcntl(S.sock, F_SETFL, fcntl(S.sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    struct sockaddr_in me; memset(&me, 0, sizeof me);
    me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port = htons(is_host ? port : 0);
    if (bind(S.sock, (struct sockaddr *)&me, sizeof me) < 0) { say("bind() failed"); CLOSESOCK(S.sock); return -1; }
    if (!is_host) {
        memset(&S.peer, 0, sizeof S.peer);
        S.peer.sin_family = AF_INET; S.peer.sin_port = htons(port);
        if (!ip || inet_pton(AF_INET, ip, &S.peer.sin_addr) != 1) { say("bad host address"); CLOSESOCK(S.sock); return -1; }
        S.peer_known = 1;
    }
    for (int f = 0; f < S.delay; f++) {   /* first 'delay' frames are neutral */
        S.loc_tag[f] = f; S.rem_tag[f] = f; S.rem_ok[f] = 1;
    }
    S.active = 1;
    say(is_host ? "waiting for the other player..." : "connecting...");
    return 0;
}

int netplay_active(void) { return S.active; }
int netplay_desynced(void) { return S.desynced; }
void netplay_stop(void) { if (S.active) { CLOSESOCK(S.sock); S.active = 0; } }

static void build_and_send(uint32_t eff_frame)
{
    pkt_t p; memset(&p, 0, sizeof p);
    p.magic = MAGIC; p.type = T_INPUT; p.frame = eff_frame;
    p.check_frame = S.frame; p.check = S.my_check[S.frame & MASK];
    p.count = (uint8_t)(eff_frame + 1 < REDUND ? eff_frame + 1 : REDUND);
    for (int i = 0; i < p.count; i++) {
        uint32_t f = eff_frame - (uint32_t)(p.count - 1 - i);
        if (S.loc_tag[f & MASK] == f) memcpy(p.data[i], S.loc[f & MASK], NP_INPUT_BYTES);
    }
    S.last = p; S.have_last = 1;
    send_pkt(&p);
}

int netplay_frame(uint32_t check, const uint8_t local[NP_INPUT_BYTES],
                  uint8_t out_r[NP_INPUT_BYTES], uint8_t out_l[NP_INPUT_BYTES])
{
    if (!S.active) return 0;
    uint64_t t0 = NOW_MS(), tr = 0;

    /* First call: handshake. Emulation is paused here on both machines, so
       both resume from the same emulated instant.                          */
    if (!S.synced) {
        uint64_t last_hello = 0;
        while (!S.hello_seen) {
            poll_net();
            uint64_t t = NOW_MS();
            if (!S.is_host && t - last_hello > 200) { send_hello(); last_hello = t; }
            if (S.is_host && S.peer_known && t - last_hello > 200) { send_hello(); last_hello = t; }
            if (t - t0 > 60000) { say("no player connected, netplay off"); netplay_stop(); return 0; }
            SLEEP_MS(5);
        }
        for (int i = 0; i < 5; i++) { send_hello(); SLEEP_MS(10); }
        S.synced = 1; say("connected");
    }

    uint32_t f = S.frame;
    S.my_check[f & MASK] = check; S.my_check_tag[f & MASK] = f; S.my_check_ok[f & MASK] = 1;
    compare_check(f);

    /* Local input takes effect 'delay' frames from now. */
    uint32_t eff = f + (uint32_t)S.delay;
    memcpy(S.loc[eff & MASK], local, NP_INPUT_BYTES); S.loc_tag[eff & MASK] = eff;
    build_and_send(eff);

    /* Wait for the peer's input for frame f. */
    t0 = NOW_MS();
    for (;;) {
        poll_net();
        if (S.rem_ok[f & MASK] && S.rem_tag[f & MASK] == f) break;
        uint64_t t = NOW_MS();
        if (t - t0 > DISCONNECT_MS) { say("peer lost, netplay off"); netplay_stop(); return 0; }
        if (t - tr > 15) { if (S.have_last) send_pkt(&S.last); tr = t; }
        SLEEP_MS(1);
    }

    const uint8_t *mine = (S.loc_tag[f & MASK] == f) ? S.loc[f & MASK] : NULL;
    uint8_t zero[NP_INPUT_BYTES] = {0};
    if (!mine) mine = zero;
    const uint8_t *theirs = S.rem[f & MASK];
    memcpy(S.is_host ? out_r : out_l, mine,   NP_INPUT_BYTES);
    memcpy(S.is_host ? out_l : out_r, theirs, NP_INPUT_BYTES);
    S.rem_ok[f & MASK] = 0;
    S.frame++;
    return 1;
}
