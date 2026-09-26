/** @file tests/unit/test_precision_touchpad.cpp
 * @brief Validate remote touchpad frames without injecting operating-system input.
 */
#include "src/precision_touchpad.h"

#include <gtest/gtest.h>

namespace {
  /**
   * @brief Build an encoded frame containing two contacts.
   */
  std::array<std::uint8_t, 88> sample() {
    std::array<std::uint8_t, 88> bytes {};
    bytes[3] = 84;
    bytes[4] = 8;
    bytes[7] = 0x55;
    bytes[8] = 1;
    bytes[9] = 1;
    bytes[10] = 2;
    bytes[12] = 1;
    bytes[16] = 10;
    bytes[21] = 32;
    bytes[25] = 16;
    bytes[28] = 1;
    bytes[33] = 4;
    bytes[37] = 4;
    bytes[40] = 2;
    bytes[45] = 8;
    bytes[49] = 4;
    return bytes;
  }
}  // namespace

TEST(PrecisionTouchpad, DecodesFullContactFrame) {
  auto bytes = sample();
  precision_touchpad::frame frame;
  ASSERT_TRUE(precision_touchpad::decode(bytes, frame));
  EXPECT_EQ(frame.width, 8192u);
  EXPECT_EQ(frame.height, 4096u);
  EXPECT_EQ(frame.count, 2);
  EXPECT_EQ(frame.contacts[1].x, 2048u);
}

TEST(PrecisionTouchpad, RejectsTruncatedAndOversizedFrames) {
  auto bytes = sample();
  precision_touchpad::frame frame;
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    EXPECT_FALSE(precision_touchpad::decode(std::span(bytes).first(size), frame));
  }
  std::array<std::uint8_t, 89> oversized {};
  EXPECT_FALSE(precision_touchpad::decode(oversized, frame));
}

TEST(PrecisionTouchpad, RejectsInvalidContactsAndHeaders) {
  precision_touchpad::frame frame;
  for (auto [offset, value] : std::array<std::pair<int, int>, 10> {{{3, 83}, {4, 9}, {8, 2}, {9, 3}, {10, 6}, {11, 1}, {28, 32}, {40, 1}, {34, 1}, {21, 0}}}) {
    auto bytes = sample();
    bytes[offset] = value;
    EXPECT_FALSE(precision_touchpad::decode(bytes, frame)) << offset;
  }
}

TEST(PrecisionTouchpad, AcceptsEmptySnapshotAndCancelOnlyWithoutContacts) {
  auto bytes = sample();
  precision_touchpad::frame frame;
  bytes[10] = 0;
  EXPECT_TRUE(precision_touchpad::decode(bytes, frame));
  bytes[9] = 2;
  EXPECT_TRUE(precision_touchpad::decode(bytes, frame));
  bytes[10] = 1;
  EXPECT_FALSE(precision_touchpad::decode(bytes, frame));
}
