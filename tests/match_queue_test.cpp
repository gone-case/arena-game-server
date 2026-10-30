#include <gtest/gtest.h>

#include "matchmaker/match_queue.hpp"

using namespace mm;
using namespace std::chrono;

static Ticket T(uint64_t id, int skill, Clock::time_point at) { return {id, static_cast<uint32_t>(id), skill, at}; }

TEST(MatchQueue, PairsCloseSkills) {
  MatchQueue q;
  auto now = Clock::now();
  q.add(T(1, 1000, now));
  q.add(T(2, 1020, now));
  auto m = q.tick(now);
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(q.size(), 0u);
}

TEST(MatchQueue, RejectsFarSkillsUntilWindowWidens) {
  MatchQueue q({2, 50, 50, 1000});
  auto now = Clock::now();
  q.add(T(1, 1000, now));
  q.add(T(2, 1200, now));
  EXPECT_TRUE(q.tick(now).empty());
  EXPECT_EQ(q.tick(now + seconds(4)).size(), 1u);  // window 50 + 4*50 = 250 >= 200
}

TEST(MatchQueue, PicksClosestCandidate) {
  MatchQueue q;
  auto now = Clock::now();
  q.add(T(1, 1000, now - seconds(5)));  // oldest anchor
  q.add(T(2, 1090, now));
  q.add(T(3, 1010, now));
  auto m = q.tick(now);
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0][1].id, 3u);
  EXPECT_TRUE(q.contains(2));
}

TEST(MatchQueue, OldestPlayerIsServedFirst) {
  MatchQueue q;
  auto now = Clock::now();
  q.add(T(1, 1000, now - seconds(10)));
  q.add(T(2, 1005, now));
  q.add(T(3, 1003, now));
  auto m = q.tick(now);
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0][0].id, 1u);
}

TEST(MatchQueue, CancelRemovesFromAllIndexes) {
  MatchQueue q;
  auto now = Clock::now();
  q.add(T(1, 1000, now));
  EXPECT_TRUE(q.remove(1));
  EXPECT_FALSE(q.remove(1));
  q.add(T(2, 1000, now));
  EXPECT_TRUE(q.tick(now).empty());  // cancelled ticket must not be matched
}

TEST(MatchQueue, FourPlayerMatches) {
  MatchQueue q({4, 100, 50, 1000});
  auto now = Clock::now();
  for (uint64_t i = 1; i <= 9; ++i) q.add(T(i, 1000 + static_cast<int>(i), now));
  EXPECT_EQ(q.tick(now).size(), 2u);
  EXPECT_EQ(q.size(), 1u);
}
