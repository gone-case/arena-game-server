// Matchmaker service: REST API for players and game servers + background matching loop.
#include <httplib.h>

#include <atomic>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

#include "common/env.hpp"
#include "matchmaker/match_queue.hpp"
#include "matchmaker/server_registry.hpp"

using json = nlohmann::json;
using namespace mm;

struct TicketStatus {
  Ticket ticket;
  bool matched = false;
  uint32_t match_id = 0;
  std::string host;
  uint16_t port = 0;
};

class Matchmaker {
 public:
  explicit Matchmaker(QueueConfig cfg) : queue_(cfg) {}

  uint64_t enqueue(uint32_t player, int skill) {
    std::lock_guard lk(mu_);
    Ticket t{next_ticket_++, player, skill, Clock::now()};
    queue_.add(t);
    tickets_[t.id].ticket = t;
    return t.id;
  }

  bool cancel(uint64_t id) {
    std::lock_guard lk(mu_);
    tickets_.erase(id);
    return queue_.remove(id);
  }

  std::optional<json> status(uint64_t id) {
    std::lock_guard lk(mu_);
    auto it = tickets_.find(id);
    if (it == tickets_.end()) return std::nullopt;
    const auto& s = it->second;
    if (!s.matched) return json{{"status", "waiting"}};
    json j{{"status", "matched"}, {"match_id", s.match_id}, {"host", s.host}, {"port", s.port}};
    tickets_.erase(it);  // delivered once; client now talks to the game server
    return j;
  }

  json heartbeat(const json& b) {
    std::lock_guard lk(mu_);
    auto out = registry_.heartbeat(b.at("id"), b.at("host"), b.at("port"), b.at("capacity"),
                                   b.at("active"), Clock::now());
    json arr = json::array();
    for (const auto& a : out) {
      arr.push_back({{"match_id", a.match_id}, {"players", a.players}});
      ticket_of_match_.erase(a.match_id);  // delivered: no longer needs a requeue path
    }
    return json{{"assignments", arr}};
  }

  // One matching pass: form matches, place them on servers, requeue what can't be placed.
  void run_once() {
    std::lock_guard lk(mu_);
    auto now = Clock::now();
    for (auto& orphan : registry_.evict_stale(now)) requeue_players(orphan.match_id);

    for (auto& m : queue_.tick(now)) {
      Assignment a{next_match_++, {}};
      for (const auto& t : m) a.players.push_back(t.player);
      auto server = registry_.place(a, now);
      if (!server) {  // no capacity: put everyone back with original wait time
        for (const auto& t : m) queue_.add(t);
        continue;
      }
      ++matches_total_;
      for (const auto& t : m) {
        auto& s = tickets_[t.id];
        s = {t, true, a.match_id, (*server)->host, (*server)->port};
        ticket_of_match_[a.match_id].push_back(t.id);
      }
    }
  }

  std::string metrics() {
    std::lock_guard lk(mu_);
    auto now = Clock::now();
    return "# TYPE mm_queue_size gauge\nmm_queue_size " + std::to_string(queue_.size()) +
           "\n# TYPE mm_matches_total counter\nmm_matches_total " + std::to_string(matches_total_) +
           "\n# TYPE mm_servers_alive gauge\nmm_servers_alive " + std::to_string(registry_.alive(now)) + "\n";
  }

 private:
  void requeue_players(uint32_t match_id) {  // server died before pulling the match
    for (auto tid : ticket_of_match_[match_id]) {
      auto it = tickets_.find(tid);
      if (it == tickets_.end()) continue;
      it->second.matched = false;
      queue_.add(it->second.ticket);
    }
    ticket_of_match_.erase(match_id);
  }

  std::mutex mu_;
  MatchQueue queue_;
  ServerRegistry registry_;
  std::unordered_map<uint64_t, TicketStatus> tickets_;
  std::unordered_map<uint32_t, std::vector<uint64_t>> ticket_of_match_;
  uint64_t next_ticket_ = 1;
  uint32_t next_match_ = 1;
  uint64_t matches_total_ = 0;
};

int main() {
  QueueConfig cfg;
  cfg.match_size = env_int("MATCH_SIZE", 2);
  Matchmaker mm(cfg);
  httplib::Server svr;

  auto bad = [](httplib::Response& res, const std::string& msg) {
    res.status = 400;
    res.set_content(json{{"error", msg}}.dump(), "application/json");
  };

  svr.Post("/v1/queue", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      auto b = json::parse(req.body);
      auto id = mm.enqueue(b.at("player_id"), b.at("skill"));
      res.set_content(json{{"ticket_id", id}}.dump(), "application/json");
    } catch (const std::exception& e) { bad(res, e.what()); }
  });
  svr.Get(R"(/v1/tickets/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
    auto s = mm.status(std::stoull(req.matches[1]));
    if (!s) { res.status = 404; return; }
    res.set_content(s->dump(), "application/json");
  });
  svr.Delete(R"(/v1/tickets/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
    res.status = mm.cancel(std::stoull(req.matches[1])) ? 204 : 404;
  });
  svr.Post("/v1/servers/heartbeat", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      res.set_content(mm.heartbeat(json::parse(req.body)).dump(), "application/json");
    } catch (const std::exception& e) { bad(res, e.what()); }
  });
  svr.Get("/healthz", [](const httplib::Request&, httplib::Response& res) { res.set_content("ok", "text/plain"); });
  svr.Get("/metrics", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(mm.metrics(), "text/plain; version=0.0.4");
  });

  std::atomic<bool> running{true};
  std::thread loop([&] {
    while (running) {
      mm.run_once();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  int port = env_int("PORT", 8080);
  std::cout << "matchmaker listening on :" << port << " (match size " << cfg.match_size << ")" << std::endl;
  svr.listen("0.0.0.0", port);
  running = false;
  loop.join();
}
