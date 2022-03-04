#include <a0/file.hpp>
#include <a0/string_view.hpp>
#include <a0/topic.hpp>

#include <doctest.h>

#include <ostream>
#include <string>

static const char TMPL[] = "aaa{topic}ccc";
static const char TOPIC[] = "bbb";
static const char REL_PATH[] = "aaabbbccc";
static const char ABS_PATH[] = "/dev/shm/alephzero/aaabbbccc";

TEST_CASE("topic] cpp topic_path") {
  REQUIRE(a0::topic_path(TMPL, TOPIC) == REL_PATH);

  REQUIRE_THROWS_WITH(
      a0::topic_path(nullptr, TOPIC),
      "Invalid topic name");

  REQUIRE_THROWS_WITH(
      a0::topic_path("", TOPIC),
      "Invalid topic name");

  REQUIRE_THROWS_WITH(
      a0::topic_path(TMPL, nullptr),
      "Invalid topic name");

  REQUIRE_THROWS_WITH(
      a0::topic_path(TMPL, ""),
      "Invalid topic name");

  REQUIRE_THROWS_WITH(
      a0::topic_path(TMPL, "/abc"),
      "Invalid topic name");
}

TEST_CASE("topic] cpp topic_open") {
  a0::File unused(ABS_PATH);
  a0::File::remove(ABS_PATH);
  REQUIRE(a0::topic_open(TMPL, TOPIC, a0::File::Options::DEFAULT).path() == ABS_PATH);
  a0::File::remove(ABS_PATH);
}
