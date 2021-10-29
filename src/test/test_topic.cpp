#include <a0/topic.hpp>

#include <doctest.h>

#include <cstdint>

static const char TMPL[] = "aaa{topic}ccc{topic}{topic}ddd";
static const char TOPIC[] = "bbb";
static const char REL_PATH[] = "aaabbbcccbbbbbbddd";
static const char ABS_PATH[] = "/dev/shm/alephzero/aaabbbcccbbbbbbddd";

TEST_CASE("topic] cpp topic_path") {
  REQUIRE(a0::topic_path(TMPL, TOPIC) == REL_PATH);

  REQUIRE_THROWS_WITH(
      [&]() { a0::topic_path(nullptr, TOPIC); }(),
      "Invalid topic name");

  REQUIRE_THROWS_WITH(
      [&]() { a0::topic_path("", TOPIC); }(),
      "Invalid topic name");

  REQUIRE_THROWS_WITH(
      [&]() { a0::topic_path(TMPL, nullptr); }(),
      "Invalid topic name");

  REQUIRE_THROWS_WITH(
      [&]() { a0::topic_path(TMPL, ""); }(),
      "Invalid topic name");

  REQUIRE_THROWS_WITH(
      [&]() { a0::topic_path(TMPL, "/abc"); }(),
      "Invalid topic name");
}

TEST_CASE("topic] cpp topic_open") {
  a0::File unused(ABS_PATH);
  a0::File::remove(ABS_PATH);
  REQUIRE(a0::topic_open(TMPL, TOPIC, a0::File::Options::DEFAULT).path() == ABS_PATH);
  a0::File::remove(ABS_PATH);
}
