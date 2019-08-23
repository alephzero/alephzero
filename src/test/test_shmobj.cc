#include <a0/shmobj.h>

#include <doctest.h>
#include <errno.h>
#include <math.h>

#include "src/test_util.hh"

static const char* kTestShm = "/test.shm";

struct ShmObjTestFixture {
  ShmObjTestFixture() {
    a0_shmobj_unlink(kTestShm);
  }

  ~ShmObjTestFixture() {
    a0_shmobj_unlink(kTestShm);
  }
};

TEST_CASE_FIXTURE(ShmObjTestFixture, "Test shared memory objects") {
  a0_shmobj_t shmobj;
  REQUIRE(a0_shmobj_open(kTestShm, nullptr, &shmobj) == EINVAL);

  a0_shmobj_options_t shmopt;
  shmopt.size = 16 * 1024 * 1024;
  REQUIRE(a0_shmobj_open(kTestShm, &shmopt, &shmobj) == A0_OK);
  REQUIRE(shmobj.stat.st_size == shmopt.size);

  REQUIRE(a0_shmobj_close(&shmobj) == A0_OK);

  REQUIRE(a0_shmobj_open(kTestShm, nullptr, &shmobj) == A0_OK);
  REQUIRE(shmobj.stat.st_size == shmopt.size);

  REQUIRE(a0_shmobj_close(&shmobj) == A0_OK);

  shmopt.size = 32 * 1024 * 1024;
  REQUIRE(a0_shmobj_open(kTestShm, &shmopt, &shmobj) == A0_OK);
  REQUIRE(shmobj.stat.st_size == shmopt.size);

  REQUIRE(a0_shmobj_close(&shmobj) == A0_OK);

  if (!a0::test::is_valgrind()) {
    shmopt.size = pow(2, 46);
    REQUIRE(a0_shmobj_open(kTestShm, &shmopt, &shmobj) == A0_OK);
    REQUIRE(shmobj.stat.st_size == shmopt.size);

    REQUIRE(a0_shmobj_close(&shmobj) == A0_OK);
  }
}
