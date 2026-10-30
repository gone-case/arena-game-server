// Skill-based matchmaking queue.
//
// Data structures:
//   fifo_   : std::list in arrival order       -> oldest player is always tried first
//   skill_  : std::multimap skill -> ticket    -> O(log n) range query for "players within +-w"
//   index_  : unordered_map id -> both iterators -> O(log n) cancel instead of O(n) scan
//
// The search window widens with wait time, so low-population skill ranges still get matches.
#pragma once
#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace mm {

using Clock = std::chrono::steady_clock;

struct Ticket {
  uint64_t id;
  uint32_t player;
  int skill;
  Clock::time_point enqueued;
};

struct QueueConfig {
  size_t match_size = 2;
  int base_window = 50;      // initial allowed skill gap
  int widen_per_sec = 50;    // gap grows by this much per second waited
  int max_window = 1000;
};

class MatchQueue {
 public:
  explicit MatchQueue(QueueConfig cfg = {}) : cfg_(cfg) {}

  void add(const Ticket& t);
  bool remove(uint64_t ticket_id);
  bool contains(uint64_t ticket_id) const { return index_.count(ticket_id) > 0; }
  size_t size() const { return fifo_.size(); }

  int window_for(const Ticket& t, Clock::time_point now) const;

  // Forms as many matches as possible; matched tickets are removed from the queue.
  std::vector<std::vector<Ticket>> tick(Clock::time_point now);

 private:
  using FifoIt = std::list<Ticket>::iterator;
  struct Entry {
    FifoIt fifo;
    std::multimap<int, FifoIt>::iterator skill;
  };

  QueueConfig cfg_;
  std::list<Ticket> fifo_;
  std::multimap<int, FifoIt> skill_;
  std::unordered_map<uint64_t, Entry> index_;
};

}  // namespace mm
