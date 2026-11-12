jzIntvImGui netplay patch (lockstep, 2 players, UDP)

APPLY: from the repository root:  patch -p1 < netplay.patch
  (adds app/src/main/cpp/jzintv/netplay/netplay.[ch]; edits pads.[ch], stic.c,
   jzintv.c, main.cpp and CMakeLists.txt)

USE: create a file named netplay.txt in the app's configuration folder
  (the folder that holds jzIntvImGui.ini) on BOTH devices:
    Player 1 (host):   host:7777
    Player 2 (joins):  join:192.168.1.20:7777      (host's IP address)
  Optional input delay in frames (default 3):  host:7777:4
  Delete the file (or empty it) to play normally.
  Start the SAME game, same ROM and BIOS files, on both devices. Both players
  use their normal Player 1 controls; the joiner is mapped to Player 2 in-game.
  Start both from a fresh launch (do not use Reset mid-game).

STATUS: the network/lockstep core is tested (loopback, 50% packet loss,
  jitter, desync detection). The glue into jzIntv compiles (syntax-checked)
  but the full Android app has NOT been built or run.
KNOWN LIMITS: UDP only (same Wi-Fi, or forward the port); blocks up to 5 s
  waiting for the peer before dropping out of netplay; desync check compares
  only the frame cycle counter, which catches gross drift but not subtle drift.
