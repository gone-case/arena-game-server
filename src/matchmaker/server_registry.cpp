#include "matchmaker/server_registry.hpp"

namespace mm {

std::vector<Assignment> ServerRegistry::heartbeat(const std::string& id, const std::string& host,
                                                  uint16_t port, uint32_t capacity, uint32_t active,
                                                  Clock::time_point now) {
  auto& s = servers_[id];
  s.id = id;
  s.host = host;
  s.port = port;
  s.capacity = capacity;
  s.last_seen = now;
  auto out = std::move(s.pending);
  s.pending.clear();
  s.active = active + static_cast<uint32_t>(out.size());  // delivered now, counted until next report
  return out;
}

std::optional<const ServerInfo*> ServerRegistry::place(const Assignment& a, Clock::time_point now) {
  ServerInfo* best = nullptr;
  for (auto& [_, s] : servers_) {
    if (!is_alive(s, now) || s.load() >= s.capacity) continue;
    if (!best || s.load() < best->load()) best = &s;
  }
  if (!best) return std::nullopt;
  best->pending.push_back(a);
  return best;
}

std::vector<Assignment> ServerRegistry::evict_stale(Clock::time_point now) {
  std::vector<Assignment> orphaned;
  for (auto it = servers_.begin(); it != servers_.end();) {
    if (is_alive(it->second, now)) {
      ++it;
      continue;
    }
    for (auto& a : it->second.pending) orphaned.push_back(std::move(a));
    it = servers_.erase(it);
  }
  return orphaned;
}

size_t ServerRegistry::alive(Clock::time_point now) const {
  size_t n = 0;
  for (const auto& [_, s] : servers_) n += is_alive(s, now);
  return n;
}

}  // namespace mm
