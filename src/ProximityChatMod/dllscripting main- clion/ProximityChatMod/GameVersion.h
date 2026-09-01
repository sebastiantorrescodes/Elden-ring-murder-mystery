#ifndef _GAME_VERSION_H_
#define _GAME_VERSION_H_

#include <cstdint>

// Reads the running process's own exe (eldenring.exe) file version and packs it into
// one comparable value: high 32 bits = dwFileVersionMS (major.minor), low 32 bits =
// dwFileVersionLS (build.revision). Same convention EROverlay uses for its own
// getGameVersion(), so version thresholds ported from there compare directly.
// Returns 0 if version info couldn't be read.
uint64_t GetGameVersion();

#endif // _GAME_VERSION_H_
