// Miles Together - the two-player session (sheet row systems/session).
#pragma once

#include "common.h"

namespace mt::net {

// Starts the session thread: hosts, or joins the address Melty gave.
void Start();

// Game thread to the other player. Cheap no-ops while nobody is connected.
void SubmitPose(const Pose& pose);
void SubmitAway();
void SubmitSuit(const Suit& suit);
// The local lighting mode (time of day and weather); sent only when it changes.
void SubmitWorld(std::uint8_t lighting_mode);

// Host: one shared enemy, at the enemy send rate. `joint_count` joints of `pose` travel.
void SubmitEnemy(std::uint32_t id, std::uint64_t asset, std::uint8_t joint_count, const Pose& pose);
// Host: that enemy is no longer shared.
void SubmitEnemyGone(std::uint32_t id);

// Joiner: one of the host's enemies, interpolated like the other player.
struct RemoteEnemy {
  std::uint32_t id = 0;
  std::uint64_t asset = 0;
  std::uint8_t joint_count = 0;
  Pose pose;
};
// Fills `out` with up to `max` enemies heard from recently; returns how many.
std::uint32_t SampleEnemies(std::uint32_t now_ms, RemoteEnemy* out, std::uint32_t max);

// What the other player looks like right now, for the game thread.
struct RemoteView {
  bool connected = false;  // hello exchanged
  bool away = true;        // their hero does not exist at the moment
  bool has_suit = false;
  Suit suit;
  bool has_pose = false;
  Pose pose;               // interpolated at (now - interpolation delay)
  bool has_world = false;
  std::uint8_t lighting_mode = 0;  // the other player's lighting mode
};
void SampleRemote(std::uint32_t now_ms, RemoteView* out);

// For the Melty test check "hosting-open".
bool OpenToOthers();

// True on the player who hosts; the host's world state is the one both players see.
bool Hosting();

}  // namespace mt::net
