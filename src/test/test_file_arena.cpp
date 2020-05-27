#include <a0/common.h>
#include <a0/file_arena.h>

#include <doctest.h>
#include <sys/types.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>

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

  REQUIRE_OK(a0_shm_open(TEST_SHM, nullptr, &shm));
  REQUIRE(!strcmp(shm.path, TEST_SHM));
  REQUIRE(shm.arena.size == A0_SHM_OPTIONS_DEFAULT.size);
  REQUIRE_OK(a0_shm_close(&shm));

  a0_shm_options_t shmopt = {
      .size = 32 * 1024 * 1024,
      .resize = false,
  };
  REQUIRE_OK(a0_shm_open(TEST_SHM, &shmopt, &shm));
  REQUIRE(shm.arena.size == A0_SHM_OPTIONS_DEFAULT.size);
  REQUIRE_OK(a0_shm_close(&shm));

  shmopt.resize = true;
  REQUIRE_OK(a0_shm_open(TEST_SHM, &shmopt, &shm));
  REQUIRE(shm.arena.size == shmopt.size);
  REQUIRE_OK(a0_shm_close(&shm));

  REQUIRE_OK(a0_shm_open(TEST_SHM, nullptr, &shm));
  REQUIRE(shm.arena.size == shmopt.size);
  REQUIRE_OK(a0_shm_close(&shm));

  if (!a0::test::is_valgrind()) {
    shmopt.size = pow(2, 46);
    REQUIRE_OK(a0_shm_open(TEST_SHM, &shmopt, &shm));
    REQUIRE(shm.arena.size == shmopt.size);

    REQUIRE_OK(a0_shm_close(&shm));
  }
}

TEST_CASE_FIXTURE(ShmTestFixture, "shm] bad size") {
  a0_shm_t shm;
  a0_shm_options_t shmopt = {
      .size = std::numeric_limits<off_t>::max(),
      .resize = false,
  };
  errno_t err = a0_shm_open("/foo", &shmopt, &shm);
  REQUIRE((err == ENOMEM || err == EINVAL || err == EFBIG));

  shmopt.size = -1;
  REQUIRE(a0_shm_open("/bar", &shmopt, &shm) == EINVAL);
}

TEST_CASE_FIXTURE(ShmTestFixture, "shm] bad path") {
  a0_shm_t shm;
  REQUIRE(a0_shm_open("/foo/bar", nullptr, &shm) == EINVAL);
}

TEST_CASE_FIXTURE(ShmTestFixture, "shm] double close") {
  a0_shm_t shm;
  REQUIRE_OK(a0_shm_open(TEST_SHM, nullptr, &shm));
  REQUIRE_OK(a0_shm_close(&shm));
  REQUIRE(a0_shm_close(&shm) == EBADF);
}

static const char* TEST_DISK = "/tmp/test.disk";

struct DiskTestFixture {
  DiskTestFixture() {
    a0_disk_unlink(TEST_DISK);
  }

  ~DiskTestFixture() {
    a0_disk_unlink(TEST_DISK);
  }
};

TEST_CASE_FIXTURE(DiskTestFixture, "disk] basic") {
  a0_disk_t disk;

  REQUIRE_OK(a0_disk_open(TEST_DISK, nullptr, &disk));
  REQUIRE(!strcmp(disk.path, TEST_DISK));
  REQUIRE(disk.arena.size == A0_DISK_OPTIONS_DEFAULT.size);
  REQUIRE_OK(a0_disk_close(&disk));

  a0_disk_options_t diskopt = {
      .size = 32 * 1024 * 1024,
      .resize = false,
  };
  REQUIRE_OK(a0_disk_open(TEST_DISK, &diskopt, &disk));
  REQUIRE(disk.arena.size == A0_DISK_OPTIONS_DEFAULT.size);
  REQUIRE_OK(a0_disk_close(&disk));

  diskopt.resize = true;
  REQUIRE_OK(a0_disk_open(TEST_DISK, &diskopt, &disk));
  REQUIRE(disk.arena.size == diskopt.size);
  REQUIRE_OK(a0_disk_close(&disk));

  REQUIRE_OK(a0_disk_open(TEST_DISK, nullptr, &disk));
  REQUIRE(disk.arena.size == diskopt.size);
  REQUIRE_OK(a0_disk_close(&disk));
}

TEST_CASE_FIXTURE(DiskTestFixture, "disk] bad size") {
  a0_disk_t disk;
  a0_disk_options_t diskopt = {
      .size = std::numeric_limits<off_t>::max(),
      .resize = false,
  };
  errno_t err = a0_disk_open("/tmp/foo.disk", &diskopt, &disk);
  REQUIRE((err == ENOMEM || err == EINVAL || err == EFBIG));

  diskopt.size = -1;
  REQUIRE(a0_disk_open("/tmp/bar.disk", &diskopt, &disk) == EINVAL);
}

TEST_CASE_FIXTURE(DiskTestFixture, "disk] bad path") {
  a0_disk_t disk;
  REQUIRE(a0_disk_open("////foo/bar", nullptr, &disk) == ENOENT);
}

TEST_CASE_FIXTURE(DiskTestFixture, "disk] double close") {
  a0_disk_t disk;
  REQUIRE_OK(a0_disk_open(TEST_DISK, nullptr, &disk));
  REQUIRE_OK(a0_disk_close(&disk));
  REQUIRE(a0_disk_close(&disk) == EBADF);
}
