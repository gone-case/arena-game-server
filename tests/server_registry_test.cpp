#include <gtest/gtest.h>

#include "matchmaker/server_registry.hpp"

using namespace mm;
using namespace std::chrono;

TEST(ServerRegistry, PlacesOnLeastLoaded) {
  ServerRegistry r;
  auto now = Clock::now();
  r.heartbeat("a", "h", 1, 10, 5, now);
  r.heartbeat("b", "h", 2, 10, 1, now);
  auto s = r.place({1, {1, 2}}, now);
  ASSERT_TRUE(s);
  EXPECT_EQ((*s)->id, "b");
}

TEST(ServerRegistry, RespectsCapacity) {
  ServerRegistry r;
  auto now = Clock::now();
  r.heartbeat("a", "h", 1, 1, 0, now);
  EXPECT_TRUE(r.place({1, {}}, now));
  EXPECT_FALSE(r.place({2, {}}, now));  // pending counts toward load
}

TEST(ServerRegistry, HeartbeatDrainsAssignments) {
  ServerRegistry r;
  auto now = Clock::now();
  r.heartbeat("a", "h", 1, 10, 0, now);
  r.place({7, {1, 2}}, now);
  auto out = r.heartbeat("a", "h", 1, 10, 0, now);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].match_id, 7u);
  EXPECT_TRUE(r.heartbeat("a", "h", 1, 10, 1, now).empty());
}

TEST(ServerRegistry, EvictsDeadServerAndReturnsOrphans) {
  ServerRegistry r(seconds(5));
  auto now = Clock::now();
  r.heartbeat("a", "h", 1, 10, 0, now);
  r.place({3, {1, 2}}, now);
  auto later = now + seconds(6);
  EXPECT_FALSE(r.place({4, {}}, later));  // dead servers never receive matches
  auto orphans = r.evict_stale(later);
  ASSERT_EQ(orphans.size(), 1u);
  EXPECT_EQ(orphans[0].match_id, 3u);
  EXPECT_EQ(r.alive(later), 0u);
}
