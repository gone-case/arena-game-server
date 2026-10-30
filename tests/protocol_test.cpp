#include <gtest/gtest.h>

#include "common/protocol.hpp"

using namespace proto;

TEST(Protocol, SnapshotRoundTrip) {
  Snapshot s{7, 3, {{1, 1.5f, 2.5f}, {2, 3.0f, 4.0f}}};
  auto bytes = encode_snapshot(s);
  Reader r(bytes.data(), bytes.size());
  Msg t;
  ASSERT_TRUE(r.get(t));
  EXPECT_EQ(t, Msg::Snapshot);
  auto d = decode_snapshot(r);
  ASSERT_TRUE(d);
  EXPECT_EQ(d->tick, 7u);
  EXPECT_EQ(d->ack_seq, 3u);
  ASSERT_EQ(d->players.size(), 2u);
  EXPECT_FLOAT_EQ(d->players[1].y, 4.0f);
}

TEST(Protocol, TruncatedPacketIsRejected) {
  auto bytes = encode_snapshot({1, 1, {{1, 1, 1}, {2, 2, 2}}});
  for (size_t cut = 1; cut < bytes.size(); ++cut) {
    Reader r(bytes.data(), cut);
    Msg t;
    r.get(t);
    EXPECT_FALSE(decode_snapshot(r)) << "cut at " << cut;
  }
}

TEST(Protocol, InputEncoding) {
  auto b = encode_input(9, 100, -1, 1);
  EXPECT_EQ(b.size(), 1u + 4 + 4 + 1 + 1);
}
