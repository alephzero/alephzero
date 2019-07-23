#include <a0/shmobj.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cheat.h>
#include <cheats.h>

CHEAT_DECLARE(
  const char* TEST_SHM = "/test.shm";
)

CHEAT_SET_UP(
  a0_shmobj_destroy(TEST_SHM);
)

CHEAT_TEAR_DOWN(
  a0_shmobj_destroy(TEST_SHM);
)

CHEAT_TEST(test_shmobj,
  bool exists = false;
  cheat_assert_int(a0_shmobj_exists(TEST_SHM, &exists), A0_OK);
  cheat_assert_not(exists);

  a0_shmobj_t shmobj;
  memset(&shmobj, 0, sizeof(shmobj));
  cheat_assert_int(a0_shmobj_attach(TEST_SHM, &shmobj), ENOENT);

  a0_shmobj_options_t shmopt;
  memset(&shmopt, 0, sizeof(shmopt));
  shmopt.size = pow(2, 60);
  cheat_assert_int(a0_shmobj_create(TEST_SHM, &shmopt), A0_OK);
  cheat_assert_int(a0_shmobj_exists(TEST_SHM, &exists), A0_OK);
  cheat_assert(exists);

  cheat_assert_int(a0_shmobj_attach(TEST_SHM, &shmobj), A0_OK);
  cheat_assert_double(shmobj.stat.st_size, shmopt.size, 0);

  cheat_assert_int(a0_shmobj_detach(&shmobj), A0_OK);
  cheat_assert_int(a0_shmobj_exists(TEST_SHM, &exists), A0_OK);
  cheat_assert(exists);

  cheat_assert_int(a0_shmobj_destroy(TEST_SHM), A0_OK);
  cheat_assert_int(a0_shmobj_exists(TEST_SHM, &exists), A0_OK);
  cheat_assert_not(exists);
)
