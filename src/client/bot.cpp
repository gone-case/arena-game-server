// Headless bot client + load generator.
// Each bot: queue -> poll ticket -> join game server over UDP -> play with client-side prediction
// and server reconciliation. Prints aggregate queue time, RTT and prediction-error stats.
//
// usage: bot [--mm host:port] [--bots N] [--seconds S] [--start-id ID] [--loss PCT]
//   --loss drops that % of outgoing packets to simulate a bad network
#include <httplib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <deque>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>

#include "common/protocol.hpp"

using json = nlohmann::json;
using namespace std::chrono;

struct Stats {
  std::mutex mu;
  int matched = 0, failed = 0, queue_fail = 0, match_timeout = 0, udp_fail = 0;
  double queue_ms = 0, rtt_ms = 0, correction = 0;
  long rtt_samples = 0, snapshots = 0;
};

static uint64_t now_us() {
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

static int udp_connect(const std::string& host, uint16_t port) {
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return -1;
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  int ok = connect(s, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (ok < 0) { close(s); return -1; }
  timeval tv{0, 1000};  // 1ms recv timeout -> non-blocking-ish drain
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return s;
}

static void run_bot(uint32_t player, const std::string& mm_host, int mm_port, int secs, int loss, Stats& st) {
  std::mt19937 rng(player);
  httplib::Client cli(mm_host, mm_port);
  int skill = std::uniform_int_distribution(800, 2000)(rng);

  // ---- 1. Matchmaking ----
  auto t0 = steady_clock::now();
  cli.set_connection_timeout(2);
  cli.set_read_timeout(5);
  httplib::Result q;
  // Retry transient errors with exponential backoff + jitter so bots don't retry in lockstep
  for (int attempt = 0; attempt < 5 && !(q && q->status == 200); ++attempt) {
    if (attempt) std::this_thread::sleep_for(milliseconds((100 << attempt) + rng() % 100));
    q = cli.Post("/v1/queue", json{{"player_id", player}, {"skill", skill}}.dump(), "application/json");
  }
  if (!q || q->status != 200) { std::lock_guard lk(st.mu); ++st.failed; ++st.queue_fail; return; }
  auto ticket = json::parse(q->body)["ticket_id"].get<uint64_t>();

  json m;
  while (steady_clock::now() - t0 < seconds(30)) {
    auto r = cli.Get("/v1/tickets/" + std::to_string(ticket));
    if (r && r->status == 200 && (m = json::parse(r->body))["status"] == "matched") break;
    std::this_thread::sleep_for(milliseconds(200));
  }
  if (m.value("status", "") != "matched") { std::lock_guard lk(st.mu); ++st.failed; ++st.match_timeout; return; }
  double queue_ms = duration<double, std::milli>(steady_clock::now() - t0).count();

  // ---- 2. Connect to assigned game server ----
  uint32_t match = m["match_id"];
  int sock = udp_connect(m["host"], m["port"]);
  if (sock < 0) { std::lock_guard lk(st.mu); ++st.failed; ++st.udp_fail; return; }
  std::uniform_int_distribution pct(0, 99);
  auto send = [&](const std::vector<uint8_t>& p) {
    if (pct(rng) >= loss) ::send(sock, p.data(), p.size(), 0);
  };

  // ---- 3. Play: predict locally, reconcile with authoritative snapshots ----
  struct Pending { uint32_t seq; int8_t dx, dy; };
  std::deque<Pending> pending;
  float x = 0, y = 0;
  bool have_state = false;
  uint32_t seq = 0;
  int8_t dx = 0, dy = 0;
  double rtt_sum = 0, corr_sum = 0;
  long rtt_n = 0, snaps = 0;

  auto end = steady_clock::now() + seconds(secs);
  auto next_tick = steady_clock::now();
  int ticks = 0;
  while (steady_clock::now() < end) {
    if (!have_state) {
      if (ticks % 3 == 0) send(proto::encode_join(match, player));
    } else {
      if (ticks % proto::kTickRate == 0) {  // new random direction every second
        dx = static_cast<int8_t>(std::uniform_int_distribution(-1, 1)(rng));
        dy = static_cast<int8_t>(std::uniform_int_distribution(-1, 1)(rng));
        send(proto::encode_ping(proto::Msg::Ping, now_us()));
      }
      ++seq;
      proto::step(x, y, dx, dy);  // prediction: move immediately
      pending.push_back({seq, dx, dy});
      send(proto::encode_input(player, seq, dx, dy));
    }

    // Drain everything that arrived
    uint8_t buf[1500];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof buf, 0)) > 0) {
      proto::Reader r(buf, static_cast<size_t>(n));
      proto::Msg type;
      if (!r.get(type)) continue;
      if (type == proto::Msg::Pong) {
        uint64_t t;
        if (r.get(t)) { rtt_sum += (now_us() - t) / 1000.0; ++rtt_n; }
      } else if (type == proto::Msg::Snapshot) {
        auto s = proto::decode_snapshot(r);
        if (!s) continue;
        ++snaps;
        for (const auto& p : s->players) {
          if (p.id != player) continue;
          // Reconciliation: rewind to server state, replay unacknowledged inputs
          while (!pending.empty() && pending.front().seq <= s->ack_seq) pending.pop_front();
          float px = x, py = y;
          x = p.x;
          y = p.y;
          for (const auto& in : pending) proto::step(x, y, in.dx, in.dy);
          if (have_state) corr_sum += std::hypot(x - px, y - py);
          have_state = true;
        }
      }
    }

    ++ticks;
    next_tick += duration_cast<steady_clock::duration>(duration<double>(proto::kDt));
    std::this_thread::sleep_until(next_tick);
  }
  close(sock);

  std::lock_guard lk(st.mu);
  ++st.matched;
  st.queue_ms += queue_ms;
  st.rtt_ms += rtt_sum;
  st.rtt_samples += rtt_n;
  st.snapshots += snaps;
  st.correction += snaps ? corr_sum / snaps : 0;
}

int main(int argc, char** argv) {
  std::string mm = "127.0.0.1:8080";
  int bots = 4, secs = 10, loss = 0;
  uint32_t start_id = 1;
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string a = argv[i];
    if (a == "--mm") mm = argv[i + 1];
    else if (a == "--bots") bots = std::stoi(argv[i + 1]);
    else if (a == "--seconds") secs = std::stoi(argv[i + 1]);
    else if (a == "--loss") loss = std::stoi(argv[i + 1]);
    else if (a == "--start-id") start_id = static_cast<uint32_t>(std::stoul(argv[i + 1]));
  }
  auto colon = mm.find(':');
  std::string host = mm.substr(0, colon);
  int port = std::stoi(mm.substr(colon + 1));

  Stats st;
  std::vector<std::thread> threads;
  for (int i = 0; i < bots; ++i)
    threads.emplace_back(run_bot, start_id + i, host, port, secs, loss, std::ref(st));
  for (auto& t : threads) t.join();

  std::cout << "bots matched:        " << st.matched << "/" << bots << " (failed " << st.failed << ": queue " << st.queue_fail << ", match timeout "
            << st.match_timeout << ", udp " << st.udp_fail << ")\n";
  if (st.matched) {
    std::cout << "avg queue time:      " << st.queue_ms / st.matched << " ms\n"
              << "avg RTT:             " << (st.rtt_samples ? st.rtt_ms / st.rtt_samples : 0) << " ms\n"
              << "snapshots received:  " << st.snapshots << "\n"
              << "avg reconciliation correction: " << st.correction / st.matched << " units/snapshot\n";
  }
  return st.failed ? 1 : 0;
}
