// Miles Together - everything that touches the running game.
// Sheet rows: systems/compatibility_gate, frame_hook, local_capture, double_spawn, double_pose,
// and the in-game half of systems/test_mode.
//
// Every address, offset and id used here comes from src/generated/sheets.h.
#include "game.h"

#include <cmath>

#include "common.h"

#include "MinHook.h"
#include "codec.h"
#include "melty.h"
#include "net.h"

namespace mt::game {
namespace {

namespace fn = sheets::fn;
namespace layout = sheets::layout;
namespace settings = sheets::settings;

std::uint8_t* g_base = nullptr;

template <typename T>
T At(const void* base, std::size_t offset) {
  T value;
  std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
  return value;
}

template <typename F>
F Native(std::uint32_t rva) {
  return reinterpret_cast<F>(g_base + rva);
}

// ---- systems/compatibility_gate ----

bool Compatible() {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  if (nt->FileHeader.TimeDateStamp != sheets::compatibility::pe_timestamp || nt->OptionalHeader.SizeOfImage != sheets::compatibility::size_of_image) {
    Log("unsupported game build (timestamp %08lx, image size %08lx); the mod stays off", nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    return false;
  }
  for (const sheets::NativeFunction& function : sheets::native_functions) {
    if (std::memcmp(g_base + function.rva, function.signature, function.signature_size) != 0) {
      Log("game code at '%s' is not what this mod was built for; the mod stays off", function.id);
      return false;
    }
  }
  return true;
}

// ---- engine access ----

std::uint32_t HandleOf(const void* object) {
  return ((At<std::uint16_t>(object, layout::object_generation) & 0x7ffu) << 20) | (At<std::uint32_t>(object, layout::object_index) & 0xfffffu);
}

void* Resolve(std::uint32_t handle) {
  if (handle == 0) return nullptr;
  return Native<fn::resolve_handle_t>(fn::resolve_handle_rva)(&handle);
}

// The live actor of an object, or null.
void* ActorOf(const void* object) {
  void* actor = At<void*>(object, layout::object_actor);
  if (!actor || At<std::uint8_t>(actor, layout::actor_invalid_flag) != 0) return nullptr;
  return actor;
}

void* FindComponent(const void* object, const char* name) {
  const std::uint32_t capacity = At<std::uint16_t>(object, layout::object_dense_capacity);
  const auto* table = At<void* const*>(object, layout::object_dense_table);
  if (capacity == 0 || capacity > 4096 || !table) return nullptr;
  for (std::uint32_t i = 0; i < capacity; ++i) {
    const void* descriptor = table[i * 2];
    void* instance = table[i * 2 + 1];
    if (!descriptor || !instance) continue;
    const char* component_name = At<const char*>(descriptor, layout::component_descriptor_name);
    if (component_name && std::strcmp(component_name, name) == 0) return instance;
  }
  return nullptr;
}

void* LoadedAsset(std::uint64_t id) {
  return Native<fn::actor_asset_lookup_t>(fn::actor_asset_lookup_rva)(g_base + sheets::data::actor_asset_manager_rva, id);
}

Suit ReadSuit(const void* skin_manager) {
  Suit suit;
  const std::uint32_t count = At<std::uint32_t>(skin_manager, layout::skin_part_count);
  const auto* parts = At<const std::uint8_t*>(skin_manager, layout::skin_parts);
  if (!parts || count > 256) return suit;
  for (std::uint32_t i = 0; i < count && suit.count < settings::max_suit_parts; ++i) {
    const std::uint64_t id = At<std::uint64_t>(parts + static_cast<std::size_t>(i) * layout::skin_part_stride, layout::skin_part_config);
    if (id != 0) suit.ids[suit.count++] = id;
  }
  return suit;
}

void AddSuitParts(void* skin_manager, const Suit& suit) {
  // The argument the engine's own add-parts method reads: a list of items, each naming a skin config.
  static std::uint8_t list[0x200];
  static std::uint8_t items[settings::max_suit_parts * layout::skin_item_stride];
  std::memset(list, 0, sizeof(list));
  std::memset(items, 0, sizeof(items));
  for (std::uint32_t i = 0; i < suit.count; ++i) std::memcpy(items + i * layout::skin_item_stride + layout::skin_item_config, &suit.ids[i], sizeof(std::uint64_t));
  void* items_pointer = items;
  const std::uint32_t count = suit.count;
  std::memcpy(list + layout::skin_list_items, &items_pointer, sizeof(void*));
  std::memcpy(list + layout::skin_list_count, &count, sizeof(count));
  using AddParts = void (*)(void* self, void* list);
  const auto* vtable = At<const std::uint8_t*>(skin_manager, 0);
  At<AddParts>(vtable, layout::skin_add_parts_vfunc)(skin_manager, list);
}

// The mask, hood and body of a suit are body groups the game shows per look. A freshly
// dressed double does not get them by itself, so show the ones its own look lists.
void ShowLook(void* object) {
  void* random_look = FindComponent(object, "RandomLook");
  if (!random_look) return;
  const void* look = Native<fn::resolve_selected_look_t>(fn::resolve_selected_look_rva)(random_look);
  if (!look) return;
  const void* entries = At<const void*>(look, layout::look_entries);
  const std::uint32_t count = At<std::uint32_t>(look, layout::look_entry_count);
  if (!entries || count == 0 || count > 64) return;
  void* root = ActorOf(object);
  void* visible = Native<fn::resolve_visible_body_actor_t>(fn::resolve_visible_body_actor_rva)(object);
  const auto apply = Native<fn::apply_body_groups_t>(fn::apply_body_groups_rva);
  if (visible && At<std::uint8_t>(visible, layout::actor_invalid_flag) == 0) apply(visible, entries, count, false);
  if (root && root != visible) apply(root, entries, count, false);
}

// ---- systems/local_capture ----

struct Local {
  bool valid = false;
  void* actor = nullptr;
  const float* joints = nullptr;
  void* skin_manager = nullptr;
};

Local ReadLocal() {
  Local local;
  void* object = Resolve(At<std::uint32_t>(g_base, sheets::data::local_hero_handle_rva));
  if (!object) return local;
  // Only a hero made from the known hero actor is certain to have the 241-joint rig.
  const void* asset = At<const void*>(object, layout::object_asset);
  if (!asset || asset != LoadedAsset(sheets::assets::hero_actor)) return local;
  local.actor = ActorOf(object);
  if (!local.actor) return local;
  local.joints = At<const float*>(local.actor, layout::actor_joints);
  if (!local.joints || local.joints[15] != 1.0f || local.joints[(kJointCount - 1) * 16 + 15] != 1.0f) return local;
  // The main menu has a hero too (Miles on the train). The actor the double is made from is
  // only loaded once a save is running, so its presence tells the two apart.
  if (!LoadedAsset(sheets::assets::puppet_actor)) return local;
  local.skin_manager = FindComponent(object, "SkinManager");
  local.valid = true;
  return local;
}

void CapturePose(const Local& local, Pose* pose) {
  pose->time_ms = NowMs();
  codec::MatrixToRigid(static_cast<const float*>(local.actor) + layout::actor_matrix / sizeof(float), pose->position, pose->rotation);
  for (std::uint32_t j = 0; j < kJointCount; ++j) codec::MatrixToJoint(local.joints + j * 16, &pose->joints[j]);
}

// ---- systems/double_spawn and systems/double_pose ----

struct Double {
  std::uint32_t handle = 0;
  Suit suit;
  std::uint32_t spawned_ms = 0;
  std::uint32_t frames_posed = 0;
  std::uint32_t frames_kept = 0;  // frames on which the pose written the frame before was still in place
  float last_probe[16] = {};      // the probe joint as last written
  std::uint32_t rest_frames = 0;  // frames to wait after a destroy before spawning again
};

Double g_double;
constexpr std::uint32_t kProbeJoint = 20;  // an arm joint: moves in every animation
std::uint32_t g_settled_frames = 0;

void RemoveDouble() {
  if (g_double.handle != 0 && Resolve(g_double.handle)) Native<fn::destroy_by_handle_t>(fn::destroy_by_handle_rva)(g_double.handle);
  g_double = Double{};
  g_double.rest_frames = 30;
}

void* SpawnDouble(const Pose& pose, const Suit& suit) {
  if (!LoadedAsset(sheets::assets::puppet_actor)) return nullptr;
  float matrix[16];
  codec::RigidToMatrix(pose.position, pose.rotation, matrix);
  void* object = Native<fn::spawn_actor_t>(fn::spawn_actor_rva)(sheets::assets::puppet_actor, nullptr, matrix);
  if (!object) return nullptr;
  g_double.handle = HandleOf(object);
  g_double.suit = suit;
  g_double.spawned_ms = NowMs();
  g_double.frames_posed = 0;
  struct Batch {
    std::uint32_t* handles;
    std::int32_t count;
  } batch{&g_double.handle, 1};
  Native<fn::activate_objects_t>(fn::activate_objects_rva)(g_base + sheets::data::object_manager_rva, &batch, 1);
  if (void* skin_manager = FindComponent(object, "SkinManager")) AddSuitParts(skin_manager, suit);
  Log("spawned the other player's double (handle %08x, %u suit parts)", g_double.handle, suit.count);
  return object;
}

// Returns the double's object once it is posed this frame, or null.
void* UpdateDouble(const net::RemoteView& remote, const Suit& fallback_suit) {
  void* object = Resolve(g_double.handle);
  if (g_double.handle != 0 && !object) g_double = Double{};  // the game removed it, for example on a load
  if (g_double.rest_frames > 0) --g_double.rest_frames;

  const bool wanted = remote.connected && !remote.away && remote.has_pose;
  if (!wanted) {
    if (object) RemoveDouble();
    return nullptr;
  }
  const Suit& suit = remote.has_suit && remote.suit.count > 0 ? remote.suit : fallback_suit;
  if (object && !(g_double.suit == suit) && suit.count > 0) {
    RemoveDouble();  // a new suit: spawn again, dressed in it
    return nullptr;
  }
  if (!object) {
    if (g_double.rest_frames > 0 || g_settled_frames < settings::spawn_settle_frames || suit.count == 0) return nullptr;
    object = SpawnDouble(remote.pose, suit);
    if (!object) return nullptr;
  }
  void* actor = ActorOf(object);
  if (!actor) return nullptr;
  auto* joints = At<float*>(actor, layout::actor_joints);
  if (!joints) return nullptr;
  float matrix[16];
  codec::RigidToMatrix(remote.pose.position, remote.pose.rotation, matrix);
  Native<fn::set_actor_transform_t>(fn::set_actor_transform_rva)(actor, matrix, static_cast<std::uint8_t*>(actor) + layout::actor_transform_state);
  if (g_double.frames_posed > 0 && std::memcmp(joints + kProbeJoint * 16, g_double.last_probe, sizeof(g_double.last_probe)) == 0) ++g_double.frames_kept;
  for (std::uint32_t j = 0; j < kJointCount; ++j) codec::JointToMatrix(remote.pose.joints[j], joints + j * 16);
  // Suit parts arrive over several frames; keep showing the look until they have settled.
  if (g_double.frames_posed % settings::look_refresh_frames == 0 && NowMs() - g_double.spawned_ms < settings::look_refresh_period) ShowLook(object);
  std::memcpy(g_double.last_probe, joints + kProbeJoint * 16, sizeof(g_double.last_probe));
  ++g_double.frames_posed;
  return object;
}

// ---- systems/test_mode: a mirrored double, through the same codec the network uses ----

bool g_mirror = false;
codec::DeltaState g_mirror_encode;
codec::DeltaState g_mirror_decode;
std::uint32_t g_hosting_deadline = 0;

void MirrorView(const Local& local, const Pose& pose, const Suit& suit, net::RemoteView* view) {
  static std::uint8_t packed[codec::kMaxPoseBytes];
  const std::size_t size = codec::EncodePose(pose, &g_mirror_encode, packed);
  view->connected = true;
  view->away = false;
  view->has_suit = suit.count > 0;
  view->suit = suit;
  view->has_pose = codec::DecodePose(packed, size, &g_mirror_decode, &view->pose);
  // Stand the double 1.5 m to the hero's right.
  const float* right = static_cast<const float*>(local.actor);
  for (int i = 0; i < 3; ++i) view->pose.position[i] += right[i] * 1.5f;
}

void ReportChecks(void* double_object) {
  namespace checks = sheets::checks;
  const std::uint32_t now = NowMs();
  if (g_settled_frames >= settings::spawn_settle_frames) melty::ReportPass(checks::hero_found);
  if (net::OpenToOthers()) {
    melty::ReportPass(checks::hosting_open);
  } else if (g_hosting_deadline != 0 && now > g_hosting_deadline) {
    melty::ReportFail(checks::hosting_open, "no port could be opened and no join completed within 60 seconds");
  }
  if (!double_object) {
    if (g_double.handle == 0 && g_settled_frames > settings::spawn_settle_frames + 1200) melty::ReportFail(checks::double_spawned, "the double never spawned");
    return;
  }
  const bool active = (At<std::uint16_t>(double_object, layout::object_flags) & 1u) != 0;
  void* skin_manager = FindComponent(double_object, "SkinManager");
  const bool dressed = skin_manager && ReadSuit(skin_manager).count > 0;
  if (active && dressed) {
    melty::ReportPass(checks::double_spawned);
  } else if (now - g_double.spawned_ms > 20000) {
    melty::ReportFail(checks::double_spawned, active ? "no suit part appeared on the double within 20 seconds" : "the double never became active");
  }
  if (g_double.frames_posed >= 300) {
    // The game must leave the written pose in place from one frame to the next.
    if (g_double.frames_kept + 10 >= g_double.frames_posed) {
      melty::ReportPass(checks::pose_mirrored);
    } else {
      melty::ReportFail(checks::pose_mirrored, "the game overwrote the double's pose between frames");
    }
  }
}

// ---- systems/frame_hook ----

using TickFunction = void* (*)(void*, void*, void*, void*);
TickFunction g_original_tick = nullptr;
bool g_disabled = false;
std::uint32_t g_faults = 0;
std::uint32_t g_last_send_ms = 0;
Suit g_local_suit;        // what the local hero wears, once it has stayed the same for two seconds
Suit g_candidate_suit;
std::uint32_t g_candidate_since = 0;

__attribute__((noinline)) void Tick() {
  const std::uint32_t now = NowMs();
  const Local local = ReadLocal();
  g_settled_frames = local.valid ? g_settled_frames + 1 : 0;

  static Pose pose;  // too large for the game thread's stack to be a good home
  bool captured = false;
  if (local.valid) {
    if (local.skin_manager) {
      const Suit suit = ReadSuit(local.skin_manager);
      if (!(suit == g_candidate_suit)) {
        g_candidate_suit = suit;
        g_candidate_since = now;
      } else if (suit.count > 0 && now - g_candidate_since >= 2000 && !(suit == g_local_suit)) {
        g_local_suit = suit;
        net::SubmitSuit(suit);
        char ids[sizeof(suit.ids) / sizeof(suit.ids[0]) * 18 + 1] = "";
        for (std::uint32_t i = 0; i < suit.count; ++i) std::snprintf(ids + std::strlen(ids), 18, " %016llx", static_cast<unsigned long long>(suit.ids[i]));
        Log("your suit has %u parts:%s", suit.count, ids);
      }
    }
    if (now - g_last_send_ms >= 1000 / settings::send_rate) {
      g_last_send_ms = now;
      CapturePose(local, &pose);
      net::SubmitPose(pose);
      captured = true;
    }
  } else {
    net::SubmitAway();
  }

  static net::RemoteView view;
  net::SampleRemote(now, &view);
  if (!view.connected && g_mirror) {
    if (!local.valid) {
      view = net::RemoteView{};
    } else {
      if (!captured) CapturePose(local, &pose);
      MirrorView(local, pose, g_local_suit, &view);
    }
  }
  void* double_object = UpdateDouble(view, g_local_suit);
  if (melty::TestMode()) ReportChecks(double_object);

  // One log line per change of state, so a player's log tells the story of a session.
  static int logged_state = -1;
  const int state = (local.valid ? 1 : 0) | (view.connected ? 2 : 0) | (view.connected && !view.away ? 4 : 0) | (double_object ? 8 : 0);
  if (state != logged_state) {
    logged_state = state;
    Log("state: you are %s; other player %s%s", local.valid ? "in the city" : "not in the city", !view.connected ? "not connected" : view.away ? "connected, not in the city" : "connected and in the city",
        double_object ? "; their double is shown" : "");
  }
}

LONG FaultFilter(DWORD code) {
  return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

void GuardedTick() {
  __try {
    Tick();
  } __except (FaultFilter(GetExceptionCode())) {
    ++g_faults;
    g_double = Double{};  // never touch that object again
    g_double.rest_frames = 600;
    if (g_faults >= settings::fault_limit) g_disabled = true;
    Log("caught a fault in the tick (%u of %u)%s", g_faults, settings::fault_limit, g_disabled ? "; the mod is now off until the game restarts" : "");
  }
}

void* HookedTick(void* a, void* b, void* c, void* d) {
  if (!g_disabled) {
    static LARGE_INTEGER frequency = {};
    static std::uint64_t total = 0, worst = 0, frames = 0;
    static std::uint32_t reported_ms = 0;
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER before, after;
    QueryPerformanceCounter(&before);
    GuardedTick();
    QueryPerformanceCounter(&after);
    const std::uint64_t cost = static_cast<std::uint64_t>(after.QuadPart - before.QuadPart);
    total += cost;
    worst = cost > worst ? cost : worst;
    ++frames;
    const std::uint32_t now = NowMs();
    if (now - reported_ms >= settings::cost_report_period) {
      reported_ms = now;
      const double to_us = 1e6 / static_cast<double>(frequency.QuadPart);
      Log("cost per frame: %.0f microseconds on average, %.0f at worst, over %llu frames", static_cast<double>(total) / static_cast<double>(frames) * to_us, static_cast<double>(worst) * to_us,
          static_cast<unsigned long long>(frames));
      total = worst = frames = 0;
    }
  }
  return g_original_tick(a, b, c, d);
}

}  // namespace

bool Install() {
  g_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
  if (!g_base || !Compatible()) return false;
  g_mirror = melty::TestMode() || melty::MirrorRequested();
  g_hosting_deadline = NowMs() + 60000;
  void* target = g_base + fn::frame_tick_rva;
  if (MH_Initialize() != MH_OK || MH_CreateHook(target, reinterpret_cast<void*>(&HookedTick), reinterpret_cast<void**>(&g_original_tick)) != MH_OK || MH_EnableHook(target) != MH_OK) {
    Log("could not install the frame tick; the mod stays off");
    return false;
  }
  Log("ready on game %s%s", sheets::compatibility::game_version, g_mirror ? " (mirror double on)" : "");
  return true;
}

}  // namespace mt::game
