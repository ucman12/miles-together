// Miles Together - the two-player session (sheet rows systems/session and the network half of
// systems/world_sync).
//
// One TCP connection between two players. The host listens on every address so Melty's
// relay can carry a friend to it; a joiner connects to the address Melty wrote.
// Frame: u16 length (type + body) | u8 type (sheet net_messages) | body.
#include "net.h"

#include <mutex>
#include <vector>

#include "codec.h"
#include "melty.h"

namespace mt::net {
namespace {

constexpr std::uint32_t kMagic = 0x3147544d;  // "MTG1"
constexpr std::size_t kMaxFrame = 4096;
constexpr std::size_t kMaxBacklog = 512 * 1024;
constexpr std::size_t kRing = 16;

using sheets::Message;

std::mutex g_mutex;  // guards everything below that both threads touch

// Outbound, written by the game thread.
bool g_peer_ready = false;
std::vector<std::uint8_t> g_outbound;
codec::DeltaState g_send_delta;
Suit g_local_suit;
bool g_local_suit_known = false;
bool g_local_away_sent = false;
std::uint8_t g_local_lighting = 0;
bool g_local_lighting_known = false;

// Inbound, written by the session thread.
bool g_remote_away = true;
bool g_remote_has_suit = false;
Suit g_remote_suit;
bool g_remote_has_world = false;
std::uint8_t g_remote_lighting = 0;
Pose g_ring[kRing];
std::size_t g_ring_count = 0;
std::size_t g_ring_head = 0;  // index of the newest pose
std::int32_t g_clock_offset = 0;
bool g_clock_known = false;
codec::DeltaState g_receive_delta;

// Shared enemies. Host side: one delta encoder per enemy it sends.
struct SentEnemy {
  std::uint32_t id = 0;  // 0 = free
  codec::DeltaState delta;
};
SentEnemy g_sent_enemies[sheets::settings::max_enemies];
// Joiner side: what the host sent about each enemy.
constexpr std::size_t kEnemyRing = 4;
struct HeardEnemy {
  std::uint32_t id = 0;  // 0 = free
  std::uint64_t asset = 0;
  std::uint8_t joint_count = 0;
  std::uint32_t heard_ms = 0;
  codec::DeltaState delta;
  Pose ring[kEnemyRing];
  std::size_t count = 0;
  std::size_t head = 0;
};
HeardEnemy g_heard_enemies[sheets::settings::max_enemies];

bool g_open = false;  // listening, or joined
bool g_hosting = false;
bool g_crime_requested = false;  // host: the joiner asked for a crime

void QueueFrameLocked(Message type, const std::uint8_t* body, std::size_t size) {
  const std::uint16_t length = static_cast<std::uint16_t>(size + 1);
  const std::size_t at = g_outbound.size();
  g_outbound.resize(at + 3 + size);
  std::memcpy(&g_outbound[at], &length, 2);
  g_outbound[at + 2] = static_cast<std::uint8_t>(type);
  if (size) std::memcpy(&g_outbound[at + 3], body, size);
}

void QueueSuitLocked() {
  std::uint8_t body[1 + sizeof(g_local_suit.ids)];
  body[0] = g_local_suit.count;
  std::memcpy(body + 1, g_local_suit.ids, sizeof(std::uint64_t) * g_local_suit.count);
  QueueFrameLocked(Message::suit, body, 1 + sizeof(std::uint64_t) * g_local_suit.count);
}

void QueueWorldLocked() { QueueFrameLocked(Message::world, &g_local_lighting, 1); }

void ResetPeerLocked() {
  g_peer_ready = false;
  g_outbound.clear();
  g_send_delta.Reset();
  g_receive_delta.Reset();
  g_remote_away = true;
  g_remote_has_suit = false;
  g_remote_has_world = false;
  for (SentEnemy& e : g_sent_enemies) e = SentEnemy{};
  for (HeardEnemy& e : g_heard_enemies) e.id = 0;
  g_ring_count = 0;
  g_clock_known = false;
  g_local_away_sent = false;
}

// ---- session thread ----

struct Peer {
  SOCKET socket = INVALID_SOCKET;
  bool hello_received = false;
  std::vector<std::uint8_t> inbound;
  std::vector<std::uint8_t> sending;
  std::size_t sent = 0;
  std::uint32_t last_heard = 0;
  std::uint32_t last_ping = 0;
};

void Tune(SOCKET socket) {
  u_long non_blocking = 1;
  ioctlsocket(socket, FIONBIO, &non_blocking);
  BOOL no_delay = TRUE;
  setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
}

void ClosePeer(Peer* peer, const char* why) {
  if (peer->socket == INVALID_SOCKET) return;
  closesocket(peer->socket);
  *peer = Peer{};
  std::lock_guard<std::mutex> lock(g_mutex);
  ResetPeerLocked();
  Log("peer gone: %s", why);
}

void SendHello(Peer* peer) {
  std::uint8_t frame[3 + 10];
  const std::uint16_t length = 11;
  const std::uint16_t protocol = static_cast<std::uint16_t>(sheets::settings::protocol_version);
  const std::uint32_t build = sheets::compatibility::pe_timestamp;
  std::memcpy(frame, &length, 2);
  frame[2] = static_cast<std::uint8_t>(Message::hello);
  std::memcpy(frame + 3, &kMagic, 4);
  std::memcpy(frame + 7, &protocol, 2);
  std::memcpy(frame + 9, &build, 4);
  peer->sending.insert(peer->sending.end(), frame, frame + sizeof(frame));
}

// Returns false when the peer must be dropped.
bool HandleFrame(Peer* peer, Message type, const std::uint8_t* body, std::size_t size, const char** why) {
  if (!peer->hello_received) {
    std::uint32_t magic = 0, build = 0;
    std::uint16_t protocol = 0;
    if (type != Message::hello || size != 10) { *why = "no hello"; return false; }
    std::memcpy(&magic, body, 4);
    std::memcpy(&protocol, body + 4, 2);
    std::memcpy(&build, body + 6, 4);
    if (magic != kMagic) { *why = "not Miles Together"; return false; }
    if (protocol != sheets::settings::protocol_version) { *why = "different mod version"; return false; }
    if (build != sheets::compatibility::pe_timestamp) { *why = "different game version"; return false; }
    peer->hello_received = true;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_peer_ready = true;
    if (g_local_suit_known) QueueSuitLocked();
    if (g_local_lighting_known) QueueWorldLocked();
    return true;
  }
  switch (type) {
    case Message::suit: {
      if (size < 1 || body[0] > sheets::settings::max_suit_parts || size != 1 + sizeof(std::uint64_t) * body[0]) { *why = "bad suit"; return false; }
      std::lock_guard<std::mutex> lock(g_mutex);
      g_remote_suit = Suit{};
      g_remote_suit.count = body[0];
      std::memcpy(g_remote_suit.ids, body + 1, sizeof(std::uint64_t) * body[0]);
      g_remote_has_suit = true;
      return true;
    }
    case Message::pose: {
      std::lock_guard<std::mutex> lock(g_mutex);
      const std::size_t slot = g_ring_count == 0 ? 0 : (g_ring_head + 1) % kRing;
      if (!codec::DecodePose(body, size, &g_receive_delta, &g_ring[slot])) { *why = "bad pose"; return false; }
      g_ring_head = slot;
      if (g_ring_count < kRing) ++g_ring_count;
      g_remote_away = false;
      const std::int32_t sample = static_cast<std::int32_t>(NowMs() - g_ring[slot].time_ms);
      // Track the smallest delay seen, drifting up slowly so a clock that runs apart is followed.
      g_clock_offset = (!g_clock_known || sample < g_clock_offset) ? sample : g_clock_offset + 1 < sample ? g_clock_offset + 1 : sample;
      g_clock_known = true;
      return true;
    }
    case Message::away: {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_remote_away = true;
      g_ring_count = 0;
      g_clock_known = false;
      return true;
    }
    case Message::world: {
      if (size != 1 || body[0] >= sheets::settings::lighting_mode_count) { *why = "bad world state"; return false; }
      std::lock_guard<std::mutex> lock(g_mutex);
      g_remote_lighting = body[0];
      g_remote_has_world = true;
      return true;
    }
    case Message::enemy: {
      if (size < 13) { *why = "bad enemy"; return false; }
      std::uint32_t id = 0;
      std::uint64_t asset = 0;
      std::memcpy(&id, body, 4);
      std::memcpy(&asset, body + 4, 8);
      const std::uint8_t joints = body[12];
      if (id == 0 || joints == 0 || joints > kJointCount) { *why = "bad enemy"; return false; }
      std::lock_guard<std::mutex> lock(g_mutex);
      HeardEnemy* slot = nullptr;
      for (HeardEnemy& e : g_heard_enemies) if (e.id == id) slot = &e;
      if (!slot) {
        for (HeardEnemy& e : g_heard_enemies) if (e.id == 0) { slot = &e; break; }
        if (!slot) return true;  // more enemies than shown: ignore this one
        slot->id = id;
        slot->delta.Reset();
        slot->count = 0;
        slot->head = 0;
      }
      if (slot->asset != asset || slot->joint_count != joints) {
        slot->asset = asset;
        slot->joint_count = joints;
        slot->delta.Reset();
        slot->count = 0;
      }
      const std::size_t at = slot->count == 0 ? 0 : (slot->head + 1) % kEnemyRing;
      if (!codec::DecodePose(body + 13, size - 13, &slot->delta, &slot->ring[at], joints)) { *why = "bad enemy pose"; return false; }
      slot->head = at;
      if (slot->count < kEnemyRing) ++slot->count;
      slot->heard_ms = NowMs();
      return true;
    }
    case Message::enemy_gone: {
      if (size != 4) { *why = "bad enemy_gone"; return false; }
      std::uint32_t id = 0;
      std::memcpy(&id, body, 4);
      std::lock_guard<std::mutex> lock(g_mutex);
      for (HeardEnemy& e : g_heard_enemies) if (e.id == id) e.id = 0;
      return true;
    }
    case Message::crime_request: {
      if (size != 0) { *why = "bad crime request"; return false; }
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_hosting) g_crime_requested = true;
      return true;
    }
    case Message::ping:
      return true;
    default:
      *why = "unknown message";
      return false;
  }
}

// Reads, parses, writes. Returns false when the peer was dropped.
bool Service(Peer* peer, bool readable, bool writable) {
  const std::uint32_t now = NowMs();
  if (readable) {
    std::uint8_t buffer[16384];
    for (;;) {
      const int got = recv(peer->socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
      if (got > 0) {
        peer->inbound.insert(peer->inbound.end(), buffer, buffer + got);
        peer->last_heard = now;
        if (got < static_cast<int>(sizeof(buffer))) break;
        continue;
      }
      if (got == 0) { ClosePeer(peer, "closed by the other player"); return false; }
      if (WSAGetLastError() == WSAEWOULDBLOCK) break;
      ClosePeer(peer, "connection lost");
      return false;
    }
    std::size_t at = 0;
    while (peer->inbound.size() - at >= 3) {
      std::uint16_t length = 0;
      std::memcpy(&length, &peer->inbound[at], 2);
      if (length < 1 || length > kMaxFrame) { ClosePeer(peer, "bad frame"); return false; }
      if (peer->inbound.size() - at < 2u + length) break;
      const char* why = "";
      if (!HandleFrame(peer, static_cast<Message>(peer->inbound[at + 2]), &peer->inbound[at + 3], length - 1u, &why)) {
        ClosePeer(peer, why);
        return false;
      }
      at += 2u + length;
    }
    peer->inbound.erase(peer->inbound.begin(), peer->inbound.begin() + static_cast<std::ptrdiff_t>(at));
  }
  if (now - peer->last_heard > sheets::settings::peer_timeout) { ClosePeer(peer, "silent too long"); return false; }
  if (now - peer->last_ping >= 1000) {
    peer->last_ping = now;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_peer_ready) QueueFrameLocked(Message::ping, reinterpret_cast<const std::uint8_t*>(&now), 4);
  }
  if (peer->sent == peer->sending.size()) {
    peer->sending.clear();
    peer->sent = 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    peer->sending.swap(g_outbound);
  }
  if (writable || !peer->sending.empty()) {
    while (peer->sent < peer->sending.size()) {
      const int wrote = send(peer->socket, reinterpret_cast<const char*>(&peer->sending[peer->sent]), static_cast<int>(peer->sending.size() - peer->sent), 0);
      if (wrote > 0) { peer->sent += static_cast<std::size_t>(wrote); continue; }
      if (WSAGetLastError() == WSAEWOULDBLOCK) break;
      ClosePeer(peer, "send failed");
      return false;
    }
  }
  return true;
}

void Adopt(Peer* peer, SOCKET socket) {
  Tune(socket);
  *peer = Peer{};
  peer->socket = socket;
  peer->last_heard = peer->last_ping = NowMs();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ResetPeerLocked();
  }
  SendHello(peer);
}

// Waits up to `wait_ms` for the peer, then services it.
void Pump(Peer* peer, SOCKET listener, int wait_ms) {
  fd_set readable, writable;
  FD_ZERO(&readable);
  FD_ZERO(&writable);
  if (listener != INVALID_SOCKET) FD_SET(listener, &readable);
  if (peer->socket != INVALID_SOCKET) {
    FD_SET(peer->socket, &readable);
    if (peer->sent < peer->sending.size()) FD_SET(peer->socket, &writable);
  }
  if (listener == INVALID_SOCKET && peer->socket == INVALID_SOCKET) {
    Sleep(static_cast<DWORD>(wait_ms));
    return;
  }
  timeval timeout{0, wait_ms * 1000};
  select(0, &readable, &writable, nullptr, &timeout);
  if (listener != INVALID_SOCKET && FD_ISSET(listener, &readable)) {
    const SOCKET incoming = accept(listener, nullptr, nullptr);
    if (incoming != INVALID_SOCKET) {
      if (peer->socket == INVALID_SOCKET) {
        Adopt(peer, incoming);
        Log("a player connected");
      } else {
        closesocket(incoming);  // two players only (sheet design/players)
        Log("turned away a third player");
      }
    }
  }
  if (peer->socket != INVALID_SOCKET) Service(peer, FD_ISSET(peer->socket, &readable), FD_ISSET(peer->socket, &writable));
}

// This PC's address on the network it reaches the internet through, without sending anything.
std::string LocalAddress() {
  std::string address = "127.0.0.1";
  const SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (probe == INVALID_SOCKET) return address;
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(53);
  inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
  if (connect(probe, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0) {
    sockaddr_in local{};
    int size = sizeof(local);
    char text[64];
    if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &size) == 0 && inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text))) address = text;
  }
  closesocket(probe);
  return address;
}

void Host() {
  SOCKET listener = INVALID_SOCKET;
  std::uint16_t port = 0;
  for (std::uint32_t attempt = 0; attempt < sheets::settings::port_attempts && listener == INVALID_SOCKET; ++attempt) {
    const SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (candidate == INVALID_SOCKET) break;
    BOOL exclusive = TRUE;
    setsockopt(candidate, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    port = static_cast<std::uint16_t>(sheets::settings::default_port + attempt);
    any.sin_port = htons(port);
    if (bind(candidate, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) == 0 && listen(candidate, 2) == 0) {
      listener = candidate;
    } else {
      closesocket(candidate);
    }
  }
  if (listener == INVALID_SOCKET) {
    Log("could not open a port to host on; playing alone");
    return;
  }
  Tune(listener);
  const std::string address = LocalAddress() + ":" + std::to_string(port);
  melty::WriteHosting(address);
  Log("hosting at %s", address.c_str());
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_open = true;
    g_hosting = true;
  }
  Peer peer;
  for (;;) Pump(&peer, listener, 4);
}

SOCKET ConnectTo(const std::string& address, std::string* error) {
  std::string host = address;
  std::string port = std::to_string(sheets::settings::default_port);
  const std::size_t colon = address.rfind(':');
  if (colon != std::string::npos) {
    host = address.substr(0, colon);
    port = address.substr(colon + 1);
  }
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0 || !found) {
    *error = "address not found";
    return INVALID_SOCKET;
  }
  SOCKET result = INVALID_SOCKET;
  for (addrinfo* it = found; it && result == INVALID_SOCKET; it = it->ai_next) {
    const SOCKET candidate = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (candidate == INVALID_SOCKET) continue;
    u_long non_blocking = 1;
    ioctlsocket(candidate, FIONBIO, &non_blocking);
    connect(candidate, it->ai_addr, static_cast<int>(it->ai_addrlen));
    fd_set writable, failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(candidate, &writable);
    FD_SET(candidate, &failed);
    timeval timeout{3, 0};
    if (select(0, nullptr, &writable, &failed, &timeout) > 0 && FD_ISSET(candidate, &writable)) {
      result = candidate;
    } else {
      closesocket(candidate);
      *error = "host did not answer";
    }
  }
  freeaddrinfo(found);
  return result;
}

void Join(const std::string& address) {
  const std::uint32_t started = NowMs();
  bool reported = false;
  Peer peer;
  for (;;) {
    std::string error = "host did not answer";
    const SOCKET socket = ConnectTo(address, &error);
    if (socket != INVALID_SOCKET) {
      Adopt(&peer, socket);
      const std::uint32_t connected = NowMs();
      while (peer.socket != INVALID_SOCKET) {
        Pump(&peer, INVALID_SOCKET, 4);
        if (peer.socket != INVALID_SOCKET && peer.hello_received && !reported) {
          reported = true;
          melty::WriteJoined(address);
          Log("joined %s", address.c_str());
          std::lock_guard<std::mutex> lock(g_mutex);
          g_open = true;
        }
        if (peer.socket != INVALID_SOCKET && !peer.hello_received && NowMs() - connected > 10000) ClosePeer(&peer, "host sent no hello");
      }
      error = "host refused or closed the connection";
    }
    if (!reported && NowMs() - started > sheets::settings::join_timeout) {
      reported = true;
      melty::WriteJoinFailed(error);
      Log("join failed: %s", error.c_str());
    }
    Sleep(2000);
  }
}

DWORD WINAPI SessionThread(LPVOID) {
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    Log("network could not start; playing alone");
    return 0;
  }
  const std::string join = melty::JoinAddress();
  if (join.empty()) {
    Host();
  } else {
    Join(join);
  }
  return 0;
}

}  // namespace

void Start() {
  const HANDLE thread = CreateThread(nullptr, 0, SessionThread, nullptr, 0, nullptr);
  if (thread) CloseHandle(thread);
}

void SubmitPose(const Pose& pose) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_peer_ready) return;
  g_local_away_sent = false;
  if (g_outbound.size() > kMaxBacklog) {
    g_send_delta.Reset();  // skipped a pose: the next one must stand alone
    return;
  }
  std::uint8_t body[codec::kMaxPoseBytes];
  const std::size_t size = codec::EncodePose(pose, &g_send_delta, body);
  QueueFrameLocked(Message::pose, body, size);
}

void SubmitAway() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_peer_ready || g_local_away_sent) return;
  g_local_away_sent = true;
  const std::uint32_t now = NowMs();
  QueueFrameLocked(Message::away, reinterpret_cast<const std::uint8_t*>(&now), 4);
}

void SubmitSuit(const Suit& suit) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_local_suit_known && g_local_suit == suit) return;
  g_local_suit = suit;
  g_local_suit_known = true;
  if (g_peer_ready) QueueSuitLocked();
}

void SubmitWorld(std::uint8_t lighting_mode) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_local_lighting_known && g_local_lighting == lighting_mode) return;
  g_local_lighting = lighting_mode;
  g_local_lighting_known = true;
  if (g_peer_ready) QueueWorldLocked();
}

void SubmitEnemy(std::uint32_t id, std::uint64_t asset, std::uint8_t joint_count, const Pose& pose) {
  static std::uint8_t body[13 + codec::kMaxPoseBytes];
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_peer_ready || id == 0) return;
  SentEnemy* slot = nullptr;
  for (SentEnemy& e : g_sent_enemies) if (e.id == id) slot = &e;
  if (!slot) {
    for (SentEnemy& e : g_sent_enemies) if (e.id == 0) { slot = &e; break; }
    if (!slot) return;
    slot->id = id;
    slot->delta.Reset();
  }
  std::memcpy(body, &id, 4);
  std::memcpy(body + 4, &asset, 8);
  body[12] = joint_count;
  const std::size_t size = codec::EncodePose(pose, &slot->delta, body + 13, joint_count);
  if (g_outbound.size() > kMaxBacklog) {  // the link cannot keep up: send this enemy whole next time
    slot->delta.Reset();
    return;
  }
  QueueFrameLocked(Message::enemy, body, 13 + size);
}

void SubmitEnemyGone(std::uint32_t id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (SentEnemy& e : g_sent_enemies) if (e.id == id) e = SentEnemy{};
  if (g_peer_ready) QueueFrameLocked(Message::enemy_gone, reinterpret_cast<const std::uint8_t*>(&id), 4);
}

void SubmitCrimeRequest() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_peer_ready) QueueFrameLocked(Message::crime_request, nullptr, 0);
}

bool TakeCrimeRequest() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const bool requested = g_crime_requested;
  g_crime_requested = false;
  return requested;
}

std::uint32_t SampleEnemies(std::uint32_t now_ms, RemoteEnemy* out, std::uint32_t max) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_peer_ready) return 0;
  std::uint32_t n = 0;
  for (HeardEnemy& e : g_heard_enemies) {
    if (e.id == 0 || e.count == 0 || n >= max) continue;
    if (now_ms - e.heard_ms > sheets::settings::enemy_timeout) continue;
    RemoteEnemy& r = out[n++];
    r.id = e.id;
    r.asset = e.asset;
    r.joint_count = e.joint_count;
    const auto at = [&e](std::size_t back) -> const Pose& { return e.ring[(e.head + kEnemyRing - back) % kEnemyRing]; };
    if (!g_clock_known) { r.pose = at(0); continue; }
    const std::uint32_t target = now_ms - static_cast<std::uint32_t>(g_clock_offset) - sheets::settings::enemy_interpolation_delay;
    if (static_cast<std::int32_t>(target - at(0).time_ms) >= 0) { r.pose = at(0); continue; }
    bool done = false;
    for (std::size_t back = 1; back < e.count && !done; ++back) {
      const Pose& older = at(back);
      const Pose& newer = at(back - 1);
      if (static_cast<std::int32_t>(target - older.time_ms) >= 0) {
        const std::uint32_t span = newer.time_ms - older.time_ms;
        const float t = span ? static_cast<float>(target - older.time_ms) / static_cast<float>(span) : 1.0f;
        codec::Interpolate(older, newer, t, &r.pose, e.joint_count);
        done = true;
      }
    }
    if (!done) r.pose = at(e.count - 1);
  }
  return n;
}

void SampleRemote(std::uint32_t now_ms, RemoteView* out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  out->connected = g_peer_ready;
  out->away = g_remote_away;
  out->has_suit = g_remote_has_suit;
  out->suit = g_remote_suit;
  out->has_world = g_peer_ready && g_remote_has_world;
  out->lighting_mode = g_remote_lighting;
  out->has_pose = false;
  if (!g_peer_ready || g_remote_away || g_ring_count == 0 || !g_clock_known) return;
  // The moment to show, on the sender's clock.
  const std::uint32_t target = now_ms - static_cast<std::uint32_t>(g_clock_offset) - sheets::settings::interpolation_delay;
  const auto at = [](std::size_t back) -> const Pose& { return g_ring[(g_ring_head + kRing - back) % kRing]; };
  out->has_pose = true;
  if (static_cast<std::int32_t>(target - at(0).time_ms) >= 0) {
    out->pose = at(0);  // newer than anything received: hold the newest
    return;
  }
  for (std::size_t back = 1; back < g_ring_count; ++back) {
    const Pose& older = at(back);
    const Pose& newer = at(back - 1);
    if (static_cast<std::int32_t>(target - older.time_ms) >= 0) {
      const std::uint32_t span = newer.time_ms - older.time_ms;
      const float t = span ? static_cast<float>(target - older.time_ms) / static_cast<float>(span) : 1.0f;
      codec::Interpolate(older, newer, t, &out->pose);
      return;
    }
  }
  out->pose = at(g_ring_count - 1);  // older than everything kept: show the oldest
}

bool OpenToOthers() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_open;
}

bool Hosting() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_hosting;
}

}  // namespace mt::net
