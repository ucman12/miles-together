// Miles Together - pose packing (sheet row systems/pose_codec, message net_messages/pose).
//
// Body of a pose message:
//   u32 time_ms | f32 position[3] | i16 rotation[4] | changed-joint bitset |
//   12 bytes per changed joint (48-bit quaternion + 3 x i16 translation) |
//   u8 scaled count | per scaled joint: u8 index + i8 scale[3]
#include "codec.h"

#include <cmath>

namespace mt::codec {
namespace {

constexpr std::size_t kMaskBytes = (kJointCount + 7) / 8;
constexpr float kInvSqrt2 = 0.70710678f;
constexpr float kFineStep = 1.0f / 8192.0f;   // +-4 m
constexpr float kCoarseStep = 1.0f / 256.0f;  // +-128 m
constexpr float kScaleStep = 1.0f / 64.0f;
constexpr int kRotationSlack = 12;    // quaternion steps a joint may drift before it is sent again (about 0.1 degree)
constexpr int kTranslationSlack = 4;  // fine translation steps (about half a millimetre)

void WriteU32(std::uint8_t*& p, std::uint32_t v) { std::memcpy(p, &v, 4); p += 4; }
void WriteF32(std::uint8_t*& p, float v) { std::memcpy(p, &v, 4); p += 4; }
void WriteI16(std::uint8_t*& p, std::int16_t v) { std::memcpy(p, &v, 2); p += 2; }
std::uint32_t ReadU32(const std::uint8_t*& p) { std::uint32_t v; std::memcpy(&v, p, 4); p += 4; return v; }
float ReadF32(const std::uint8_t*& p) { float v; std::memcpy(&v, p, 4); p += 4; return v; }
std::int16_t ReadI16(const std::uint8_t*& p) { std::int16_t v; std::memcpy(&v, p, 2); p += 2; return v; }

std::int16_t Quantize16(float value, float step) {
  float scaled = std::round(value / step);
  if (!(scaled > -32767.0f)) scaled = -32767.0f;  // also catches NaN
  if (scaled > 32767.0f) scaled = 32767.0f;
  return static_cast<std::int16_t>(scaled);
}

void Normalize4(float* q) {
  const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (length < 1e-8f || !std::isfinite(length)) {
    q[0] = q[1] = q[2] = 0.0f;
    q[3] = 1.0f;
    return;
  }
  for (int i = 0; i < 4; ++i) q[i] /= length;
}

// Rotation part: rows of `m` are the basis vectors, already unit length.
void RowsToQuat(const float r[3][3], float* q) {
  // c[j][i] = r[i][j]: the column-vector form of the same rotation.
  const float c00 = r[0][0], c01 = r[1][0], c02 = r[2][0];
  const float c10 = r[0][1], c11 = r[1][1], c12 = r[2][1];
  const float c20 = r[0][2], c21 = r[1][2], c22 = r[2][2];
  const float trace = c00 + c11 + c22;
  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    q[3] = 0.25f * s;
    q[0] = (c21 - c12) / s;
    q[1] = (c02 - c20) / s;
    q[2] = (c10 - c01) / s;
  } else if (c00 > c11 && c00 > c22) {
    const float s = std::sqrt(1.0f + c00 - c11 - c22) * 2.0f;
    q[3] = (c21 - c12) / s;
    q[0] = 0.25f * s;
    q[1] = (c01 + c10) / s;
    q[2] = (c02 + c20) / s;
  } else if (c11 > c22) {
    const float s = std::sqrt(1.0f + c11 - c00 - c22) * 2.0f;
    q[3] = (c02 - c20) / s;
    q[0] = (c01 + c10) / s;
    q[1] = 0.25f * s;
    q[2] = (c12 + c21) / s;
  } else {
    const float s = std::sqrt(1.0f + c22 - c00 - c11) * 2.0f;
    q[3] = (c10 - c01) / s;
    q[0] = (c02 + c20) / s;
    q[1] = (c12 + c21) / s;
    q[2] = 0.25f * s;
  }
  Normalize4(q);
}

void QuatToRows(const float* q, float r[3][3]) {
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const float c00 = 1 - 2 * (y * y + z * z), c01 = 2 * (x * y - z * w), c02 = 2 * (x * z + y * w);
  const float c10 = 2 * (x * y + z * w), c11 = 1 - 2 * (x * x + z * z), c12 = 2 * (y * z - x * w);
  const float c20 = 2 * (x * z - y * w), c21 = 2 * (y * z + x * w), c22 = 1 - 2 * (x * x + y * y);
  r[0][0] = c00; r[0][1] = c10; r[0][2] = c20;
  r[1][0] = c01; r[1][1] = c11; r[1][2] = c21;
  r[2][0] = c02; r[2][1] = c12; r[2][2] = c22;
}

// 48 bits: 2 bits largest index, 1 bit translation mode, 3 x 15 bits.
void PackJoint(const JointPose& joint, std::uint8_t* out) {
  float q[4] = {joint.q[0], joint.q[1], joint.q[2], joint.q[3]};
  int largest = 0;
  for (int i = 1; i < 4; ++i) {
    if (std::fabs(q[i]) > std::fabs(q[largest])) largest = i;
  }
  if (q[largest] < 0.0f) {
    for (float& v : q) v = -v;
  }
  const bool coarse = std::fabs(joint.t[0]) > 3.9f || std::fabs(joint.t[1]) > 3.9f || std::fabs(joint.t[2]) > 3.9f;
  std::uint64_t bits = static_cast<std::uint64_t>(largest) | (coarse ? 4u : 0u);
  int shift = 3;
  for (int i = 0; i < 4; ++i) {
    if (i == largest) continue;
    float unit = q[i] / kInvSqrt2 * 0.5f + 0.5f;
    if (!(unit > 0.0f)) unit = 0.0f;
    if (unit > 1.0f) unit = 1.0f;
    bits |= static_cast<std::uint64_t>(std::lround(unit * 32767.0f)) << shift;
    shift += 15;
  }
  std::memcpy(out, &bits, 6);
  std::uint8_t* p = out + 6;
  const float step = coarse ? kCoarseStep : kFineStep;
  for (int i = 0; i < 3; ++i) WriteI16(p, Quantize16(joint.t[i], step));
}

void UnpackJoint(const std::uint8_t* in, JointPose* joint) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, in, 6);
  const int largest = static_cast<int>(bits & 3u);
  const bool coarse = (bits & 4u) != 0;
  int shift = 3;
  float sum = 0.0f;
  for (int i = 0; i < 4; ++i) {
    if (i == largest) continue;
    const float unit = static_cast<float>((bits >> shift) & 0x7fffu) / 32767.0f;
    joint->q[i] = (unit - 0.5f) * 2.0f * kInvSqrt2;
    sum += joint->q[i] * joint->q[i];
    shift += 15;
  }
  joint->q[largest] = std::sqrt(sum < 1.0f ? 1.0f - sum : 0.0f);
  Normalize4(joint->q);
  const std::uint8_t* p = in + 6;
  const float step = coarse ? kCoarseStep : kFineStep;
  for (int i = 0; i < 3; ++i) joint->t[i] = static_cast<float>(ReadI16(p)) * step;
}

// True when `now` differs from `sent` by less than the eye can see: the joint is not sent again.
// Compared with what was last sent, never with the last pose, so small changes cannot add up unseen.
bool NearlySame(const std::uint8_t* now, const std::uint8_t* sent) {
  std::uint64_t a = 0, b = 0;
  std::memcpy(&a, now, 6);
  std::memcpy(&b, sent, 6);
  if ((a & 7u) != (b & 7u)) return false;  // another largest component or translation mode
  for (int shift = 3; shift < 48; shift += 15) {
    const int da = static_cast<int>((a >> shift) & 0x7fffu), db = static_cast<int>((b >> shift) & 0x7fffu);
    if (da - db > kRotationSlack || db - da > kRotationSlack) return false;
  }
  const int slack = (a & 4u) ? 0 : kTranslationSlack;
  const std::uint8_t* pa = now + 6;
  const std::uint8_t* pb = sent + 6;
  for (int i = 0; i < 3; ++i) {
    const int da = ReadI16(pa), db = ReadI16(pb);
    if (da - db > slack || db - da > slack) return false;
  }
  return true;
}

bool IsScaled(const JointPose& joint) {
  for (float s : joint.s) {
    if (std::fabs(s - 1.0f) > 0.01f) return true;
  }
  return false;
}

std::int8_t PackScale(float s) {
  float v = std::round(s / kScaleStep);
  if (!(v > -127.0f)) v = -127.0f;
  if (v > 127.0f) v = 127.0f;
  return static_cast<std::int8_t>(v);
}

}  // namespace

void MatrixToJoint(const float* m, JointPose* out) {
  float r[3][3];
  for (int i = 0; i < 3; ++i) {
    const float* row = m + i * 4;
    const float length = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
    out->s[i] = length;
    if (length > 1e-6f && std::isfinite(length)) {
      for (int j = 0; j < 3; ++j) r[i][j] = row[j] / length;
    } else {
      out->s[i] = 0.0f;
      for (int j = 0; j < 3; ++j) r[i][j] = (i == j) ? 1.0f : 0.0f;
    }
  }
  // A mirrored joint cannot be a quaternion: flip one axis and carry the sign in its scale.
  const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                    r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
  if (det < 0.0f) {
    for (int j = 0; j < 3; ++j) r[0][j] = -r[0][j];
    out->s[0] = -out->s[0];
  }
  RowsToQuat(r, out->q);
  for (int i = 0; i < 3; ++i) out->t[i] = std::isfinite(m[12 + i]) ? m[12 + i] : 0.0f;
}

void JointToMatrix(const JointPose& joint, float* m) {
  float r[3][3];
  QuatToRows(joint.q, r);
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) m[i * 4 + j] = r[i][j] * joint.s[i];
    m[i * 4 + 3] = 0.0f;
  }
  m[12] = joint.t[0];
  m[13] = joint.t[1];
  m[14] = joint.t[2];
  m[15] = 1.0f;
}

void MatrixToRigid(const float* m, float* position, float* rotation) {
  JointPose joint;
  MatrixToJoint(m, &joint);
  std::memcpy(position, m + 12, sizeof(float) * 3);
  std::memcpy(rotation, joint.q, sizeof(float) * 4);
}

void RigidToMatrix(const float* position, const float* rotation, float* m) {
  JointPose joint;
  std::memcpy(joint.q, rotation, sizeof(float) * 4);
  std::memcpy(joint.t, position, sizeof(float) * 3);
  joint.s[0] = joint.s[1] = joint.s[2] = 1.0f;
  JointToMatrix(joint, m);
}

std::size_t EncodePose(const Pose& pose, DeltaState* state, std::uint8_t* out) {
  std::uint8_t* p = out;
  WriteU32(p, pose.time_ms);
  for (float v : pose.position) WriteF32(p, v);
  float rotation[4] = {pose.rotation[0], pose.rotation[1], pose.rotation[2], pose.rotation[3]};
  Normalize4(rotation);
  for (float v : rotation) WriteI16(p, Quantize16(v, 1.0f / 32767.0f));
  std::uint8_t* mask = p;
  std::memset(mask, 0, kMaskBytes);
  p += kMaskBytes;
  for (std::uint32_t j = 0; j < kJointCount; ++j) {
    std::uint8_t packed[12];
    PackJoint(pose.joints[j], packed);
    if (state->primed && NearlySame(packed, state->packed[j])) continue;
    std::memcpy(state->packed[j], packed, 12);
    mask[j >> 3] |= static_cast<std::uint8_t>(1u << (j & 7));
    std::memcpy(p, packed, 12);
    p += 12;
  }
  state->primed = true;
  std::uint8_t* scaled_count = p++;
  *scaled_count = 0;
  for (std::uint32_t j = 0; j < kJointCount; ++j) {
    if (!IsScaled(pose.joints[j])) continue;
    *p++ = static_cast<std::uint8_t>(j);
    for (float s : pose.joints[j].s) *p++ = static_cast<std::uint8_t>(PackScale(s));
    ++*scaled_count;
  }
  return static_cast<std::size_t>(p - out);
}

bool DecodePose(const std::uint8_t* data, std::size_t size, DeltaState* state, Pose* pose) {
  const std::size_t header = 4 + 12 + 8 + kMaskBytes;
  if (size < header + 1) return false;
  const std::uint8_t* p = data;
  const std::uint8_t* end = data + size;
  pose->time_ms = ReadU32(p);
  for (float& v : pose->position) {
    v = ReadF32(p);
    if (!std::isfinite(v) || std::fabs(v) > 1.0e6f) return false;
  }
  for (float& v : pose->rotation) v = static_cast<float>(ReadI16(p)) / 32767.0f;
  Normalize4(pose->rotation);
  const std::uint8_t* mask = p;
  p += kMaskBytes;
  std::uint32_t changed = 0;
  for (std::uint32_t j = 0; j < kJointCount; ++j) {
    if (mask[j >> 3] & (1u << (j & 7))) ++changed;
  }
  if (!state->primed && changed != kJointCount) return false;  // a delta with nothing to apply it to
  if (static_cast<std::size_t>(end - p) < static_cast<std::size_t>(changed) * 12 + 1) return false;
  for (std::uint32_t j = 0; j < kJointCount; ++j) {
    if (mask[j >> 3] & (1u << (j & 7))) {
      std::memcpy(state->packed[j], p, 12);
      p += 12;
    }
    UnpackJoint(state->packed[j], &pose->joints[j]);
    pose->joints[j].s[0] = pose->joints[j].s[1] = pose->joints[j].s[2] = 1.0f;
  }
  state->primed = true;
  const std::uint32_t scaled = *p++;
  if (static_cast<std::size_t>(end - p) != static_cast<std::size_t>(scaled) * 4) return false;
  for (std::uint32_t i = 0; i < scaled; ++i) {
    const std::uint32_t j = *p++;
    if (j >= kJointCount) return false;
    for (float& s : pose->joints[j].s) s = static_cast<float>(static_cast<std::int8_t>(*p++)) * kScaleStep;
  }
  return true;
}

void Interpolate(const Pose& a, const Pose& b, float t, Pose* out) {
  if (t <= 0.0f) { *out = a; return; }
  if (t >= 1.0f) { *out = b; return; }
  const float dx = b.position[0] - a.position[0], dy = b.position[1] - a.position[1], dz = b.position[2] - a.position[2];
  const float limit = static_cast<float>(sheets::settings::teleport_distance);
  if (dx * dx + dy * dy + dz * dz > limit * limit) { *out = b; return; }  // a jump, not a glide
  const auto blend_quat = [t](const float* qa, const float* qb, float* qo) {
    const float dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    for (int i = 0; i < 4; ++i) qo[i] = qa[i] + (qb[i] * sign - qa[i]) * t;
    Normalize4(qo);
  };
  out->time_ms = a.time_ms + static_cast<std::uint32_t>(static_cast<float>(b.time_ms - a.time_ms) * t);
  for (int i = 0; i < 3; ++i) out->position[i] = a.position[i] + (b.position[i] - a.position[i]) * t;
  blend_quat(a.rotation, b.rotation, out->rotation);
  for (std::uint32_t j = 0; j < kJointCount; ++j) {
    const JointPose& ja = a.joints[j];
    const JointPose& jb = b.joints[j];
    JointPose& jo = out->joints[j];
    blend_quat(ja.q, jb.q, jo.q);
    for (int i = 0; i < 3; ++i) {
      jo.t[i] = ja.t[i] + (jb.t[i] - ja.t[i]) * t;
      jo.s[i] = ja.s[i] + (jb.s[i] - ja.s[i]) * t;
    }
  }
}

}  // namespace mt::codec
