// Miles Together - everything that touches the running game.
// Sheet rows: systems/compatibility_gate, frame_hook, local_capture, double_spawn, double_pose,
// partner_map_marker, the game half of world_sync and enemy_sync, pause_menu_options,
// and the in-game half of systems/test_mode.
//
// Every address, offset and id used here comes from src/generated/sheets.h.
#include "game.h"

#include <atomic>
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

template <typename T>
void Put(void* base, std::size_t offset, T value) {
  std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
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
  void* object = nullptr;
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
  local.object = object;
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

// ---- systems/partner_map_marker ----
// The double comes from the hero actor, whose MapElement has the hero type. The minimap draws
// that type only for the local player, so the double never showed. Before activation registers
// the element, give it the person-marker type and the always-show flag.
constexpr std::uint32_t kHeroMapType = 1;

void ShowOnMinimap(void* object) {
  void* map_element = FindComponent(object, "MapElement");
  if (!map_element) {
    Log("the double has no map element; no minimap marker");
    return;
  }
  const std::uint32_t type = At<std::uint32_t>(map_element, layout::map_element_type);
  if (type != kHeroMapType) {
    Log("the double's map element has type %u, not the hero type; no minimap marker", type);
    return;
  }
  Put<std::uint32_t>(map_element, layout::map_element_type, settings::partner_map_type);
  Put<std::uint8_t>(map_element, layout::map_element_always_show, 1);
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
  ShowOnMinimap(object);
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

// ---- systems/world_sync ----
// The host's lighting mode (time of day and weather) is the one both players see. Each side
// sends its own; the joiner asks its game to switch when the host's differs.
std::uint32_t g_world_requested_ms = 0;
bool g_world_requested = false;

void SyncWorld(const Local& local, const net::RemoteView& view, std::uint32_t now) {
  if (!local.valid) return;
  std::uint8_t* lighting = At<std::uint8_t*>(g_base + sheets::data::lighting_system_rva, 0);
  if (!lighting) return;
  const std::int32_t mode = At<std::int32_t>(lighting, layout::lighting_mode_index);
  if (mode < 0 || mode >= static_cast<std::int32_t>(settings::lighting_mode_count)) return;
  net::SubmitWorld(static_cast<std::uint8_t>(mode));
  if (net::Hosting() || !view.has_world || view.lighting_mode == mode) return;
  if (g_world_requested && now - g_world_requested_ms < settings::world_retry_period) return;
  g_world_requested = true;
  g_world_requested_ms = now;
  Native<fn::set_lighting_mode_t>(fn::set_lighting_mode_rva)(lighting + layout::lighting_request, view.lighting_mode);
  Log("matching the host's time of day and weather (lighting mode %u; this game had %d)", view.lighting_mode, mode);
}

// ---- systems/enemy_sync ----
// The host shares the enemies near either player; the joiner shows each as an inert puppet of
// the same actor, posed from the host's joints. A bot made with spawn_actor has no AI and no
// animation of its own, so the joints written each frame stay in place.
constexpr std::uint32_t kMaxEnemies = settings::max_enemies;

// The number of joints in a live bot's rig, from its animation controller; 0 when it has no
// rig set up (a bot that is not animating, such as a freshly spawned puppet). Counting the
// matrices in the joint array is not safe: the memory after it can look like more joints.
std::uint32_t RigJointCount(void* object) {
  void* controller = FindComponent(object, "AnimControllerComponent");
  if (!controller) return 0;
  std::uint32_t best = 0;
  for (std::uint32_t k = 0; k < settings::anim_rig_slot_count; ++k) {
    const void* rig = At<const void*>(controller, layout::anim_rig_slots + k * layout::anim_rig_slot_stride);
    if (!rig) continue;
    const std::uint32_t n = At<std::uint16_t>(rig, layout::rig_joint_count);
    best = n > best ? n : best;
  }
  return best;
}

const float* ActorPosition(const void* actor) {
  return reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(actor) + layout::actor_matrix) + 12;
}

// Whether objects made from an actor asset are enemies (have a BotHealth), learnt once per asset.
struct AssetVerdict {
  const void* asset = nullptr;
  bool enemy = false;
};
constexpr std::size_t kVerdicts = 1024;
AssetVerdict g_verdicts[kVerdicts];
bool g_mirror_civilians = false;  // test aid: the mirror also treats civilians as enemies

bool IsEnemyAsset(void* object, const void* asset) {
  std::size_t at = (reinterpret_cast<std::uintptr_t>(asset) >> 4) % kVerdicts;
  for (std::size_t probe = 0; probe < kVerdicts; ++probe, at = (at + 1) % kVerdicts) {
    if (g_verdicts[at].asset == asset) return g_verdicts[at].enemy;
    if (g_verdicts[at].asset == nullptr) {
      g_verdicts[at].asset = asset;
      g_verdicts[at].enemy = FindComponent(object, "BotHealth") != nullptr || (g_mirror_civilians && FindComponent(object, "CivilianBot") != nullptr);
      return g_verdicts[at].enemy;
    }
  }
  return false;  // table full: treat unknown assets as not enemies
}

// Joiner side (and the host's mirror test): the puppets.
struct Puppet {
  std::uint32_t remote_id = 0;  // 0 = free
  std::uint32_t handle = 0;
  std::uint64_t asset = 0;
  std::uint32_t local_joints = 0;
};
Puppet g_puppets[kMaxEnemies];
// Actor assets the joiner has asked the game to load, with when it asked.
struct AssetRequest {
  std::uint64_t asset = 0;
  std::uint32_t asked_ms = 0;
  bool reported = false;
};
AssetRequest g_asset_requests[32];

bool IsPuppetHandle(std::uint32_t handle) {
  for (const Puppet& p : g_puppets) {
    if (p.remote_id != 0 && p.handle == handle) return true;
  }
  return false;
}

void RemovePuppet(Puppet* p) {
  if (p->handle != 0 && Resolve(p->handle)) Native<fn::destroy_by_handle_t>(fn::destroy_by_handle_rva)(p->handle);
  *p = Puppet{};
}

void RemoveAllPuppets() {
  for (Puppet& p : g_puppets) {
    if (p.remote_id != 0) RemovePuppet(&p);
  }
}

// Each game only keeps the actors its own surroundings need, so the host's thugs are often not
// loaded on the joiner. Ask the game to load a missing one once; the puppet appears when it has.
// True while an actor this mod asked for may still be loading. The asset table lists an
// actor as soon as it is requested, before its parts have arrived.
bool AssetSettling(std::uint64_t asset, std::uint32_t now) {
  for (const AssetRequest& r : g_asset_requests) {
    if (r.asset == asset) return now - r.asked_ms < settings::asset_settle_time;
  }
  return false;
}

void RequestMissingAsset(std::uint64_t asset, std::uint32_t now) {
  AssetRequest* free_slot = nullptr;
  for (AssetRequest& r : g_asset_requests) {
    if (r.asset == asset) {
      if (!r.reported && now - r.asked_ms > 10000) {
        r.reported = true;
        Log("actor %016llx has not loaded 10 s after asking; that enemy is not shown", static_cast<unsigned long long>(asset));
      }
      return;
    }
    if (r.asset == 0 && !free_slot) free_slot = &r;
  }
  if (!free_slot) return;
  free_slot->asset = asset;
  free_slot->asked_ms = now;
  Native<fn::request_actor_asset_t>(fn::request_actor_asset_rva)(g_base + sheets::data::actor_asset_manager_rva, asset, nullptr, nullptr, nullptr);
  Log("loading actor %016llx for the host's enemy", static_cast<unsigned long long>(asset));
}

void UpdatePuppets(const net::RemoteEnemy* enemies, std::uint32_t count) {
  bool spawned_this_frame = false;
  for (Puppet& p : g_puppets) {
    if (p.remote_id == 0) continue;
    bool listed = false;
    for (std::uint32_t i = 0; i < count; ++i) listed = listed || enemies[i].id == p.remote_id;
    if (!listed) RemovePuppet(&p);
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    const net::RemoteEnemy& e = enemies[i];
    Puppet* p = nullptr;
    for (Puppet& q : g_puppets) {
      if (q.remote_id == e.id) p = &q;
    }
    if (p && p->asset != e.asset) {
      RemovePuppet(p);
      p = nullptr;
    }
    void* object = p ? Resolve(p->handle) : nullptr;
    if (p && !object) {  // the game removed it, for example on a load
      *p = Puppet{};
      p = nullptr;
    }
    if (!p) {
      if (spawned_this_frame) continue;  // one new puppet per frame: activation does more work than posing
      for (Puppet& q : g_puppets) {
        if (q.remote_id == 0) {
          p = &q;
          break;
        }
      }
      if (!p) continue;
      if (!LoadedAsset(e.asset)) {
        RequestMissingAsset(e.asset, NowMs());
        continue;
      }
      if (AssetSettling(e.asset, NowMs())) continue;
      float matrix[16];
      codec::RigidToMatrix(e.pose.position, e.pose.rotation, matrix);
      object = Native<fn::spawn_actor_t>(fn::spawn_actor_rva)(e.asset, nullptr, matrix);
      if (!object) continue;
      p->remote_id = e.id;
      p->asset = e.asset;
      p->handle = HandleOf(object);
      struct Batch {
        std::uint32_t* handles;
        std::int32_t count;
      } batch{&p->handle, 1};
      Native<fn::activate_objects_t>(fn::activate_objects_rva)(g_base + sheets::data::object_manager_rva, &batch, 1);
      // Same actor asset, same rig: the host's count is this puppet's count too. A freshly
      // spawned puppet has no rig set up of its own to ask.
      p->local_joints = e.joint_count;
      spawned_this_frame = true;
      Log("showing the host's enemy %08x (actor %016llx, %u joints)", e.id, static_cast<unsigned long long>(e.asset), e.joint_count);
    }
    void* actor = ActorOf(object);
    if (!actor) continue;
    auto* joints = At<float*>(actor, layout::actor_joints);
    if (!joints) continue;
    float matrix[16];
    codec::RigidToMatrix(e.pose.position, e.pose.rotation, matrix);
    Native<fn::set_actor_transform_t>(fn::set_actor_transform_rva)(actor, matrix, static_cast<std::uint8_t*>(actor) + layout::actor_transform_state);
    const std::uint32_t n = e.joint_count < p->local_joints ? e.joint_count : p->local_joints;
    for (std::uint32_t j = 0; j < n; ++j) codec::JointToMatrix(e.pose.joints[j], joints + j * 16);
  }
}

// Host side: which enemies are shared.
struct Tracked {
  std::uint32_t handle = 0;
  std::uint64_t asset = 0;
  std::uint8_t joints = 0;
};
Tracked g_tracked[kMaxEnemies];
std::uint32_t g_tracked_count = 0;
std::uint32_t g_enemy_scan_ms = 0;
std::uint32_t g_enemy_send_ms = 0;

void ScanEnemies(const float* const* centers, std::uint32_t center_count, std::uint32_t now) {
  g_enemy_scan_ms = now;
  std::uint8_t* table = At<std::uint8_t*>(g_base + sheets::data::object_table_rva, 0);
  const std::uint32_t slots = At<std::uint32_t>(g_base + sheets::data::object_table_count_rva, 0);
  if (!table || slots == 0 || slots > 4000000) return;
  struct Candidate {
    float d2;
    Tracked t;
  };
  Candidate best[kMaxEnemies];
  std::uint32_t found = 0;
  const float range = static_cast<float>(settings::enemy_range) * (g_mirror_civilians ? 3.0f : 1.0f);  // the civilian test reaches further
  const float range2 = range * range;
  for (std::uint32_t i = 0; i < slots; ++i) {
    std::uint8_t* object = table + static_cast<std::size_t>(i) * layout::object_slot_size;
    if (At<std::uint16_t>(object, layout::object_generation) == 0) continue;
    if ((At<std::uint16_t>(object, layout::object_flags) & 1u) == 0) continue;
    const void* asset = At<const void*>(object, layout::object_asset);
    if (!asset || !IsEnemyAsset(object, asset)) continue;
    const std::uint32_t handle = HandleOf(object);
    if (IsPuppetHandle(handle)) continue;
    void* actor = ActorOf(object);
    if (!actor) continue;
    const float* pos = ActorPosition(actor);
    float d2 = range2 + 1.0f;
    for (std::uint32_t c = 0; c < center_count; ++c) {
      const float dx = pos[0] - centers[c][0], dy = pos[1] - centers[c][1], dz = pos[2] - centers[c][2];
      const float d = dx * dx + dy * dy + dz * dz;
      d2 = d < d2 ? d : d2;
    }
    if (d2 > range2) continue;
    if (!At<const float*>(actor, layout::actor_joints)) continue;
    const std::uint32_t joint_count = RigJointCount(object);
    if (joint_count == 0 || joint_count > kJointCount) continue;  // not animating, or a rig too big to send
    const Candidate c{d2, Tracked{handle, At<std::uint64_t>(asset, layout::asset_id), static_cast<std::uint8_t>(joint_count)}};
    if (found < kMaxEnemies) {
      best[found++] = c;
    } else {
      std::uint32_t worst = 0;
      for (std::uint32_t k = 1; k < found; ++k) {
        if (best[k].d2 > best[worst].d2) worst = k;
      }
      if (c.d2 < best[worst].d2) best[worst] = c;
    }
  }
  for (std::uint32_t k = 0; k < g_tracked_count; ++k) {
    bool kept = false;
    for (std::uint32_t m = 0; m < found; ++m) kept = kept || best[m].t.handle == g_tracked[k].handle;
    if (!kept) net::SubmitEnemyGone(g_tracked[k].handle);
  }
  if (found != g_tracked_count) Log("sharing %u enemies", found);
  for (std::uint32_t m = 0; m < found; ++m) g_tracked[m] = best[m].t;
  g_tracked_count = found;
}

// Captures one tracked enemy; false when it no longer exists.
bool CaptureEnemy(const Tracked& t, std::uint32_t now, Pose* pose) {
  void* object = Resolve(t.handle);
  void* actor = object ? ActorOf(object) : nullptr;
  const float* joints = actor ? At<const float*>(actor, layout::actor_joints) : nullptr;
  if (!joints) return false;
  pose->time_ms = now;
  codec::MatrixToRigid(reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(actor) + layout::actor_matrix), pose->position, pose->rotation);
  for (std::uint32_t j = 0; j < t.joints; ++j) codec::MatrixToJoint(joints + j * 16, &pose->joints[j]);
  return true;
}

// Mirror test on one PC: the host shows puppets of its own enemies 3 m to the side, through the
// same codec the network uses.
bool g_mirror_enemies = false;
codec::DeltaState g_mirror_enemy_encode[kMaxEnemies];
codec::DeltaState g_mirror_enemy_decode[kMaxEnemies];
std::uint32_t g_mirror_enemy_ids[kMaxEnemies] = {};

void SyncEnemies(const Local& local, const net::RemoteView& view, std::uint32_t now) {
  static net::RemoteEnemy enemies[kMaxEnemies];
  if (view.connected && !net::Hosting()) {  // joiner: show the host's enemies
    UpdatePuppets(enemies, local.valid ? net::SampleEnemies(now, enemies, kMaxEnemies) : 0);
    return;
  }
  const bool sharing = view.connected;
  const bool mirror = !view.connected && g_mirror_enemies;
  if (!local.valid || (!sharing && !mirror)) {
    if (g_tracked_count != 0) {
      for (std::uint32_t k = 0; k < g_tracked_count; ++k) net::SubmitEnemyGone(g_tracked[k].handle);
      g_tracked_count = 0;
    }
    RemoveAllPuppets();
    return;
  }
  if (now - g_enemy_scan_ms >= settings::enemy_scan_period) {
    const float* centers[2] = {ActorPosition(local.actor), view.has_pose ? view.pose.position : nullptr};
    ScanEnemies(centers, centers[1] ? 2u : 1u, now);
  }
  if (now - g_enemy_send_ms < 1000 / settings::enemy_send_rate) return;
  g_enemy_send_ms = now;
  static Pose pose;
  std::uint32_t mirrored = 0;
  for (std::uint32_t k = 0; k < g_tracked_count;) {
    if (!CaptureEnemy(g_tracked[k], now, &pose)) {
      net::SubmitEnemyGone(g_tracked[k].handle);
      g_tracked[k] = g_tracked[--g_tracked_count];
      continue;
    }
    if (sharing) {
      net::SubmitEnemy(g_tracked[k].handle, g_tracked[k].asset, g_tracked[k].joints, pose);
    } else {
      static std::uint8_t packed[codec::kMaxPoseBytes];
      if (g_mirror_enemy_ids[k] != g_tracked[k].handle) {
        g_mirror_enemy_ids[k] = g_tracked[k].handle;
        g_mirror_enemy_encode[k].Reset();
        g_mirror_enemy_decode[k].Reset();
      }
      const std::size_t size = codec::EncodePose(pose, &g_mirror_enemy_encode[k], packed, g_tracked[k].joints);
      net::RemoteEnemy& e = enemies[mirrored];
      if (codec::DecodePose(packed, size, &g_mirror_enemy_decode[k], &e.pose, g_tracked[k].joints)) {
        e.id = g_tracked[k].handle;
        e.asset = g_tracked[k].asset;
        e.joint_count = g_tracked[k].joints;
        const float* right = static_cast<const float*>(local.actor);
        for (int i = 0; i < 3; ++i) e.pose.position[i] += right[i] * 3.0f;
        ++mirrored;
      }
    }
    ++k;
  }
  if (mirror) UpdatePuppets(enemies, mirrored);
}

// ---- systems/pause_menu_options ----
// The pause list is the MainMenu of the system menu config; it has room for 16 items and uses
// 12. The mod fills the next slots with its own items (a copy of Resume with a new id and
// label), answers their labels through the text lookup, and acts on them when chosen.
constexpr char kGoToFriendTag[] = "MILESTOGETHER_GO_TO_FRIEND";
constexpr char kStartCrimeTag[] = "MILESTOGETHER_START_CRIME";
constexpr char kGoToFriendText[] = "Go to your friend";
constexpr char kStartCrimeText[] = "Start a crime (test)";
std::uint32_t g_go_to_friend_hash = 0;
std::uint32_t g_start_crime_hash = 0;
std::atomic<bool> g_want_warp{false};
std::atomic<bool> g_want_crime{false};
int g_menu_shown = -1;  // which of our items the list shows now: bit 0 friend, bit 1 crime
bool g_menu_unusable_logged = false;
bool g_menu_disabled = false;  // the hooks could not be installed: never touch the list

using TextLookupFunction = const char* (*)(std::uint32_t, const char*);
TextLookupFunction g_original_text_lookup = nullptr;

const char* HookedTextLookup(std::uint32_t hash, const char* fallback) {
  if (hash != 0 && hash == g_go_to_friend_hash) return kGoToFriendText;
  if (hash != 0 && hash == g_start_crime_hash) return kStartCrimeText;
  return g_original_text_lookup(hash, fallback);
}

using MenuSelectFunction = void (*)(void*, std::uint32_t);
MenuSelectFunction g_original_menu_select = nullptr;

void HookedMenuSelect(void* menu, std::uint32_t id) {
  if (id == settings::menu_go_to_friend_id || id == settings::menu_start_crime_id) {
    (id == settings::menu_go_to_friend_id ? g_want_warp : g_want_crime).store(true);
    g_original_menu_select(menu, 0);  // as Resume: close the menu; the frame tick does the rest
    return;
  }
  g_original_menu_select(menu, id);
}

void UpdatePauseMenu(bool connected) {
  if (g_go_to_friend_hash == 0) {
    g_go_to_friend_hash = Native<fn::string_hash_t>(fn::string_hash_rva)(kGoToFriendTag, 0xEDB88320u);
    g_start_crime_hash = Native<fn::string_hash_t>(fn::string_hash_rva)(kStartCrimeTag, 0xEDB88320u);
  }
  if (g_menu_disabled) return;
  const int wanted = (connected ? 1 : 0) | 2;
  void* entry = Native<fn::actor_asset_lookup_t>(fn::actor_asset_lookup_rva)(g_base + sheets::data::config_asset_manager_rva, sheets::assets::system_menu_config);
  std::uint8_t* config = entry ? At<std::uint8_t*>(entry, layout::asset_entry_object) : nullptr;
  std::uint8_t* items = config ? At<std::uint8_t*>(config, layout::menu_main_items) : nullptr;
  if (!items) return;
  const std::uint32_t count = At<std::uint32_t>(config, layout::menu_main_count);
  const std::uint32_t capacity = At<std::uint32_t>(config, layout::menu_main_capacity);
  const std::uint32_t base = settings::menu_base_items;
  // Already showing what is wanted, and the game has not reloaded the list since.
  const std::uint32_t shown_count = base + (g_menu_shown < 0 ? 0u : static_cast<std::uint32_t>((g_menu_shown & 1) + ((g_menu_shown >> 1) & 1)));
  if (wanted == g_menu_shown && count == shown_count) return;
  if (count < base || count > base + 2 || capacity < base + 2) {
    if (!g_menu_unusable_logged) {
      g_menu_unusable_logged = true;
      Log("the pause list is not as expected (%u items, room for %u); its options are not added", count, capacity);
    }
    return;
  }
  std::uint32_t n = base;
  const auto add = [&](std::uint32_t id, const char* tag, std::uint32_t hash) {
    std::uint8_t* item = items + static_cast<std::size_t>(n) * layout::menu_item_size;
    std::memcpy(item, items, layout::menu_item_size);  // a copy of Resume: a plain command
    Put<std::uint32_t>(item, layout::menu_item_id, id);
    Put<const char*>(item, layout::menu_item_label, tag);
    Put<std::uint32_t>(item, layout::menu_item_label_length, static_cast<std::uint32_t>(std::strlen(tag)));
    Put<std::uint32_t>(item, layout::menu_item_label_hash, hash);
    ++n;
  };
  if (wanted & 1) add(settings::menu_go_to_friend_id, kGoToFriendTag, g_go_to_friend_hash);
  if (wanted & 2) add(settings::menu_start_crime_id, kStartCrimeTag, g_start_crime_hash);
  Put<std::uint32_t>(config, layout::menu_main_count, n);
  if (g_menu_shown < 0) Log("pause list options added");
  g_menu_shown = wanted;
}

// Warping a perched hero only half works: the perch pulls him back. Check a second later and
// warp once more if he did not stay.
struct WarpCheck {
  bool pending = false;
  std::uint32_t due_ms = 0;
  float target[16] = {};
};
WarpCheck g_warp_check;

void WarpHero(void* hero_object, const float* matrix) {
  alignas(16) static std::uint8_t action[0x100];
  std::memset(action, 0, sizeof(action));
  Put<void*>(action, layout::warp_action_owner, hero_object);
  Put<std::uint8_t>(action, layout::warp_action_release, 1);  // leave a perch rather than be pulled back to it
  Native<fn::warp_object_t>(fn::warp_object_rva)(action, hero_object, matrix);
}

void RunPauseMenuOptions(const Local& local, const net::RemoteView& view, std::uint32_t now) {
  UpdatePauseMenu(view.connected && !view.away);
  if (g_want_crime.exchange(false)) {
    float* cooldown = reinterpret_cast<float*>(g_base + sheets::data::random_crime_system_rva + layout::crime_cooldown);
    if (*cooldown > 0.5f) *cooldown = 0.5f;
    Log("starting a crime: the crime system picks one now");
  }
  if (g_want_warp.exchange(false)) {
    if (!local.valid || !view.connected || view.away || !view.has_pose) {
      Log("go to your friend: they are not in the city right now");
    } else {
      float matrix[16];
      codec::RigidToMatrix(view.pose.position, view.pose.rotation, matrix);
      const float offset = static_cast<float>(settings::friend_warp_offset);
      for (int i = 0; i < 3; ++i) matrix[12 + i] += matrix[i] * offset;  // to their side
      WarpHero(local.object, matrix);
      std::memcpy(g_warp_check.target, matrix, sizeof(matrix));
      g_warp_check.pending = true;
      g_warp_check.due_ms = now + 1000;
      Log("going to your friend at %.0f %.0f %.0f", matrix[12], matrix[13], matrix[14]);
    }
  }
  if (g_warp_check.pending && static_cast<std::int32_t>(now - g_warp_check.due_ms) >= 0) {
    g_warp_check.pending = false;
    if (local.valid) {
      const float* at = ActorPosition(local.actor);
      const float dx = at[0] - g_warp_check.target[12], dy = at[1] - g_warp_check.target[13], dz = at[2] - g_warp_check.target[14];
      const float limit = static_cast<float>(settings::warp_retry_distance);
      if (dx * dx + dy * dy + dz * dz > limit * limit) {
        WarpHero(local.object, g_warp_check.target);
        Log("go to your friend: warped again (something pulled you back)");
      }
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
  SyncWorld(local, view, now);
  SyncEnemies(local, view, now);
  RunPauseMenuOptions(local, view, now);
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
  const std::wstring mirror_enemies_file = LocalAppData() + L"MilesTogether\\mirror-enemies.txt";
  g_mirror_enemies = GetFileAttributesW(mirror_enemies_file.c_str()) != INVALID_FILE_ATTRIBUTES;
  if (g_mirror_enemies) {
    // The file may say "civilians": then walking people stand in for enemies, so motion can be
    // checked on one PC without a crime nearby.
    char text[64] = {};
    if (FILE* f = _wfopen(mirror_enemies_file.c_str(), L"rb")) {
      std::fread(text, 1, sizeof(text) - 1, f);
      std::fclose(f);
    }
    g_mirror_civilians = std::strstr(text, "civilians") != nullptr;
  }
  g_hosting_deadline = NowMs() + 60000;
  void* target = g_base + fn::frame_tick_rva;
  if (MH_Initialize() != MH_OK || MH_CreateHook(target, reinterpret_cast<void*>(&HookedTick), reinterpret_cast<void**>(&g_original_tick)) != MH_OK || MH_EnableHook(target) != MH_OK) {
    Log("could not install the frame tick; the mod stays off");
    return false;
  }
  // The pause list options are a convenience: without their hooks the rest still works.
  void* text_target = g_base + fn::text_lookup_rva;
  void* select_target = g_base + fn::menu_select_rva;
  if (MH_CreateHook(text_target, reinterpret_cast<void*>(&HookedTextLookup), reinterpret_cast<void**>(&g_original_text_lookup)) != MH_OK ||
      MH_CreateHook(select_target, reinterpret_cast<void*>(&HookedMenuSelect), reinterpret_cast<void**>(&g_original_menu_select)) != MH_OK ||
      MH_EnableHook(text_target) != MH_OK || MH_EnableHook(select_target) != MH_OK) {
    Log("could not hook the pause list; its options are off");
    g_menu_disabled = true;
  }
  Log("ready on game %s%s%s", sheets::compatibility::game_version, g_mirror ? " (mirror double on)" : "", g_mirror_enemies ? " (mirror enemies on)" : "");
  return true;
}

}  // namespace mt::game
