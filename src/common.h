// Miles Together - shared helpers.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "generated/sheets.h"

namespace mt {

// Milliseconds on a steady clock since the mod started.
std::uint32_t NowMs();

// The mod's own diagnostic log in %LOCALAPPDATA%\MilesTogether\MilesTogether.log.
void Log(const char* format, ...);

// %LOCALAPPDATA% with a trailing backslash, or empty when it cannot be found.
std::wstring LocalAppData();

inline constexpr std::uint32_t kJointCount = sheets::layout::joint_count;
static_assert(kJointCount <= 255, "joint indices travel as one byte");

// One joint, in the actor's space.
struct JointPose {
  float q[4];  // rotation, x y z w
  float t[3];  // translation, metres
  float s[3];  // per-axis scale
};

// One moment of a player's hero.
struct Pose {
  std::uint32_t time_ms = 0;  // sender's clock
  float position[3] = {};     // world
  float rotation[4] = {0, 0, 0, 1};
  JointPose joints[kJointCount];
};

// The suit a player wears: skin config ids of its parts.
struct Suit {
  std::uint8_t count = 0;
  std::uint64_t ids[sheets::settings::max_suit_parts] = {};
  bool operator==(const Suit& other) const {
    return count == other.count && std::memcmp(ids, other.ids, sizeof(std::uint64_t) * count) == 0;
  }
};

}  // namespace mt
