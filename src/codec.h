// Miles Together - pose packing (sheet row systems/pose_codec, message net_messages/pose).
#pragma once

#include "common.h"

namespace mt::codec {

// Largest pose message body this codec writes.
inline constexpr std::size_t kMaxPoseBytes = 4 + 12 + 8 + 31 + kJointCount * 12 + 1 + kJointCount * 4;

// 4x4 row matrices (translation in the last row) to and from a joint pose.
void MatrixToJoint(const float* m, JointPose* out);
void JointToMatrix(const JointPose& joint, float* m);
void MatrixToRigid(const float* m, float* position, float* rotation);
void RigidToMatrix(const float* position, const float* rotation, float* m);

// Remembers what was last sent or received, so only changed joints travel.
struct DeltaState {
  bool primed = false;
  std::uint8_t packed[kJointCount][12] = {};
  void Reset() { primed = false; }
};

// Packs `pose`; returns the byte count written to `out` (at most kMaxPoseBytes).
std::size_t EncodePose(const Pose& pose, DeltaState* state, std::uint8_t* out);

// Unpacks into `pose`, which must hold the previously decoded pose of this stream.
// Returns false when the bytes are not a valid pose.
bool DecodePose(const std::uint8_t* data, std::size_t size, DeltaState* state, Pose* pose);

// Blend between two poses, t in [0, 1].
void Interpolate(const Pose& a, const Pose& b, float t, Pose* out);

}  // namespace mt::codec
