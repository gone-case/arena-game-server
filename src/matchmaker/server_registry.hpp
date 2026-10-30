// Registry of game server instances (the "distributed" part).
// Servers heartbeat in; the matchmaker places each new match on the least-loaded live server
// and hands the assignment back on that server's next heartbeat (pull model: no inbound ports
// needed on game servers, and a dead server simply stops pulling).
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

using Clock = std::chrono::steady_clock;

struct Assignment {
  uint32_t match_id;
  std::vector<uint32_t> players;
};

struct ServerInfo {
  std::string id, host;
  uint16_t port = 0;
  uint32_t capacity = 0;  // max concurrent matches
  uint32_t active = 0;    // matches the server reports running
  Clock::time_point last_seen{};
  std::vector<Assignment> pending;  // assigned but not yet pulled

  uint32_t load() const { return active + static_cast<uint32_t>(pending.size()); }
};

class ServerRegistry {
 public:
  explicit ServerRegistry(std::chrono::milliseconds ttl = std::chrono::seconds(5)) : ttl_(ttl) {}

  // Updates/creates the server and returns (drains) its pending assignments.
  std::vector<Assignment> heartbeat(const std::string& id, const std::string& host, uint16_t port,
                                    uint32_t capacity, uint32_t active, Clock::time_point now);

  // Least-loaded live server with free capacity; reserves the slot.
  std::optional<const ServerInfo*> place(const Assignment& a, Clock::time_point now);

  // Drops servers that missed heartbeats; returns matches that were never delivered.
  std::vector<Assignment> evict_stale(Clock::time_point now);

  size_t alive(Clock::time_point now) const;

 private:
  bool is_alive(const ServerInfo& s, Clock::time_point now) const { return now - s.last_seen <= ttl_; }

  std::chrono::milliseconds ttl_;
  std::unordered_map<std::string, ServerInfo> servers_;
};

}  // namespace mm
