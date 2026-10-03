// Brings up the UDP path to one peer (ENet, simultaneous open toward every candidate endpoint) and finds peers on the LAN.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "input_transport.h"
#include "peer_identity.h"
#include <string>
#include <vector>

namespace mu_net {

struct Endpoint {
  std::string address;   // dotted IPv4. Names are not resolved here: a lookup can block for seconds.
  uint16_t port = 0;
};

enum class ConnectState : uint8_t { Idle, Connecting, Connected, Failed };
enum class ConnectFailure : uint8_t {
  None,
  NetworkStart,   // the network library would not start
  Bind,           // the local UDP port could not be opened (in use, or blocked)
  BadAddress,     // a candidate is not a dotted IPv4 address with a port
  TooMany,        // more candidates than kMaxCandidates
  TimedOut,       // no candidate answered inside the limit
  PeerClosed,     // the chosen path was closed by the other side or timed out
};

constexpr int kMaxCandidates = 6;
constexpr int kMaxPaths = 8;                        // candidates we dial plus connections that arrive
constexpr uint64_t kConnectLimitUs = 8 * 1000000;   // from connect() to the first path

class PeerConnector : public Link {
 public:
  PeerConnector();
  ~PeerConnector() override;
  PeerConnector(const PeerConnector&) = delete;
  PeerConnector& operator=(const PeerConnector&) = delete;

  // Opens the UDP socket. Port 0 lets the system choose; local_port() tells which it chose.
  bool open(uint16_t local_port, std::string* error);
  uint16_t local_port() const { return local_port_; }
  // Starts dialing every candidate at once and accepting connections that arrive on the socket.
  // Both peers call this toward each other at the same time: each side's outgoing packets open
  // its own NAT mapping for the other's (hole punching). An empty list only listens, which is
  // enough for a host whose port is forwarded. Returns at once; poll() makes the progress.
  bool connect(const std::vector<Endpoint>& candidates, uint64_t now_us, std::string* error);
  void set_limit_us(uint64_t limit) { limit_us_ = limit; }

  ConnectState state() const { return state_; }
  ConnectFailure failure() const { return failure_; }
  // One sentence for the player, naming what to try when the cause is a blocked path.
  const char* failure_text() const;
  std::string peer_address() const;   // of the chosen path, for the log; empty before one is chosen
  uint64_t packets_refused() const { return refused_; }

  // Link
  bool send(uint8_t channel, const uint8_t* data, size_t size, bool reliable) override;
  void poll(uint64_t now_us, const Receiver& on_packet) override;
  bool connected() const override;
  bool failed(std::string* why) const override;
  uint32_t generation() const override { return generation_; }
  void pin_path() override;
  void flush() override;
  void close() override;

 private:
  struct Impl;
  Impl* impl_;
  uint16_t local_port_ = 0;
  ConnectState state_ = ConnectState::Idle;
  ConnectFailure failure_ = ConnectFailure::None;
  uint64_t limit_us_ = kConnectLimitUs;
  uint32_t generation_ = 0;
  uint64_t refused_ = 0;
};

// LAN discovery: a small signed beacon broadcast on a fixed UDP port. It says "this identity is
// waiting on this port"; the address is the datagram's source. A beacon proves who signed it, not
// who relayed it, so the session handshake still checks the identity before anything is trusted.
struct LanPeer {
  std::string address;
  uint16_t port = 0;       // the game's UDP port, for PeerConnector
  Bytes32 identity{};
  std::string name;
  std::string build_id;
  uint64_t seen_us = 0;
};

class LanBeacon {
 public:
  static constexpr uint16_t kPort = 47633;
  static constexpr size_t kMaxPeers = 32;
  LanBeacon();
  ~LanBeacon();
  LanBeacon(const LanBeacon&) = delete;
  LanBeacon& operator=(const LanBeacon&) = delete;
  bool open(std::string* error);
  // Broadcasts one beacon. Call about once a second while waiting for a LAN opponent.
  bool announce(const PeerIdentity& identity, uint16_t game_port, const std::string& name, const std::string& build_id);
  // Reads what arrived, without waiting. `peers` is updated in place: one entry per identity,
  // entries not heard for 5 s removed, never more than kMaxPeers. Our own beacons are skipped.
  void poll(uint64_t now_us, const Bytes32* own_identity, std::vector<LanPeer>& peers);
  void close();

 private:
  intptr_t socket_;
};

}  // namespace mu_net
