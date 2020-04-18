#include <a0/shm.h>

#include <doctest.h>
#include <errno.h>
#include <math.h>

#include "src/test_util.hpp"

static const char* TEST_SHM = "/test.shm";

struct ShmTestFixture {
  ShmTestFixture() {
    a0_shm_unlink(TEST_SHM);
  }

  ~ShmTestFixture() {
    a0_shm_unlink(TEST_SHM);
  }
};

TEST_CASE_FIXTURE(ShmTestFixture, "shm] basic") {
  a0_shm_t shm;
  REQUIRE(a0_shm_open(TEST_SHM, nullptr, &shm) == EINVAL);

  a0_shm_options_t shmopt;
  shmopt.size = 16 * 1024 * 1024;
  REQUIRE_OK(a0_shm_open(TEST_SHM, &shmopt, &shm));
  REQUIRE(!strcmp(shm.path, TEST_SHM));
  REQUIRE(shm.buf.size == shmopt.size);

  REQUIRE_OK(a0_shm_close(&shm));

  REQUIRE_OK(a0_shm_open(TEST_SHM, nullptr, &shm));
  REQUIRE(shm.buf.size == shmopt.size);

  REQUIRE_OK(a0_shm_close(&shm));

  shmopt.size = 32 * 1024 * 1024;
  REQUIRE_OK(a0_shm_open(TEST_SHM, &shmopt, &shm));
  REQUIRE(shm.buf.size == shmopt.size);

  REQUIRE_OK(a0_shm_close(&shm));

  if (!a0::test::is_valgrind()) {
    shmopt.size = pow(2, 46);
    REQUIRE_OK(a0_shm_open(TEST_SHM, &shmopt, &shm));
    REQUIRE(shm.buf.size == shmopt.size);

    REQUIRE_OK(a0_shm_close(&shm));
  }
}

TEST_CASE_FIXTURE(ShmTestFixture, "shm] bad path") {
  a0_shm_options_t shmopt;
  shmopt.size = 16 * 1024 * 1024;

  a0_shm_t shm;
  REQUIRE(a0_shm_open("/foo/bar", &shmopt, &shm) == EINVAL);
}

TEST_CASE_FIXTURE(ShmTestFixture, "shm] double close") {
  a0_shm_options_t shmopt;
  shmopt.size = 16 * 1024 * 1024;

  a0_shm_t shm;
  REQUIRE_OK(a0_shm_open(TEST_SHM, &shmopt, &shm));
  REQUIRE_OK(a0_shm_close(&shm));
  REQUIRE(a0_shm_close(&shm) == EBADF);
}
