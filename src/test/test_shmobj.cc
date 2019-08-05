#include <a0/shmobj.h>

#include <doctest.h>

#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char* TEST_SHM = "/test.shm";

struct ShmObjTestFixture {
  ShmObjTestFixture() {
    a0_shmobj_unlink(TEST_SHM);
  }

  ~ShmObjTestFixture() {
    a0_shmobj_unlink(TEST_SHM);
  }
};

TEST_CASE_FIXTURE(ShmObjTestFixture, "Test shared memory objects") {
  a0_shmobj_t shmobj;
  REQUIRE(a0_shmobj_open(TEST_SHM, nullptr, &shmobj) == EINVAL);

  a0_shmobj_options_t shmopt;
  shmopt.size = 16 * 1024 * 1024;
  REQUIRE(a0_shmobj_open(TEST_SHM, &shmopt, &shmobj) == A0_OK);
  REQUIRE(shmobj.stat.st_size == shmopt.size);

  a0_shmobj_close(&shmobj);

  REQUIRE(a0_shmobj_open(TEST_SHM, nullptr, &shmobj) == A0_OK);
  REQUIRE(shmobj.stat.st_size == shmopt.size);

  a0_shmobj_close(&shmobj);

  shmopt.size = 32 * 1024 * 1024;
  REQUIRE(a0_shmobj_open(TEST_SHM, &shmopt, &shmobj) == A0_OK);
  REQUIRE(shmobj.stat.st_size == shmopt.size);

  a0_shmobj_close(&shmobj);

  shmopt.size = pow(2, 46);
  REQUIRE(a0_shmobj_open(TEST_SHM, &shmopt, &shmobj) == A0_OK);
  REQUIRE(shmobj.stat.st_size == shmopt.size);

  a0_shmobj_close(&shmobj);
}
