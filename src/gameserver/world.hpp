// Authoritative game state for all matches on one server. Pure logic, no sockets.
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "common/protocol.hpp"

namespace gs {

using Clock = std::chrono::steady_clock;

struct Player {
  float x = 0, y = 0;
  uint32_t last_seq = 0;
  int inputs_this_tick = 0;
  bool joined = false;
};

struct Match {
  uint32_t id;
  std::unordered_map<uint32_t, Player> players;  // only assigned players may join
  Clock::time_point last_activity;
};

class World {
 public:
  static constexpr int kMaxInputsPerTick = 2;  // simple speed-hack guard

  void add_match(uint32_t match_id, const std::vector<uint32_t>& players, Clock::time_point now);
  bool join(uint32_t match_id, uint32_t player_id, Clock::time_point now);
  // Returns false if rejected (unknown player, stale/duplicate seq, rate limit).
  bool apply_input(uint32_t player_id, uint32_t seq, int8_t dx, int8_t dy, Clock::time_point now);

  void end_tick();  // resets per-tick counters
  std::vector<uint32_t> reap_idle(Clock::time_point now, std::chrono::seconds idle);

  proto::Snapshot snapshot_for(uint32_t match_id, uint32_t player_id, uint32_t tick) const;
  const Match* match(uint32_t id) const;
  const Player* player(uint32_t player_id) const;
  std::optional<uint32_t> match_of(uint32_t player_id) const {
    auto it = player_match_.find(player_id);
    return it == player_match_.end() ? std::nullopt : std::optional(it->second);
  }
  size_t match_count() const { return matches_.size(); }

 private:
  std::unordered_map<uint32_t, Match> matches_;
  std::unordered_map<uint32_t, uint32_t> player_match_;  // player -> match
};

}  // namespace gs
