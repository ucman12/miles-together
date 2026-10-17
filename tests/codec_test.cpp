// Checks the pose codec without the game: matrices in, bytes across, matrices out.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "codec.h"

using namespace mt;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++g_failures;
  }
}

float Random(float low, float high) { return low + (high - low) * (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)); }

// A joint matrix with a random rotation, translation up to `reach` metres and the given scale.
void RandomMatrix(float reach, const float* scale, float* m) {
  JointPose joint;
  for (float& v : joint.q) v = Random(-1.0f, 1.0f);
  const float length = std::sqrt(joint.q[0] * joint.q[0] + joint.q[1] * joint.q[1] + joint.q[2] * joint.q[2] + joint.q[3] * joint.q[3]);
  for (float& v : joint.q) v /= length;
  for (float& v : joint.t) v = Random(-reach, reach);
  for (int i = 0; i < 3; ++i) joint.s[i] = scale[i];
  codec::JointToMatrix(joint, m);
}

float MaxDifference(const float* a, const float* b, int from, int to) {
  float worst = 0.0f;
  for (int i = from; i < to; ++i) worst = std::fmax(worst, std::fabs(a[i] - b[i]));
  return worst;
}

}  // namespace

int main() {
  std::srand(20261012);
  auto sent = std::make_unique<Pose>();
  auto received = std::make_unique<Pose>();
  auto matrices = std::make_unique<float[]>(kJointCount * 16);
  auto packed = std::make_unique<std::uint8_t[]>(codec::kMaxPoseBytes);
  codec::DeltaState encode, decode;
  std::size_t first_size = 0, repeat_size = 0;

  for (int round = 0; round < 50; ++round) {
    const float unit[3] = {1.0f, 1.0f, 1.0f};
    for (std::uint32_t j = 0; j < kJointCount; ++j) {
      const float scaled[3] = {Random(0.5f, 1.6f), Random(0.5f, 1.6f), Random(0.5f, 1.6f)};
      const bool distant = j == 7 || j == 9;        // reach targets sit tens of metres out
      const bool odd = j == 155 || j == 209;    // joints the game scales
      if (round % 2 == 0 || j % 3 == 0) RandomMatrix(distant ? 40.0f : 1.5f, odd ? scaled : unit, &matrices[j * 16]);
      codec::MatrixToJoint(&matrices[j * 16], &sent->joints[j]);
    }
    float actor[16];
    RandomMatrix(3000.0f, unit, actor);
    sent->time_ms = 1000u + static_cast<std::uint32_t>(round) * 33u;
    codec::MatrixToRigid(actor, sent->position, sent->rotation);

    const std::size_t size = codec::EncodePose(*sent, &encode, packed.get());
    Check(size <= codec::kMaxPoseBytes, "pose fits its buffer");
    if (round == 0) first_size = size;
    Check(codec::DecodePose(packed.get(), size, &decode, received.get()), "pose decodes");
    Check(received->time_ms == sent->time_ms, "time survives");
    Check(MaxDifference(received->position, sent->position, 0, 3) == 0.0f, "position is exact");

    float back[16];
    codec::RigidToMatrix(received->position, received->rotation, back);
    Check(MaxDifference(back, actor, 0, 12) < 2e-4f, "actor rotation within 0.0002");
    for (std::uint32_t j = 0; j < kJointCount; ++j) {
      codec::JointToMatrix(received->joints[j], back);
      const bool distant = j == 7 || j == 9;
      const bool odd = j == 155 || j == 209;
      Check(MaxDifference(back, &matrices[j * 16], 0, 12) < (odd ? 0.02f : 2e-3f), "joint rotation and scale within tolerance");
      Check(MaxDifference(back, &matrices[j * 16], 12, 15) < (distant ? 0.004f : 0.0008f), "joint translation within tolerance");
    }
  }

  // Nothing moved: only the header, the empty mask and the scaled joints travel.
  repeat_size = codec::EncodePose(*sent, &encode, packed.get());
  Check(repeat_size < 80, "an unchanged pose is tiny");
  Check(codec::DecodePose(packed.get(), repeat_size, &decode, received.get()), "an unchanged pose decodes");

  // A delta cannot start a stream, and truncated bytes are refused.
  codec::DeltaState fresh;
  Check(!codec::DecodePose(packed.get(), repeat_size, &fresh, received.get()), "a delta without a base is refused");
  codec::DeltaState encode_again, decode_again;
  const std::size_t full = codec::EncodePose(*sent, &encode_again, packed.get());
  Check(!codec::DecodePose(packed.get(), full - 5, &decode_again, received.get()), "a cut-off pose is refused");

  // A joint that creeps by less than the slack is not sent; once it has crept far enough it is.
  {
    codec::DeltaState creep_encode, creep_decode;
    auto creeping = std::make_unique<Pose>(*sent);
    std::size_t size = codec::EncodePose(*creeping, &creep_encode, packed.get());
    Check(codec::DecodePose(packed.get(), size, &creep_decode, received.get()), "creep base decodes");
    float worst = 0.0f;
    for (int step = 0; step < 400; ++step) {
      creeping->joints[30].t[0] += 0.00002f;  // 0.02 mm a pose
      creeping->time_ms += 33;
      size = codec::EncodePose(*creeping, &creep_encode, packed.get());
      Check(codec::DecodePose(packed.get(), size, &creep_decode, received.get()), "creeping pose decodes");
      worst = std::fmax(worst, std::fabs(received->joints[30].t[0] - creeping->joints[30].t[0]));
    }
    Check(worst < 0.0008f, "a creeping joint never drifts more than the slack");
  }

  // An enemy rig with fewer joints: only those travel, and a stream of one rig refuses more.
  {
    constexpr std::uint32_t kEnemyJoints = 135;
    codec::DeltaState enemy_encode, enemy_decode;
    const std::size_t size = codec::EncodePose(*sent, &enemy_encode, packed.get(), kEnemyJoints);
    Check(size < full, "a smaller rig packs smaller");
    auto enemy = std::make_unique<Pose>();
    Check(codec::DecodePose(packed.get(), size, &enemy_decode, enemy.get(), kEnemyJoints), "a smaller rig decodes");
    float back[16];
    for (std::uint32_t j = 0; j < kEnemyJoints; ++j) {
      codec::JointToMatrix(enemy->joints[j], back);
      Check(MaxDifference(back, &matrices[j * 16], 12, 15) < 0.004f, "smaller rig joint translation within tolerance");
    }
    codec::DeltaState enemy_decode_small;
    Check(!codec::DecodePose(packed.get(), size, &enemy_decode_small, enemy.get(), kEnemyJoints - 1), "a rig smaller than sent is refused");
  }

  // Halfway between a pose and itself moved one metre is half a metre.
  auto moved = std::make_unique<Pose>(*sent);
  auto between = std::make_unique<Pose>();
  moved->position[0] += 1.0f;
  moved->time_ms += 100;
  codec::Interpolate(*sent, *moved, 0.5f, between.get());
  Check(std::fabs(between->position[0] - sent->position[0] - 0.5f) < 1e-3f, "interpolation is linear in position");
  moved->position[0] += 500.0f;
  codec::Interpolate(*sent, *moved, 0.5f, between.get());
  Check(between->position[0] == moved->position[0], "a jump snaps to the new pose");

  std::printf("codec test: %s (full pose %zu bytes, unchanged pose %zu bytes)\n", g_failures ? "FAILED" : "passed", first_size, repeat_size);
  return g_failures ? 1 : 0;
}
