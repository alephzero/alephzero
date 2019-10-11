#include <a0/shm.h>

#include <doctest.h>
#include <errno.h>
#include <math.h>

#include "src/test_util.hpp"

static const char* kTestShm = "/test.shm";

struct ShmObjTestFixture {
  ShmObjTestFixture() {
    a0_shm_unlink(kTestShm);
  }

  ~ShmObjTestFixture() {
    a0_shm_unlink(kTestShm);
  }
};

TEST_CASE_FIXTURE(ShmObjTestFixture, "Test shared memory objects") {
  a0_shm_t shm;
  REQUIRE(a0_shm_open(kTestShm, nullptr, &shm) == EINVAL);

  a0_shm_options_t shmopt;
  shmopt.size = 16 * 1024 * 1024;
  REQUIRE(a0_shm_open(kTestShm, &shmopt, &shm) == A0_OK);
  REQUIRE(!strcmp(shm.path, kTestShm));
  REQUIRE(shm.buf.size == shmopt.size);

  REQUIRE(a0_shm_close(&shm) == A0_OK);

  REQUIRE(a0_shm_open(kTestShm, nullptr, &shm) == A0_OK);
  REQUIRE(shm.buf.size == shmopt.size);

  REQUIRE(a0_shm_close(&shm) == A0_OK);

  shmopt.size = 32 * 1024 * 1024;
  REQUIRE(a0_shm_open(kTestShm, &shmopt, &shm) == A0_OK);
  REQUIRE(shm.buf.size == shmopt.size);

  REQUIRE(a0_shm_close(&shm) == A0_OK);

  if (!a0::test::is_valgrind()) {
    shmopt.size = pow(2, 46);
    REQUIRE(a0_shm_open(kTestShm, &shmopt, &shm) == A0_OK);
    REQUIRE(shm.buf.size == shmopt.size);

    REQUIRE(a0_shm_close(&shm) == A0_OK);
  }
}

TEST_CASE_FIXTURE(ShmObjTestFixture, "Test shared memory bad path") {
  a0_shm_options_t shmopt;
  shmopt.size = 16 * 1024 * 1024;

  a0_shm_t shm;
  REQUIRE(a0_shm_open("/foo/bar", &shmopt, &shm) == EINVAL);
}