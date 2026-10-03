// ENet host for one peer: dial all candidates at once, accept what arrives, keep one path; plus the LAN beacon.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "peer_connector.h"
#include "session_security.h"
#include <enet/enet.h>
#include <cstdio>
#include <mutex>

namespace mu_net {
namespace {

bool enet_ready() {
  // ENet's start is process-wide (it starts Winsock). Other parts of the game may start it too;
  // each start is counted by Winsock, and this one is never undone while the process lives.
  static std::once_flag once;
  static bool ok = false;
  std::call_once(once, [] { ok = enet_initialize() == 0; });
  return ok;
}

// Dotted IPv4 only, parsed here so no resolver is ever entered: this runs on the caller's thread,
// which may be the simulation's.
bool parse_ipv4(const std::string& text, enet_uint32& host) {
  unsigned parts[4] = {};
  int part = 0, digits = 0;
  for (char c : text) {
    if (c >= '0' && c <= '9') {
      parts[part] = parts[part] * 10 + (unsigned)(c - '0');
      if (++digits > 3 || parts[part] > 255) return false;
    } else if (c == '.') {
      if (digits == 0 || ++part > 3) return false;
      digits = 0;
    } else {
      return false;
    }
  }
  if (part != 3 || digits == 0) return false;
  const uint8_t bytes[4] = {(uint8_t)parts[0], (uint8_t)parts[1], (uint8_t)parts[2], (uint8_t)parts[3]};
  std::memcpy(&host, bytes, 4);   // ENet keeps the address in network byte order
  return true;
}

std::string address_text(const ENetAddress& address) {
  uint8_t bytes[4];
  std::memcpy(bytes, &address.host, 4);
  char text[32];
  std::snprintf(text, sizeof text, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
  return text;
}

const char kBeaconDomain[] = "MeleeUnlockedNet1 beacon";
const uint8_t kBeaconMagic[4] = {'M', 'U', 'N', 'B'};

}  // namespace

struct PeerConnector::Impl {
  struct Path { ENetPeer* peer = nullptr; bool up = false; bool outgoing = false; };
  struct Packet { int path; uint8_t channel; std::vector<uint8_t> bytes; };
  ENetHost* host = nullptr;
  Path paths[kMaxPaths];
  ENetAddress candidates[kMaxCandidates];
  int candidate_count = 0;
  int pinned = -1;    // the path the session chose
  int current = -1;   // the path of the packet being delivered
  uint64_t deadline_us = 0, next_punch_us = 0;
  std::vector<Packet> inbox;

  int find(const ENetPeer* peer) const {
    for (int i = 0; i < kMaxPaths; ++i) if (paths[i].peer == peer) return i;
    return -1;
  }
  int free_slot() const {
    for (int i = 0; i < kMaxPaths; ++i) if (!paths[i].peer) return i;
    return -1;
  }
  bool any_up() const {
    for (const auto& p : paths) if (p.up) return true;
    return false;
  }
};

PeerConnector::PeerConnector() : impl_(new Impl) {}
PeerConnector::~PeerConnector() { close(); delete impl_; }

bool PeerConnector::open(uint16_t local_port, std::string* error) {
  close();
  auto fail = [&](ConnectFailure f) { state_ = ConnectState::Failed; failure_ = f; if (error) *error = failure_text(); return false; };
  if (!enet_ready()) return fail(ConnectFailure::NetworkStart);
  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = local_port;
  impl_->host = enet_host_create(&address, kMaxPaths, kChannelCount, 0, 0);
  if (!impl_->host) return fail(ConnectFailure::Bind);
  ENetAddress bound;
  local_port_ = enet_socket_get_address(impl_->host->socket, &bound) == 0 ? bound.port : local_port;
  state_ = ConnectState::Idle;
  failure_ = ConnectFailure::None;
  return true;
}

bool PeerConnector::connect(const std::vector<Endpoint>& candidates, uint64_t now_us, std::string* error) {
  auto fail = [&](ConnectFailure f) { state_ = ConnectState::Failed; failure_ = f; if (error) *error = failure_text(); return false; };
  if (!impl_->host) return fail(ConnectFailure::Bind);
  if ((int)candidates.size() > kMaxCandidates) return fail(ConnectFailure::TooMany);
  impl_->candidate_count = 0;
  for (const auto& c : candidates) {
    ENetAddress address;
    if (c.port == 0 || !parse_ipv4(c.address, address.host)) return fail(ConnectFailure::BadAddress);
    address.port = c.port;
    impl_->candidates[impl_->candidate_count++] = address;
  }
  for (int i = 0; i < impl_->candidate_count; ++i) {
    const int slot = impl_->free_slot();
    if (slot < 0) break;
    ENetPeer* peer = enet_host_connect(impl_->host, &impl_->candidates[i], kChannelCount, 0);
    if (!peer) continue;
    impl_->paths[slot].peer = peer;
    impl_->paths[slot].outgoing = true;
    impl_->paths[slot].up = false;
  }
  impl_->deadline_us = now_us + limit_us_;
  impl_->next_punch_us = now_us;
  state_ = ConnectState::Connecting;
  failure_ = ConnectFailure::None;
  enet_host_flush(impl_->host);
  return true;
}

const char* PeerConnector::failure_text() const {
  switch (failure_) {
    case ConnectFailure::None: return "";
    case ConnectFailure::NetworkStart: return "Windows networking could not be started";
    case ConnectFailure::Bind: return "The game's network port could not be opened on this PC (another program may be using it)";
    case ConnectFailure::BadAddress: return "The other player's address is not a usable IP address and port";
    case ConnectFailure::TooMany: return "Too many addresses were given for the other player";
    case ConnectFailure::TimedOut:
      return "The other player could not be reached. A router or firewall on one side blocks direct connections; "
             "forwarding the game's UDP port on either side usually fixes it";
    case ConnectFailure::PeerClosed: return "The connection to the other player was lost";
  }
  return "The connection failed";
}

std::string PeerConnector::peer_address() const {
  if (impl_->pinned < 0 || !impl_->paths[impl_->pinned].peer) return {};
  const ENetAddress& a = impl_->paths[impl_->pinned].peer->address;
  return address_text(a) + ":" + std::to_string(a.port);
}

bool PeerConnector::connected() const {
  if (state_ != ConnectState::Connected) return false;
  return impl_->pinned >= 0 ? impl_->paths[impl_->pinned].up : impl_->any_up();
}

bool PeerConnector::failed(std::string* why) const {
  if (state_ != ConnectState::Failed) return false;
  if (why) *why = failure_text();
  return true;
}

bool PeerConnector::send(uint8_t channel, const uint8_t* data, size_t size, bool reliable) {
  if (!impl_->host || !data || size == 0 || size > kMaxPacket || channel >= kChannelCount) return false;
  // Unsequenced, not merely unreliable: ENet would discard a late unreliable packet, and a late
  // COMMIT still carries frames. Order and repeats are the session's business (its replay window).
  const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
  bool any = false;
  for (int i = 0; i < kMaxPaths; ++i) {
    auto& path = impl_->paths[i];
    if (!path.up || (impl_->pinned >= 0 && i != impl_->pinned)) continue;
    ENetPacket* packet = enet_packet_create(data, size, flags);
    if (!packet) continue;
    if (enet_peer_send(path.peer, channel, packet) < 0) enet_packet_destroy(packet);
    else any = true;
  }
  return any;
}

void PeerConnector::poll(uint64_t now_us, const Receiver& on_packet) {
  if (!impl_->host) return;
  ENetEvent event;
  // A bounded number of events a call: a flood cannot hold the caller, the rest waits in the
  // socket buffer (or is lost there, which the protocol is built to survive).
  for (int budget = 256; budget > 0 && enet_host_service(impl_->host, &event, 0) > 0; --budget) {
    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        int slot = impl_->find(event.peer);
        if (slot < 0) {
          // Someone connected to us: the peer's own dial, when things are well.
          slot = impl_->pinned >= 0 ? -1 : impl_->free_slot();
          if (slot < 0) { enet_peer_disconnect_now(event.peer, 0); break; }
          impl_->paths[slot].peer = event.peer;
          impl_->paths[slot].outgoing = false;
        } else if (impl_->pinned >= 0 && slot != impl_->pinned) {
          enet_peer_disconnect_now(event.peer, 0);
          impl_->paths[slot] = Impl::Path();
          break;
        }
        impl_->paths[slot].up = true;
        ++generation_;
        // Dead-path detection in 4 to 8 s, a ping every 250 ms, and no throttling of unreliable
        // sends: a dropped COMMIT costs a rollback, which is worse than the bandwidth it saves.
        enet_peer_timeout(event.peer, 32, 4000, 8000);
        enet_peer_ping_interval(event.peer, 250);
        enet_peer_throttle_configure(event.peer, ENET_PEER_PACKET_THROTTLE_INTERVAL, ENET_PEER_PACKET_THROTTLE_SCALE, 0);
        if (state_ == ConnectState::Connecting) state_ = ConnectState::Connected;
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE: {
        const int slot = impl_->find(event.peer);
        const bool wanted = slot >= 0 && impl_->paths[slot].up && (impl_->pinned < 0 || slot == impl_->pinned);
        // Size and channel are refused here, before the session spends anything on the packet.
        if (!wanted || event.packet->dataLength == 0 || event.packet->dataLength > kMaxPacket ||
            event.channelID >= kChannelCount || impl_->inbox.size() >= 512) {
          ++refused_;
        } else {
          Impl::Packet p;
          p.path = slot;
          p.channel = event.channelID;
          p.bytes.assign(event.packet->data, event.packet->data + event.packet->dataLength);
          impl_->inbox.push_back(std::move(p));
        }
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT: {
        const int slot = impl_->find(event.peer);
        if (slot < 0) break;
        impl_->paths[slot] = Impl::Path();
        if (slot == impl_->pinned) { state_ = ConnectState::Failed; failure_ = ConnectFailure::PeerClosed; }
        break;
      }
      default:
        break;
    }
  }

  if (state_ == ConnectState::Connected && impl_->pinned < 0 && !impl_->any_up())
    state_ = ConnectState::Connecting;   // every early path fell away before one was chosen
  if (state_ == ConnectState::Connecting) {
    if (now_us >= impl_->deadline_us) {
      state_ = ConnectState::Failed;
      failure_ = ConnectFailure::TimedOut;
    } else if (now_us >= impl_->next_punch_us) {
      // ENet repeats its connect at 0.5, 1.5 and 3.5 s. In between, a one-byte datagram every
      // 100 ms keeps this side's NAT mapping toward each candidate open, so the peer's connect
      // gets in whenever it comes. The receiving ENet host drops a one-byte datagram unread.
      uint8_t zero = 0;
      ENetBuffer buffer;
      buffer.data = &zero;
      buffer.dataLength = 1;
      for (int i = 0; i < impl_->candidate_count; ++i) enet_socket_send(impl_->host->socket, &impl_->candidates[i], &buffer, 1);
      impl_->next_punch_us = now_us + 100000;
    }
  }

  std::vector<Impl::Packet> batch;
  batch.swap(impl_->inbox);   // the receiver may send, pin or close while it is handed packets
  for (auto& p : batch) {
    if (!impl_->host) break;
    if (impl_->pinned >= 0 && p.path != impl_->pinned) { ++refused_; continue; }
    impl_->current = p.path;
    if (on_packet) on_packet(p.channel, p.bytes.data(), p.bytes.size());
  }
  impl_->current = -1;
}

void PeerConnector::pin_path() {
  if (impl_->current < 0 || impl_->pinned >= 0 || !impl_->host) return;
  impl_->pinned = impl_->current;
  for (int i = 0; i < kMaxPaths; ++i) {
    auto& path = impl_->paths[i];
    if (i == impl_->pinned || !path.peer) continue;
    if (path.up) {
      enet_peer_disconnect(path.peer, 0);   // told, so the other side frees its end; our slot clears on the event
      path.up = false;
    } else {
      enet_peer_reset(path.peer);           // a dial that never completed: nothing to tell
      path = Impl::Path();
    }
  }
}

void PeerConnector::flush() {
  if (impl_->host) enet_host_flush(impl_->host);
}

void PeerConnector::close() {
  if (impl_->host) {
    for (auto& path : impl_->paths) {
      if (!path.peer) continue;
      if (path.up) enet_peer_disconnect_now(path.peer, 0);
      else enet_peer_reset(path.peer);
      path = Impl::Path();
    }
    enet_host_flush(impl_->host);
    enet_host_destroy(impl_->host);
    impl_->host = nullptr;
  }
  impl_->inbox.clear();
  impl_->pinned = impl_->current = -1;
  impl_->candidate_count = 0;
  state_ = ConnectState::Idle;
  failure_ = ConnectFailure::None;
  local_port_ = 0;
}

// ---------------------------------------------------------------- LAN beacon
LanBeacon::LanBeacon() : socket_((intptr_t)ENET_SOCKET_NULL) {}
LanBeacon::~LanBeacon() { close(); }

bool LanBeacon::open(std::string* error) {
  close();
  auto fail = [&](const char* text) { if (error) *error = text; close(); return false; };
  if (!enet_ready()) return fail("Windows networking could not be started");
  const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (s == ENET_SOCKET_NULL) return fail("The LAN discovery socket could not be created");
  socket_ = (intptr_t)s;
  // Reuse first: two games on one PC (and the tests) share the fixed port.
  enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1);
  enet_socket_set_option(s, ENET_SOCKOPT_BROADCAST, 1);
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = kPort;
  if (enet_socket_bind(s, &address) < 0) return fail("The LAN discovery port is in use on this PC");
  return true;
}

void LanBeacon::close() {
  if ((ENetSocket)socket_ != ENET_SOCKET_NULL) enet_socket_destroy((ENetSocket)socket_);
  socket_ = (intptr_t)ENET_SOCKET_NULL;
}

bool LanBeacon::announce(const PeerIdentity& identity, uint16_t game_port, const std::string& name, const std::string& build_id) {
  if ((ENetSocket)socket_ == ENET_SOCKET_NULL || !identity.valid) return false;
  std::vector<uint8_t> beacon(kBeaconDomain, kBeaconDomain + sizeof kBeaconDomain - 1);
  const size_t body = beacon.size();   // the signed message is the domain and then the beacon; only the beacon travels
  Writer w(beacon);
  w.bytes(kBeaconMagic, 4);
  w.u16(kProtocolVersion);
  w.u16(game_port);
  w.bytes(identity.public_key.data(), identity.public_key.size());
  w.str8(name, kMaxNameBytes);
  w.str8(build_id, kMaxBuildIdBytes);
  Bytes64 signature{};
  sign(identity, beacon.data(), beacon.size(), signature);
  w.bytes(signature.data(), signature.size());
  ENetAddress to;
  to.host = ENET_HOST_BROADCAST;
  to.port = kPort;
  ENetBuffer buffer;
  buffer.data = beacon.data() + body;
  buffer.dataLength = beacon.size() - body;
  return enet_socket_send((ENetSocket)socket_, &to, &buffer, 1) > 0;
}

void LanBeacon::poll(uint64_t now_us, const Bytes32* own_identity, std::vector<LanPeer>& peers) {
  if ((ENetSocket)socket_ != ENET_SOCKET_NULL) {
    const size_t domain = sizeof kBeaconDomain - 1;
    uint8_t message[sizeof kBeaconDomain - 1 + 256];
    std::memcpy(message, kBeaconDomain, domain);
    for (int budget = 64; budget > 0; --budget) {
      ENetAddress from;
      ENetBuffer buffer;
      buffer.data = message + domain;
      buffer.dataLength = 256;
      const int got = enet_socket_receive((ENetSocket)socket_, &from, &buffer, 1);
      if (got <= 0) break;
      if (got < 4 + 2 + 2 + 32 + 1 + 1 + 64 || got > 256) continue;
      const uint8_t* data = message + domain;
      Reader r(data, (size_t)got - 64);
      uint8_t magic[4];
      r.bytes(magic, 4);
      if (!r.ok || std::memcmp(magic, kBeaconMagic, 4) != 0 || r.u16() != kProtocolVersion) continue;
      LanPeer peer;
      peer.port = r.u16();
      r.bytes(peer.identity.data(), peer.identity.size());
      r.str8(peer.name, kMaxNameBytes);
      r.str8(peer.build_id, kMaxBuildIdBytes);
      if (!r.done() || peer.port == 0) continue;
      if (own_identity && peer.identity == *own_identity) continue;
      Bytes64 signature{};
      std::memcpy(signature.data(), data + got - 64, 64);
      if (!verify(peer.identity, message, domain + (size_t)got - 64, signature)) continue;
      peer.address = address_text(from);
      peer.seen_us = now_us;
      bool known = false;
      for (auto& p : peers) if (p.identity == peer.identity) { p = peer; known = true; break; }
      if (!known && peers.size() < kMaxPeers) peers.push_back(peer);
    }
  }
  for (size_t i = 0; i < peers.size();) {
    if (now_us - peers[i].seen_us > 5000000) peers.erase(peers.begin() + i);
    else ++i;
  }
}

}  // namespace mu_net
