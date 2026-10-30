#include "gameserver/world.hpp"

namespace gs {

void World::add_match(uint32_t match_id, const std::vector<uint32_t>& players, Clock::time_point now) {
  Match m{match_id, {}, now};
  // Spread spawn points around the arena centre
  for (size_t i = 0; i < players.size(); ++i) {
    Player p;
    p.x = proto::kArena / 2 + (i % 2 ? 10.0f : -10.0f);
    p.y = proto::kArena / 2 + static_cast<float>(i / 2) * 10.0f;
    m.players[players[i]] = p;
    player_match_[players[i]] = match_id;
  }
  matches_[match_id] = std::move(m);
}

bool World::join(uint32_t match_id, uint32_t player_id, Clock::time_point now) {
  auto m = matches_.find(match_id);
  if (m == matches_.end()) return false;
  auto p = m->second.players.find(player_id);
  if (p == m->second.players.end()) return false;
  p->second.joined = true;
  m->second.last_activity = now;
  return true;
}

bool World::apply_input(uint32_t player_id, uint32_t seq, int8_t dx, int8_t dy, Clock::time_point now) {
  auto pm = player_match_.find(player_id);
  if (pm == player_match_.end()) return false;
  auto& m = matches_.at(pm->second);
  auto& p = m.players.at(player_id);
  if (!p.joined || seq <= p.last_seq || p.inputs_this_tick >= kMaxInputsPerTick) return false;
  proto::step(p.x, p.y, dx, dy);
  p.last_seq = seq;
  ++p.inputs_this_tick;
  m.last_activity = now;
  return true;
}

void World::end_tick() {
  for (auto& [_, m] : matches_)
    for (auto& [__, p] : m.players) p.inputs_this_tick = 0;
}

std::vector<uint32_t> World::reap_idle(Clock::time_point now, std::chrono::seconds idle) {
  std::vector<uint32_t> ended;
  for (auto it = matches_.begin(); it != matches_.end();) {
    if (now - it->second.last_activity < idle) {
      ++it;
      continue;
    }
    for (const auto& [pid, _] : it->second.players) player_match_.erase(pid);
    ended.push_back(it->first);
    it = matches_.erase(it);
  }
  return ended;
}

proto::Snapshot World::snapshot_for(uint32_t match_id, uint32_t player_id, uint32_t tick) const {
  proto::Snapshot s{tick, 0, {}};
  const auto& m = matches_.at(match_id);
  for (const auto& [id, p] : m.players) {
    if (!p.joined) continue;
    s.players.push_back({id, p.x, p.y});
    if (id == player_id) s.ack_seq = p.last_seq;
  }
  return s;
}

const Match* World::match(uint32_t id) const {
  auto it = matches_.find(id);
  return it == matches_.end() ? nullptr : &it->second;
}

const Player* World::player(uint32_t player_id) const {
  auto pm = player_match_.find(player_id);
  return pm == player_match_.end() ? nullptr : &matches_.at(pm->second).players.at(player_id);
}

}  // namespace gs
