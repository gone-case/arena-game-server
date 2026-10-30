// Game server: authoritative fixed-tick UDP simulation. Registers with the matchmaker via
// heartbeats and pulls the matches assigned to it.
#include <arpa/inet.h>
#include <httplib.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

#include "common/env.hpp"
#include "gameserver/world.hpp"

using json = nlohmann::json;
using namespace std::chrono;

static std::atomic<bool> g_running{true};

struct Endpoint {
  sockaddr_in addr{};
};

int main() {
  std::signal(SIGINT, [](int) { g_running = false; });
  std::signal(SIGTERM, [](int) { g_running = false; });

  const std::string id = env_or("GS_ID", "gs-1");
  const std::string adv_host = env_or("GS_HOST", "127.0.0.1");  // address clients should use
  const int port = env_int("GS_PORT", 7777);
  const int capacity = env_int("GS_CAPACITY", 50);
  const std::string mm_host = env_or("MM_HOST", "127.0.0.1");
  const int mm_port = env_int("MM_PORT", 8080);
  const int hb_ms = env_int("HEARTBEAT_MS", 500);

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = INADDR_ANY;
  bind_addr.sin_port = htons(port);
  if (bind(sock, reinterpret_cast<sockaddr*>(&bind_addr), sizeof bind_addr) < 0) {
    perror("bind");
    return 1;
  }

  gs::World world;
  std::mutex inbox_mu;
  std::vector<json> inbox;  // assignments from heartbeat thread -> tick thread
  std::atomic<uint32_t> active{0};

  std::thread heartbeat([&] {
    httplib::Client cli(mm_host, mm_port);
    cli.set_connection_timeout(1);
    while (g_running) {
      json body{{"id", id}, {"host", adv_host}, {"port", port}, {"capacity", capacity}, {"active", active.load()}};
      if (auto res = cli.Post("/v1/servers/heartbeat", body.dump(), "application/json"); res && res->status == 200) {
        auto j = json::parse(res->body);
        std::lock_guard lk(inbox_mu);
        for (auto& a : j["assignments"]) inbox.push_back(a);
      } else {
        std::cerr << "heartbeat failed\n";
      }
      std::this_thread::sleep_for(milliseconds(hb_ms));
    }
  });

  std::unordered_map<uint32_t, Endpoint> endpoints;  // player -> last seen address
  uint32_t tick = 0;
  auto next_tick = steady_clock::now();
  const auto tick_len = duration_cast<steady_clock::duration>(duration<double>(proto::kDt));
  std::cout << id << " listening udp :" << port << " @" << proto::kTickRate << "Hz" << std::endl;

  while (g_running) {
    // 1) Receive until the next tick is due
    for (;;) {
      auto wait = duration_cast<milliseconds>(next_tick - steady_clock::now()).count();
      if (wait <= 0) break;
      pollfd pfd{sock, POLLIN, 0};
      if (poll(&pfd, 1, static_cast<int>(wait)) <= 0) continue;

      uint8_t buf[1500];
      sockaddr_in from{};
      socklen_t flen = sizeof from;
      ssize_t n = recvfrom(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen);
      if (n <= 0) continue;
      proto::Reader r(buf, static_cast<size_t>(n));
      proto::Msg type;
      if (!r.get(type)) continue;
      auto now = steady_clock::now();

      if (type == proto::Msg::Join) {
        uint32_t match, player;
        if (r.get(match) && r.get(player) && world.join(match, player, now)) endpoints[player] = {from};
      } else if (type == proto::Msg::Input) {
        uint32_t player, seq;
        int8_t dx, dy;
        if (r.get(player) && r.get(seq) && r.get(dx) && r.get(dy) && world.apply_input(player, seq, dx, dy, now))
          endpoints[player] = {from};  // handles client NAT rebinding
      } else if (type == proto::Msg::Ping) {
        uint64_t t;
        if (r.get(t)) {
          auto pong = proto::encode_ping(proto::Msg::Pong, t);
          sendto(sock, pong.data(), pong.size(), 0, reinterpret_cast<sockaddr*>(&from), flen);
        }
      }
    }

    // 2) Tick: pull new matches, broadcast snapshots, reap idle matches
    auto now = steady_clock::now();
    {
      std::lock_guard lk(inbox_mu);
      for (auto& a : inbox) {
        world.add_match(a["match_id"], a["players"].get<std::vector<uint32_t>>(), now);
        std::cout << "match " << a["match_id"] << " started with players " << a["players"].dump() << std::endl;
      }
      inbox.clear();
    }
    ++tick;
    // Each player gets its own snapshot because ack_seq is per player (for reconciliation)
    for (auto it = endpoints.begin(); it != endpoints.end();) {
      auto mid = world.match_of(it->first);
      if (!mid) { it = endpoints.erase(it); continue; }
      auto pkt = proto::encode_snapshot(world.snapshot_for(*mid, it->first, tick));
      sendto(sock, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&it->second.addr), sizeof it->second.addr);
      ++it;
    }
    world.end_tick();
    for (auto mid : world.reap_idle(now, seconds(15))) std::cout << "match " << mid << " ended (idle)\n";
    active = static_cast<uint32_t>(world.match_count());
    next_tick += tick_len;
    if (steady_clock::now() - next_tick > seconds(1)) next_tick = steady_clock::now();  // don't spiral if stalled
  }

  heartbeat.join();
  close(sock);
}
