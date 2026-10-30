#include "matchmaker/match_queue.hpp"

#include <algorithm>
#include <cstdlib>

namespace mm {

void MatchQueue::add(const Ticket& t) {
  auto f = fifo_.insert(fifo_.end(), t);
  auto s = skill_.emplace(t.skill, f);
  index_[t.id] = {f, s};
}

bool MatchQueue::remove(uint64_t id) {
  auto it = index_.find(id);
  if (it == index_.end()) return false;
  skill_.erase(it->second.skill);
  fifo_.erase(it->second.fifo);
  index_.erase(it);
  return true;
}

int MatchQueue::window_for(const Ticket& t, Clock::time_point now) const {
  double waited = std::chrono::duration<double>(now - t.enqueued).count();
  int w = cfg_.base_window + static_cast<int>(waited * cfg_.widen_per_sec);
  return std::min(w, cfg_.max_window);
}

std::vector<std::vector<Ticket>> MatchQueue::tick(Clock::time_point now) {
  std::vector<std::vector<Ticket>> matches;
  const size_t need = cfg_.match_size - 1;

  // Snapshot anchor ids oldest-first; anchors consumed by earlier matches are skipped.
  std::vector<uint64_t> anchors;
  anchors.reserve(fifo_.size());
  for (const auto& t : fifo_) anchors.push_back(t.id);

  for (uint64_t aid : anchors) {
    auto ait = index_.find(aid);
    if (ait == index_.end()) continue;
    const Ticket anchor = *ait->second.fifo;
    const int w = window_for(anchor, now);

    // Range query: all tickets with skill in [s-w, s+w]
    std::vector<FifoIt> cands;
    for (auto it = skill_.lower_bound(anchor.skill - w);
         it != skill_.end() && it->first <= anchor.skill + w; ++it)
      if (it->second->id != aid) cands.push_back(it->second);
    if (cands.size() < need) continue;

    // Keep the `need` closest in skill: O(k) partial selection
    auto gap = [&](FifoIt a, FifoIt b) {
      return std::abs(a->skill - anchor.skill) < std::abs(b->skill - anchor.skill);
    };
    std::nth_element(cands.begin(), cands.begin() + (need - 1), cands.end(), gap);
    cands.resize(need);

    std::vector<Ticket> match{anchor};
    for (auto c : cands) match.push_back(*c);
    for (const auto& t : match) remove(t.id);
    matches.push_back(std::move(match));
  }
  return matches;
}

}  // namespace mm
