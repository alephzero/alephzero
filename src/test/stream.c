#include <a0/stream.h>

#include <cheat.h>
#include <cheats.h>

CHEAT_DECLARE(
  const char* TEST_SHM = "/test.shm";
  a0_shmobj_options_t shmopt;
  a0_shmobj_t shmobj;

  uint32_t construct_cnt;
  uint32_t already_constructed_cnt;

  static void inc_construct_cnt() { construct_cnt++; }
  static void inc_already_constructed_cnt() { already_constructed_cnt++; }
)

CHEAT_SET_UP(
  a0_shmobj_destroy(TEST_SHM);

  shmopt.size = 4096;
  cheat_assert_int(a0_shmobj_create(TEST_SHM, &shmopt), A0_OK);
  cheat_assert_int(a0_shmobj_attach(TEST_SHM, &shmobj), A0_OK);

  construct_cnt = 0;
  already_constructed_cnt = 0;
)

CHEAT_TEAR_DOWN(
  a0_shmobj_detach(&shmobj);
  a0_shmobj_destroy(TEST_SHM);
)

CHEAT_TEST(test_stream_construct,
  cheat_assert_not_uint64(0xA0A0A0A0A0A0A0A0, *(uint64_t*)shmobj.ptr);
  cheat_assert_uint32(0, construct_cnt);
  cheat_assert_uint32(0, already_constructed_cnt);

  a0_stream_options_t opts;
  memset(&opts, 0, sizeof(opts));
  opts.shmobj = &shmobj;

  a0_stream_construct_options_t sco;
  memset(&sco, 0, sizeof(sco));
  sco.protocol_metadata_size = 13;
  sco.on_construct = &inc_construct_cnt;
  sco.on_already_constructed = &inc_already_constructed_cnt;
  opts.construct_opts = &sco;

  a0_stream_t stream;
  cheat_assert_int(a0_stream_init(&stream, &opts), A0_OK);

  cheat_assert_uint64(0xA0A0A0A0A0A0A0A0, *(uint64_t*)shmobj.ptr);
  cheat_assert_uint32(1, construct_cnt);
  cheat_assert_uint32(0, already_constructed_cnt);

  cheat_assert_int(a0_stream_init(&stream, &opts), A0_OK);
  cheat_assert_uint32(1, construct_cnt);
  cheat_assert_uint32(1, already_constructed_cnt);

  cheat_assert_int(a0_stream_init(&stream, &opts), A0_OK);
  cheat_assert_uint32(1, construct_cnt);
  cheat_assert_uint32(2, already_constructed_cnt);

  a0_locked_stream_t locked_stream;
  cheat_assert_int(a0_lock_stream(&locked_stream, &stream), A0_OK);

  a0_buf_t protocol_metadata;
  cheat_assert_int(a0_stream_protocol_metadata(&locked_stream, &protocol_metadata), A0_OK);
  cheat_assert_uint64(13, protocol_metadata.len);
  cheat_assert_uint64(0, (uintptr_t)protocol_metadata.ptr % alignof(max_align_t));
  memcpy(protocol_metadata.ptr, "protocol info", 13);

  {
    char* debugstr;
    size_t debugstr_len;
    _a0_testing_stream_debugstr(&locked_stream, &debugstr, &debugstr_len);
    cheat_assert_string(debugstr, "\n"
                        "=========================\n"
                        "HEADER\n"
                        "-------------------------\n"
                        "-- shmobj_size = 4096\n"
                        "-------------------------\n"
                        "Committed state\n"
                        "-- seq    = [0, 0]\n"
                        "-- head @ = 0\n"
                        "-- tail @ = 0\n"
                        "-------------------------\n"
                        "Working state\n"
                        "-- seq    = [0, 0]\n"
                        "-- head @ = 0\n"
                        "-- tail @ = 0\n"
                        "=========================\n"
                        "PROTOCOL INFO\n"
                        "-------------------------\n"
                        "-- size = 13\n"
                        "-- payload: protocol info\n"
                        "=========================\n"
                        "DATA\n"
                        "=========================\n");
    free(debugstr);
  }

  cheat_assert_int(a0_unlock_stream(&locked_stream), A0_OK);
  cheat_assert_int(a0_stream_close(&stream), A0_OK);
)

CHEAT_TEST(test_stream_alloc_commit,
  a0_stream_options_t opts;
  memset(&opts, 0, sizeof(opts));
  opts.shmobj = &shmobj;

  a0_stream_t stream;
  cheat_assert_int(a0_stream_init(&stream, &opts), A0_OK);

  a0_locked_stream_t locked_stream;
  cheat_assert_int(a0_lock_stream(&locked_stream, &stream), A0_OK);

  bool is_empty;
  cheat_assert_int(a0_stream_is_empty(&locked_stream, &is_empty), A0_OK);
  cheat_assert(is_empty);

  {
    char* debugstr;
    size_t debugstr_len;
    _a0_testing_stream_debugstr(&locked_stream, &debugstr, &debugstr_len);
    cheat_assert_string(debugstr, "\n"
                        "=========================\n"
                        "HEADER\n"
                        "-------------------------\n"
                        "-- shmobj_size = 4096\n"
                        "-------------------------\n"
                        "Committed state\n"
                        "-- seq    = [0, 0]\n"
                        "-- head @ = 0\n"
                        "-- tail @ = 0\n"
                        "-------------------------\n"
                        "Working state\n"
                        "-- seq    = [0, 0]\n"
                        "-- head @ = 0\n"
                        "-- tail @ = 0\n"
                        "=========================\n"
                        "PROTOCOL INFO\n"
                        "-------------------------\n"
                        "-- size = 0\n"
                        "-- payload: \n"
                        "=========================\n"
                        "DATA\n"
                        "=========================\n");
    free(debugstr);
  }

  a0_buf_t first_elem;
  cheat_assert_int(a0_stream_alloc(&locked_stream, 10, &first_elem), A0_OK);
  memcpy(first_elem.ptr, "0123456789", 10);
  cheat_assert_int(a0_stream_commit(&locked_stream), A0_OK);

  a0_buf_t second_elem;
  cheat_assert_int(a0_stream_alloc(&locked_stream, 40, &second_elem), A0_OK);
  memcpy(second_elem.ptr, "0123456789012345678901234567890123456789", 40);

  {
    char* debugstr;
    size_t debugstr_len;
    _a0_testing_stream_debugstr(&locked_stream, &debugstr, &debugstr_len);
    cheat_assert_string(debugstr, "\n"
                        "=========================\n"
                        "HEADER\n"
                        "-------------------------\n"
                        "-- shmobj_size = 4096\n"
                        "-------------------------\n"
                        "Committed state\n"
                        "-- seq    = [1, 1]\n"
                        "-- head @ = 144\n"
                        "-- tail @ = 144\n"
                        "-------------------------\n"
                        "Working state\n"
                        "-- seq    = [1, 2]\n"
                        "-- head @ = 144\n"
                        "-- tail @ = 192\n"
                        "=========================\n"
                        "PROTOCOL INFO\n"
                        "-------------------------\n"
                        "-- size = 0\n"
                        "-- payload: \n"
                        "=========================\n"
                        "DATA\n"
                        "-------------------------\n"
                        "Elem\n"
                        "-- @      = 144\n"
                        "-- seq    = 1\n"
                        "-- next @ = 192\n"
                        "-- size   = 10\n"
                        "-- payload: 0123456789\n"
                        "-------------------------\n"
                        "Elem (not committed)\n"
                        "-- @      = 192\n"
                        "-- seq    = 2\n"
                        "-- next @ = 0\n"
                        "-- size   = 40\n"
                        "-- payload: 01234567890123456789012345678...\n"
                        "=========================\n");
    free(debugstr);
  }

  cheat_assert_int(a0_stream_commit(&locked_stream), A0_OK);

  {
    char* debugstr;
    size_t debugstr_len;
    _a0_testing_stream_debugstr(&locked_stream, &debugstr, &debugstr_len);
    cheat_assert_string(debugstr, "\n"
                        "=========================\n"
                        "HEADER\n"
                        "-------------------------\n"
                        "-- shmobj_size = 4096\n"
                        "-------------------------\n"
                        "Committed state\n"
                        "-- seq    = [1, 2]\n"
                        "-- head @ = 144\n"
                        "-- tail @ = 192\n"
                        "-------------------------\n"
                        "Working state\n"
                        "-- seq    = [1, 2]\n"
                        "-- head @ = 144\n"
                        "-- tail @ = 192\n"
                        "=========================\n"
                        "PROTOCOL INFO\n"
                        "-------------------------\n"
                        "-- size = 0\n"
                        "-- payload: \n"
                        "=========================\n"
                        "DATA\n"
                        "-------------------------\n"
                        "Elem\n"
                        "-- @      = 144\n"
                        "-- seq    = 1\n"
                        "-- next @ = 192\n"
                        "-- size   = 10\n"
                        "-- payload: 0123456789\n"
                        "-------------------------\n"
                        "Elem\n"
                        "-- @      = 192\n"
                        "-- seq    = 2\n"
                        "-- next @ = 0\n"
                        "-- size   = 40\n"
                        "-- payload: 01234567890123456789012345678...\n"
                        "=========================\n");
    free(debugstr);
  }

  cheat_assert_int(a0_unlock_stream(&locked_stream), A0_OK);
  cheat_assert_int(a0_stream_close(&stream), A0_OK);
)

CHEAT_TEST(test_stream_iter,
  // Create stream and close it.
  {
    a0_stream_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.shmobj = &shmobj;

    a0_stream_t stream;
    cheat_assert_int(a0_stream_init(&stream, &opts), A0_OK);

    a0_locked_stream_t locked_stream;
    cheat_assert_int(a0_lock_stream(&locked_stream, &stream), A0_OK);

    a0_buf_t first_elem;
    cheat_assert_int(a0_stream_alloc(&locked_stream, 1, &first_elem), A0_OK);
    memcpy(first_elem.ptr, "A", 1);

    a0_buf_t second_elem;
    cheat_assert_int(a0_stream_alloc(&locked_stream, 2, &second_elem), A0_OK);
    memcpy(second_elem.ptr, "BB", 2);

    a0_buf_t third_elem;
    cheat_assert_int(a0_stream_alloc(&locked_stream, 3, &third_elem), A0_OK);
    memcpy(third_elem.ptr, "CCC", 3);

    cheat_assert_int(a0_stream_commit(&locked_stream), A0_OK);

    cheat_assert_int(a0_unlock_stream(&locked_stream), A0_OK);
    cheat_assert_int(a0_stream_close(&stream), A0_OK);
  }

  a0_stream_options_t opts;
  memset(&opts, 0, sizeof(opts));
  opts.shmobj = &shmobj;

  a0_stream_t stream;
  cheat_assert_int(a0_stream_init(&stream, &opts), A0_OK);

  a0_locked_stream_t locked_stream;
  cheat_assert_int(a0_lock_stream(&locked_stream, &stream), A0_OK);

  bool is_empty;
  cheat_assert_int(a0_stream_is_empty(&locked_stream, &is_empty), A0_OK);
  cheat_assert_not(is_empty);

  cheat_assert_int(a0_stream_jump_head(&locked_stream), A0_OK);

  a0_stream_elem_hdr_t elem_hdr;
  a0_buf_t elem_payload;

  cheat_assert_int(a0_stream_elem(&locked_stream, &elem_hdr, &elem_payload), A0_OK);
  cheat_assert_uint64(1, elem_hdr.seq);
  cheat_assert_string("A", (char*)elem_payload.ptr);

  bool has_next;
  cheat_assert_int(a0_stream_has_next(&locked_stream, &has_next), A0_OK);
  cheat_assert(has_next);

  cheat_assert_int(a0_stream_next(&locked_stream), A0_OK);
  cheat_assert_int(a0_stream_elem(&locked_stream, &elem_hdr, &elem_payload), A0_OK);
  cheat_assert_uint64(2, elem_hdr.seq);
  cheat_assert_string("BB", (char*)elem_payload.ptr);

  cheat_assert_int(a0_stream_has_next(&locked_stream, &has_next), A0_OK);
  cheat_assert(has_next);

  cheat_assert_int(a0_stream_next(&locked_stream), A0_OK);
  cheat_assert_int(a0_stream_elem(&locked_stream, &elem_hdr, &elem_payload), A0_OK);
  cheat_assert_uint64(3, elem_hdr.seq);
  cheat_assert_string("CCC", (char*)elem_payload.ptr);

  cheat_assert_int(a0_stream_has_next(&locked_stream, &has_next), A0_OK);
  cheat_assert_not(has_next);

  cheat_assert_int(a0_stream_jump_head(&locked_stream), A0_OK);
  cheat_assert_int(a0_stream_elem(&locked_stream, &elem_hdr, &elem_payload), A0_OK);
  cheat_assert_uint64(1, elem_hdr.seq);
  cheat_assert_string("A", (char*)elem_payload.ptr);

  cheat_assert_int(a0_stream_jump_tail(&locked_stream), A0_OK);
  cheat_assert_int(a0_stream_elem(&locked_stream, &elem_hdr, &elem_payload), A0_OK);
  cheat_assert_uint64(3, elem_hdr.seq);
  cheat_assert_string("CCC", (char*)elem_payload.ptr);

  cheat_assert_int(a0_unlock_stream(&locked_stream), A0_OK);
  cheat_assert_int(a0_stream_close(&stream), A0_OK);
)
