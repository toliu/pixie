/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "src/stirling/source_connectors/socket_tracer/protocols/common/data_stream_buffer.h"

#include "src/common/testing/testing.h"

namespace px {
namespace stirling {
namespace protocols {

TEST(DataStreamTest, AddAndGet) {
  DataStreamBuffer stream_buffer(15, 15, 15);

  // Initially everything should be empty.
  EXPECT_EQ(stream_buffer.Get(0), "");
  EXPECT_EQ(stream_buffer.Get(20), "");

  // Add a basic event.
  stream_buffer.Add(0, "0123", 0);
  EXPECT_EQ(stream_buffer.Get(0), "0123");
  EXPECT_EQ(stream_buffer.Get(2), "23");
  EXPECT_EQ(stream_buffer.Get(4), "");

  // Add an adjacent event.
  stream_buffer.Add(4, "45", 4);
  EXPECT_EQ(stream_buffer.Get(0), "012345");
  EXPECT_EQ(stream_buffer.Get(2), "2345");
  EXPECT_EQ(stream_buffer.Get(4), "45");
  EXPECT_EQ(stream_buffer.Get(6), "");

  // Add an event with a gap
  stream_buffer.Add(8, "89", 8);
  EXPECT_EQ(stream_buffer.Get(0), "012345");
  EXPECT_EQ(stream_buffer.Get(2), "2345");
  EXPECT_EQ(stream_buffer.Get(4), "45");
  EXPECT_EQ(stream_buffer.Get(6), "");
  EXPECT_EQ(stream_buffer.Get(8), "89");

  // Fill in the gap with an out-of-order event.
  stream_buffer.Add(6, "67", 6);
  EXPECT_EQ(stream_buffer.Get(0), "0123456789");
  EXPECT_EQ(stream_buffer.Get(2), "23456789");
  EXPECT_EQ(stream_buffer.Get(4), "456789");
  EXPECT_EQ(stream_buffer.Get(6), "6789");
  EXPECT_EQ(stream_buffer.Get(8), "89");

  // Fill the buffer.
  stream_buffer.Add(10, "abcde", 10);
  EXPECT_EQ(stream_buffer.Get(0), "0123456789abcde");
  EXPECT_EQ(stream_buffer.Get(5), "56789abcde");

  // Cause the buffer to expand such that data should expire.
  stream_buffer.Add(15, "fghij", 15);
  EXPECT_EQ(stream_buffer.Get(0), "");
  EXPECT_EQ(stream_buffer.Get(4), "");
  EXPECT_EQ(stream_buffer.Get(5), "56789abcdefghij");
  EXPECT_EQ(stream_buffer.Get(6), "6789abcdefghij");
  EXPECT_EQ(stream_buffer.Get(8), "89abcdefghij");
  EXPECT_EQ(stream_buffer.Get(10), "abcdefghij");

  // Jump ahead, leaving a gap.
  stream_buffer.Add(28, "st", 28);
  stream_buffer.Add(26, "qr", 26);
  EXPECT_EQ(stream_buffer.Get(14), "");
  EXPECT_EQ(stream_buffer.Get(15), "fghij");
  EXPECT_EQ(stream_buffer.Get(26), "qrst");

  // Fill in the gap.
  stream_buffer.Add(22, "mn", 22);
  stream_buffer.Add(20, "kl", 20);
  stream_buffer.Add(24, "op", 24);
  EXPECT_EQ(stream_buffer.Get(14), "");
  EXPECT_EQ(stream_buffer.Get(15), "fghijklmnopqrst");
  EXPECT_EQ(stream_buffer.Get(26), "qrst");

  // Remove some of the head.
  stream_buffer.RemovePrefix(5);
  EXPECT_EQ(stream_buffer.Get(19), "");
  EXPECT_EQ(stream_buffer.Get(20), "klmnopqrst");
  EXPECT_EQ(stream_buffer.Get(26), "qrst");

  // Jump ahead such that everything should expire.
  stream_buffer.Add(100, "0123456789", 100);
  EXPECT_EQ(stream_buffer.Get(99), "");
  EXPECT_EQ(stream_buffer.Get(100), "0123456789");
  EXPECT_EQ(stream_buffer.Get(110), "");

  // Jump back to a point where *part* of the incoming data is too old.
  stream_buffer.Add(90, "abcdefghi", 90);
  EXPECT_EQ(stream_buffer.Get(94), "");
  EXPECT_EQ(stream_buffer.Get(95), "fghi");
  EXPECT_EQ(stream_buffer.Get(100), "0123456789");
  EXPECT_EQ(stream_buffer.Get(110), "");

  // Add something larger than the capacity. Head should get truncated.
  stream_buffer.Add(120, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 120);
  EXPECT_EQ(stream_buffer.Get(120), "");
  EXPECT_EQ(stream_buffer.Get(130), "");
  EXPECT_EQ(stream_buffer.Get(131), "LMNOPQRSTUVWXYZ");

  // Add something way in the past. Should be ignored.
  stream_buffer.Add(50, "oldie", 50);
  EXPECT_EQ(stream_buffer.Get(50), "");
  EXPECT_EQ(stream_buffer.Get(130), "");
  EXPECT_EQ(stream_buffer.Get(131), "LMNOPQRSTUVWXYZ");
}

TEST(DataStreamTest, RemovePrefixAndTrim) {
  DataStreamBuffer stream_buffer(15, 15, 15);

  // Add some events with a gap.
  stream_buffer.Add(0, "0123", 0);
  stream_buffer.Add(10, "abcd", 10);
  EXPECT_EQ(stream_buffer.Get(0), "0123");
  EXPECT_EQ(stream_buffer.Get(10), "abcd");

  // Remove part of the first event.
  stream_buffer.RemovePrefix(2);
  EXPECT_EQ(stream_buffer.Get(0), "");
  EXPECT_EQ(stream_buffer.Get(2), "23");
  EXPECT_EQ(stream_buffer.Get(10), "abcd");
  EXPECT_EQ(stream_buffer.Head(), "23");

  // Remove more of the first event.
  stream_buffer.RemovePrefix(1);
  EXPECT_EQ(stream_buffer.Get(0), "");
  EXPECT_EQ(stream_buffer.Get(2), "");
  EXPECT_EQ(stream_buffer.Get(3), "3");
  EXPECT_EQ(stream_buffer.Get(4), "");
  EXPECT_EQ(stream_buffer.Get(10), "abcd");
  EXPECT_EQ(stream_buffer.Head(), "3");

  // Remove more of the first event.
  stream_buffer.RemovePrefix(3);
  EXPECT_EQ(stream_buffer.Get(0), "");
  EXPECT_EQ(stream_buffer.Get(2), "");
  EXPECT_EQ(stream_buffer.Get(3), "");
  EXPECT_EQ(stream_buffer.Get(5), "");
  EXPECT_EQ(stream_buffer.Get(10), "abcd");
  EXPECT_EQ(stream_buffer.Head(), "");

  // Head should have a gap, trim it.
  stream_buffer.Trim();
  EXPECT_EQ(stream_buffer.Get(0), "");
  EXPECT_EQ(stream_buffer.Get(10), "abcd");
  EXPECT_EQ(stream_buffer.Head(), "abcd");

  // Another trim shouldn't impact anything.
  stream_buffer.Trim();
  EXPECT_EQ(stream_buffer.Get(0), "");
  EXPECT_EQ(stream_buffer.Get(10), "abcd");
  EXPECT_EQ(stream_buffer.Head(), "abcd");

  // Removing negative amount should do nothing in production.
  // In debug mode, it should die.
  EXPECT_DEBUG_DEATH(stream_buffer.RemovePrefix(-1), "");
  EXPECT_EQ(stream_buffer.Get(0), "");
  EXPECT_EQ(stream_buffer.Get(10), "abcd");
  EXPECT_EQ(stream_buffer.Head(), "abcd");
}

TEST(DataStreamTest, Timestamp) {
  DataStreamBuffer stream_buffer(15, 15, 15);

  EXPECT_NOT_OK(stream_buffer.GetTimestamp(0));
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(20));

  stream_buffer.Add(0, "0123", 0);
  stream_buffer.Add(4, "4567", 4);
  stream_buffer.RemovePrefix(1);

  EXPECT_NOT_OK(stream_buffer.GetTimestamp(0));
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(1), 0);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(3), 0);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(4), 4);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(7), 4);
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(8));
}

TEST(DataStreamTest, TimestampWithGap) {
  DataStreamBuffer stream_buffer(15, 15, 15);

  stream_buffer.Add(0, "0123", 0);
  stream_buffer.Add(10, "abcd", 10);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(0), 0);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(3), 0);
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(4));
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(9));
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(10), 10);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(13), 10);

  stream_buffer.RemovePrefix(2);
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(0));
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(2), 0);
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(9));
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(10), 10);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(13), 10);

  stream_buffer.RemovePrefix(2);
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(0));
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(9));
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(10), 10);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(13), 10);

  stream_buffer.RemovePrefix(2);
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(0));
  EXPECT_NOT_OK(stream_buffer.GetTimestamp(5));
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(10), 10);
  EXPECT_OK_AND_EQ(stream_buffer.GetTimestamp(13), 10);
}

TEST(DataStreamTest, SizeAndGetPos) {
  DataStreamBuffer stream_buffer(15, 15, 15);

  // Start off empty.
  EXPECT_EQ(stream_buffer.position(), 0);
  EXPECT_EQ(stream_buffer.size(), 0);
  EXPECT_TRUE(stream_buffer.empty());

  // Add basic event.
  stream_buffer.Add(0, "0123", 0);
  EXPECT_EQ(stream_buffer.position(), 0);
  EXPECT_EQ(stream_buffer.size(), 4);
  EXPECT_FALSE(stream_buffer.empty());

  // Add event with a gap. Size includes the gap.
  stream_buffer.Add(8, "89", 8);
  EXPECT_EQ(stream_buffer.position(), 0);
  EXPECT_EQ(stream_buffer.size(), 10);
  EXPECT_FALSE(stream_buffer.empty());

  // Add event that causes events to expire. Size should be max capacity.
  stream_buffer.Add(20, "kl", 20);
  EXPECT_EQ(stream_buffer.position(), 7);
  EXPECT_EQ(stream_buffer.size(), 15);
  EXPECT_FALSE(stream_buffer.empty());

  // Trim. Size should shrink because there was an unused byte at the beginning.
  stream_buffer.Trim();
  EXPECT_EQ(stream_buffer.position(), 8);
  EXPECT_EQ(stream_buffer.size(), 14);
  EXPECT_FALSE(stream_buffer.empty());

  // Trim again. No change expected.
  stream_buffer.Trim();
  EXPECT_EQ(stream_buffer.position(), 8);
  EXPECT_EQ(stream_buffer.size(), 14);
  EXPECT_FALSE(stream_buffer.empty());

  // Jump way ahead. Size should be at max capacity.
  stream_buffer.Add(100, "!!", 100);
  EXPECT_EQ(stream_buffer.position(), 87);
  EXPECT_EQ(stream_buffer.size(), 15);
  EXPECT_FALSE(stream_buffer.empty());

  // Trim should leave us with only the two "!!" bytes.
  stream_buffer.Trim();
  EXPECT_EQ(stream_buffer.position(), 100);
  EXPECT_EQ(stream_buffer.size(), 2);
  EXPECT_FALSE(stream_buffer.empty());

  // Remove prefix should shrink size.
  stream_buffer.RemovePrefix(1);
  EXPECT_EQ(stream_buffer.size(), 1);
  EXPECT_FALSE(stream_buffer.empty());

  // Can even shrink to zero.
  stream_buffer.RemovePrefix(1);
  EXPECT_EQ(stream_buffer.size(), 0);
  EXPECT_TRUE(stream_buffer.empty());

  // Add something larger than the capacity.
  stream_buffer.Add(105, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 105);
  EXPECT_EQ(stream_buffer.size(), 15);
  EXPECT_FALSE(stream_buffer.empty());
}

TEST(DataStreamTest, LargeGap) {
  const size_t kMaxGapSize = 32;
  const size_t kAllowBeforeGapSize = 8;
  DataStreamBuffer stream_buffer(128, kMaxGapSize, kAllowBeforeGapSize);

  // Add basic event.
  stream_buffer.Add(0, "0123", 0);
  EXPECT_EQ(stream_buffer.position(), 0);
  EXPECT_EQ(stream_buffer.size(), 4);
  EXPECT_FALSE(stream_buffer.empty());

  // Add event with gap less than max_gap_size.
  stream_buffer.Add(32, "4567", 10);
  EXPECT_EQ(stream_buffer.Get(0), "0123");
  EXPECT_EQ(stream_buffer.Get(32), "4567");

  // Add event with gap larger than max_gap_size.
  stream_buffer.Add(100, "abcd", 20);
  // We should only have 4 bytes of data left in the buffer, plus size for the allow_before_gap_size
  // amount.
  EXPECT_EQ(stream_buffer.size(), 4 + kAllowBeforeGapSize);
  EXPECT_EQ(stream_buffer.Get(100), "abcd");

  // Add event more than allow_before_gap_size before the last event. This event should not be added
  // to the buffer.
  stream_buffer.Add(100 - kMaxGapSize, "test", 18);
  EXPECT_EQ(stream_buffer.Get(100 - kMaxGapSize), "");

  // Add event before the gap event but not more than allow_before_gap_size before. This event
  // should be added to the buffer.
  stream_buffer.Add(100 - kAllowBeforeGapSize, "allow", 19);
  EXPECT_EQ(stream_buffer.Get(100 - kAllowBeforeGapSize), "allow");
}

}  // namespace protocols
}  // namespace stirling
}  // namespace px
