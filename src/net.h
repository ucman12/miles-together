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

// What the other player looks like right now, for the game thread.
struct RemoteView {
  bool connected = false;  // hello exchanged
  bool away = true;        // their hero does not exist at the moment
  bool has_suit = false;
  Suit suit;
  bool has_pose = false;
  Pose pose;               // interpolated at (now - interpolation delay)
};
void SampleRemote(std::uint32_t now_ms, RemoteView* out);

// For the Melty test check "hosting-open".
bool OpenToOthers();

}  // namespace mt::net
