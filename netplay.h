/* Lockstep netplay core for jzIntv. UDP, two players, input-delay based. */
#ifndef NETPLAY_H_
#define NETPLAY_H_
#include <stdint.h>

#define NP_INPUT_BYTES 16   /* 15 keypad/action entries + 1 raw-bits entry */

typedef void (*netplay_log_fn)(const char *msg);

/* Start a session. is_host: listen on port. Otherwise connect to ip:port.
   delay: input delay in frames (2-4 is typical). Returns 0 on success.   */
int  netplay_start(int is_host, const char *ip, uint16_t port, int delay,
                   netplay_log_fn log_fn);

/* 1 while a session is active (not started, failed, or dropped -> 0).    */
int  netplay_active(void);

/* Call once per emulated video frame, at the same emulated instant on both
   machines. 'local' is this player's controller state; 'check' is a value
   that must be identical on both sides each frame (e.g. total CPU cycles).
   Blocks until the peer's input for this frame arrives. On success fills
   out_r (host/player-1 controller) and out_l (player-2 controller) and
   returns 1. Returns 0 if the session ended; caller then uses live input. */
int  netplay_frame(uint32_t check, const uint8_t local[NP_INPUT_BYTES],
                   uint8_t out_r[NP_INPUT_BYTES], uint8_t out_l[NP_INPUT_BYTES]);

/* Nonzero if the peers' check values ever disagreed (game states drifted). */
int  netplay_desynced(void);

void netplay_stop(void);
#endif
