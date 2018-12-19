#include "gtest/gtest.h"

namespace Nighthawk {

class ClientOptionsTest {

public:
  ClientOptionsTest() {}
  void SetUp();
  void TearDown();
};

TEST(ClientOptionsTest, TestTest) { EXPECT_EQ("hello", "hello"); }

} // namespace Nighthawk
