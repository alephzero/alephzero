#include <a0/pathglob.h>
#include <a0/pathglob.hpp>

#include <doctest.h>

#include <ostream>
#include <string>

#include "src/test_util.hpp"

TEST_CASE("pathglob] split") {
  a0_pathglob_t pathglob;

  auto REQUIRE_PART = [&](int idx, std::string val, a0_pathglob_part_type_t type) {
    REQUIRE(a0::test::str(pathglob.parts[idx].str) == val);
    REQUIRE(pathglob.parts[idx].type == type);
  };

  {
    REQUIRE_OK(a0_pathglob_init(&pathglob, "/dev/shm/**/abc*def/*.a0"));

    REQUIRE(pathglob.depth == 5);
    REQUIRE_PART(0, "dev", A0_PATHGLOB_PART_TYPE_VERBATIM);
    REQUIRE_PART(1, "shm", A0_PATHGLOB_PART_TYPE_VERBATIM);
    REQUIRE_PART(2, "**", A0_PATHGLOB_PART_TYPE_RECURSIVE);
    REQUIRE_PART(3, "abc*def", A0_PATHGLOB_PART_TYPE_PATTERN);
    REQUIRE_PART(4, "*.a0", A0_PATHGLOB_PART_TYPE_PATTERN);

    REQUIRE_OK(a0_pathglob_close(&pathglob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&pathglob, "/dev/shm/*.a0"));

    REQUIRE(pathglob.depth == 3);
    REQUIRE(a0::test::str(pathglob.parts[0].str) == "dev");
    REQUIRE(a0::test::str(pathglob.parts[1].str) == "shm");
    REQUIRE(a0::test::str(pathglob.parts[2].str) == "*.a0");

    REQUIRE_OK(a0_pathglob_close(&pathglob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&pathglob, "/*.a0"));

    REQUIRE(pathglob.depth == 1);
    REQUIRE(a0::test::str(pathglob.parts[0].str) == "*.a0");

    REQUIRE_OK(a0_pathglob_close(&pathglob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&pathglob, "/dev/shm/"));

    REQUIRE(pathglob.depth == 3);
    REQUIRE(a0::test::str(pathglob.parts[0].str) == "dev");
    REQUIRE(a0::test::str(pathglob.parts[1].str) == "shm");
    REQUIRE(a0::test::str(pathglob.parts[2].str) == "");

    REQUIRE_OK(a0_pathglob_close(&pathglob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&pathglob, "**/abc*def/*.a0"));

    REQUIRE(pathglob.depth == 6);
    REQUIRE_PART(0, "dev", A0_PATHGLOB_PART_TYPE_VERBATIM);
    REQUIRE_PART(1, "shm", A0_PATHGLOB_PART_TYPE_VERBATIM);
    REQUIRE_PART(2, "alephzero", A0_PATHGLOB_PART_TYPE_VERBATIM);
    REQUIRE_PART(3, "**", A0_PATHGLOB_PART_TYPE_RECURSIVE);
    REQUIRE_PART(4, "abc*def", A0_PATHGLOB_PART_TYPE_PATTERN);
    REQUIRE_PART(5, "*.a0", A0_PATHGLOB_PART_TYPE_PATTERN);

    REQUIRE_OK(a0_pathglob_close(&pathglob));
  }

  {
    a0::test::scope_env change_root("A0_ROOT", "/foo/bar");
    REQUIRE_OK(a0_pathglob_init(&pathglob, "**/abc*def/*.a0"));

    REQUIRE(pathglob.depth == 5);

    REQUIRE_PART(0, "foo", A0_PATHGLOB_PART_TYPE_VERBATIM);
    REQUIRE_PART(1, "bar", A0_PATHGLOB_PART_TYPE_VERBATIM);
    REQUIRE_PART(2, "**", A0_PATHGLOB_PART_TYPE_RECURSIVE);
    REQUIRE_PART(3, "abc*def", A0_PATHGLOB_PART_TYPE_PATTERN);
    REQUIRE_PART(4, "*.a0", A0_PATHGLOB_PART_TYPE_PATTERN);

    REQUIRE_OK(a0_pathglob_close(&pathglob));
  }
}

TEST_CASE("pathglob] match") {
  bool match;
  a0_pathglob_t glob;

  {
    REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/a/foo.a0"));

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
    REQUIRE(!match);

    REQUIRE_OK(a0_pathglob_close(&glob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/*/foo.a0"));

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
    REQUIRE(!match);

    REQUIRE_OK(a0_pathglob_close(&glob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/*/*.a0"));

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
    REQUIRE(!match);

    REQUIRE_OK(a0_pathglob_close(&glob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**/*.a0"));

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_close(&glob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**/b/*.a0"));

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/foo.a0", &match));
    REQUIRE(!match);

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_close(&glob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**"));

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_close(&glob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**/**/**/**/**/*******b***/*.a0"));

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/a/b/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_close(&glob));
  }

  {
    REQUIRE_OK(a0_pathglob_init(&glob, "/dev/shm/**/*.a0"));

    REQUIRE_OK(a0_pathglob_match(&glob, "/dev/shm/foo.a0", &match));
    REQUIRE(match);

    REQUIRE_OK(a0_pathglob_close(&glob));
  }
}

TEST_CASE("pathglob] cpp match") {
  a0::PathGlob glob;

  glob = a0::PathGlob("/dev/shm/a/foo.a0");
  REQUIRE(glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(!glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/*/foo.a0");
  REQUIRE(glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(!glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/*/*.a0");
  REQUIRE(glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(!glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**/*.a0");
  REQUIRE(glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**/b/*.a0");
  REQUIRE(!glob.match("/dev/shm/a/foo.a0"));
  REQUIRE(glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**");
  REQUIRE(glob.match("/dev/shm/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**/**/**/**/**/*******b***/*.a0");
  REQUIRE(glob.match("/dev/shm/a/b/foo.a0"));

  glob = a0::PathGlob("/dev/shm/**/*.a0");
  REQUIRE(glob.match("/dev/shm/foo.a0"));

  glob = a0::PathGlob("foo.a0");
  REQUIRE(glob.match("/dev/shm/alephzero/foo.a0"));
  REQUIRE(!glob.match("/foo.a0"));
  REQUIRE(glob.match("foo.a0"));

  glob = a0::PathGlob("**/*.a0");
  REQUIRE(glob.match("a/foo.a0"));
  REQUIRE(glob.match("a/b/foo.a0"));
  REQUIRE(glob.match("/dev/shm/alephzero/a/foo.a0"));
  REQUIRE(glob.match("/dev/shm/alephzero/a/b/foo.a0"));
  REQUIRE(!glob.match("/foo/bar/a/foo.a0"));
  REQUIRE(!glob.match("/foo/bar/a/b/foo.a0"));

  {
    a0::test::scope_env change_root("A0_ROOT", "/foo/bar");
    glob = a0::PathGlob("**/*.a0");
    REQUIRE(glob.match("a/foo.a0"));
    REQUIRE(glob.match("a/b/foo.a0"));
    REQUIRE(!glob.match("/dev/shm/alephzero/a/foo.a0"));
    REQUIRE(!glob.match("/dev/shm/alephzero/a/b/foo.a0"));
    REQUIRE(glob.match("/foo/bar/a/foo.a0"));
    REQUIRE(glob.match("/foo/bar/a/b/foo.a0"));
  }
}
