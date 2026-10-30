// Binary UDP protocol shared by game server and clients.
// Little-endian on the wire (all supported targets are little-endian).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace proto {

enum class Msg : uint8_t { Join = 1, Input = 2, Snapshot = 3, Ping = 4, Pong = 5 };

class Writer {
 public:
  template <typename T>
  Writer& put(T v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    buf_.insert(buf_.end(), p, p + sizeof(T));
    return *this;
  }
  const std::vector<uint8_t>& data() const { return buf_; }

 private:
  std::vector<uint8_t> buf_;
};

class Reader {
 public:
  Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
  template <typename T>
  bool get(T& out) {  // bounds-checked: malformed packets never read past the buffer
    if (pos_ + sizeof(T) > n_) return false;
    std::memcpy(&out, d_ + pos_, sizeof(T));
    pos_ += sizeof(T);
    return true;
  }

 private:
  const uint8_t* d_;
  size_t n_, pos_ = 0;
};

struct PlayerSnap {
  uint32_t id;
  float x, y;
};

struct Snapshot {
  uint32_t tick;
  uint32_t ack_seq;  // last input seq the server applied for the receiving player
  std::vector<PlayerSnap> players;
};

inline std::vector<uint8_t> encode_join(uint32_t match, uint32_t player) {
  return Writer().put(Msg::Join).put(match).put(player).data();
}
inline std::vector<uint8_t> encode_input(uint32_t player, uint32_t seq, int8_t dx, int8_t dy) {
  return Writer().put(Msg::Input).put(player).put(seq).put(dx).put(dy).data();
}
inline std::vector<uint8_t> encode_ping(Msg type, uint64_t t) { return Writer().put(type).put(t).data(); }

inline std::vector<uint8_t> encode_snapshot(const Snapshot& s) {
  Writer w;
  w.put(Msg::Snapshot).put(s.tick).put(s.ack_seq).put(static_cast<uint8_t>(s.players.size()));
  for (const auto& p : s.players) w.put(p.id).put(p.x).put(p.y);
  return w.data();
}

inline std::optional<Snapshot> decode_snapshot(Reader& r) {  // after the Msg byte
  Snapshot s;
  uint8_t n;
  if (!r.get(s.tick) || !r.get(s.ack_seq) || !r.get(n)) return std::nullopt;
  s.players.resize(n);
  for (auto& p : s.players)
    if (!r.get(p.id) || !r.get(p.x) || !r.get(p.y)) return std::nullopt;
  return s;
}

// ---- Shared deterministic movement: server simulates it, client predicts with it ----
constexpr int kTickRate = 30;
constexpr float kDt = 1.0f / kTickRate;
constexpr float kSpeed = 10.0f;  // units per second
constexpr float kArena = 100.0f;

inline void step(float& x, float& y, int8_t dx, int8_t dy) {
  float fx = static_cast<float>(dx > 0) - static_cast<float>(dx < 0);
  float fy = static_cast<float>(dy > 0) - static_cast<float>(dy < 0);
  float len = std::sqrt(fx * fx + fy * fy);
  if (len == 0) return;
  x = std::clamp(x + fx / len * kSpeed * kDt, 0.0f, kArena);
  y = std::clamp(y + fy / len * kSpeed * kDt, 0.0f, kArena);
}

}  // namespace proto
