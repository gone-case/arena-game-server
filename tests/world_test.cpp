#include <gtest/gtest.h>

#include "gameserver/world.hpp"

using namespace gs;

TEST(World, OnlyAssignedPlayersCanJoin) {
  World w;
  auto now = Clock::now();
  w.add_match(1, {10, 11}, now);
  EXPECT_TRUE(w.join(1, 10, now));
  EXPECT_FALSE(w.join(1, 99, now));
  EXPECT_FALSE(w.join(2, 10, now));
}

TEST(World, InputMovesAndRejectsReplays) {
  World w;
  auto now = Clock::now();
  w.add_match(1, {10}, now);
  EXPECT_FALSE(w.apply_input(10, 1, 1, 0, now));  // not joined yet
  w.join(1, 10, now);
  float x0 = w.player(10)->x;
  EXPECT_TRUE(w.apply_input(10, 1, 1, 0, now));
  EXPECT_FLOAT_EQ(w.player(10)->x, x0 + proto::kSpeed * proto::kDt);
  EXPECT_FALSE(w.apply_input(10, 1, 1, 0, now));  // duplicate seq
}

TEST(World, RateLimitsInputsPerTick) {
  World w;
  auto now = Clock::now();
  w.add_match(1, {10}, now);
  w.join(1, 10, now);
  uint32_t seq = 0;
  for (int i = 0; i < World::kMaxInputsPerTick; ++i) EXPECT_TRUE(w.apply_input(10, ++seq, 1, 0, now));
  EXPECT_FALSE(w.apply_input(10, ++seq, 1, 0, now));
  w.end_tick();
  EXPECT_TRUE(w.apply_input(10, ++seq, 1, 0, now));
}

TEST(World, ClampsToArena) {
  float x = proto::kArena, y = 0;
  proto::step(x, y, 1, -1);
  EXPECT_FLOAT_EQ(x, proto::kArena);
  EXPECT_FLOAT_EQ(y, 0.0f);
}

TEST(World, SnapshotCarriesPerPlayerAck) {
  World w;
  auto now = Clock::now();
  w.add_match(1, {10, 11}, now);
  w.join(1, 10, now);
  w.join(1, 11, now);
  w.apply_input(10, 5, 1, 0, now);
  auto s = w.snapshot_for(1, 10, 42);
  EXPECT_EQ(s.tick, 42u);
  EXPECT_EQ(s.ack_seq, 5u);
  EXPECT_EQ(s.players.size(), 2u);
  EXPECT_EQ(w.snapshot_for(1, 11, 42).ack_seq, 0u);
}

TEST(World, ReapsIdleMatches) {
  World w;
  auto now = Clock::now();
  w.add_match(1, {10}, now);
  EXPECT_TRUE(w.reap_idle(now + std::chrono::seconds(5), std::chrono::seconds(15)).empty());
  EXPECT_EQ(w.reap_idle(now + std::chrono::seconds(16), std::chrono::seconds(15)).size(), 1u);
  EXPECT_FALSE(w.match_of(10));
}
