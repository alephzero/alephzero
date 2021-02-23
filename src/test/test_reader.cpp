#include <a0/packet.h>
#include <a0/transport.h>
#include <a0/reader.h>

#include <doctest.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "src/test_util.hpp"
#include "src/transport_tools.hpp"


struct ReaderSyncZCFixture {
  std::vector<uint8_t> arena_data;
  a0_arena_t arena;
  a0_reader_sync_zc_t rsz;

  ReaderSyncZCFixture() {
    arena_data.resize(4096);
    arena.buf.ptr = arena_data.data();
    arena.buf.size = arena_data.size();
    arena.mode = A0_ARENA_MODE_SHARED;
  }

  void push_pkt(std::string payload) {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, arena));

    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_lock(&transport, &lk));

    a0_alloc_t alloc;
    a0_transport_allocator(&lk, &alloc);
    a0_packet_serialize(a0::test::pkt(std::move(payload)), alloc, NULL);
    a0_transport_commit(lk);

    REQUIRE_OK(a0_transport_unlock(lk));
  }

  bool has_next() {
    bool has_next_;
    REQUIRE_OK(a0_reader_sync_zc_has_next(&rsz, &has_next_));
    return has_next_;
  }

  void REQUIRE_NEXT(std::string want_payload) {
    struct data_t {
      a0_packet_t pkt;
      bool executed;
    } data{a0::test::pkt(std::move(want_payload)), false};

    a0_zero_copy_callback_t cb = {
        .user_data = &data,
        .fn = [](void* user_data, a0_locked_transport_t, a0_flat_packet_t fpkt) {
          auto* want = (data_t*)user_data;
          REQUIRE(a0::test::pkt_cmp(want->pkt, a0::test::pkt(fpkt)).content_match);
          want->executed = true;
        },
    };

    REQUIRE_OK(a0_reader_sync_zc_next(&rsz, cb));
    REQUIRE(data.executed);
  };
};

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] oldest-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_OLDEST, A0_ITER_NEXT));
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_0");
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] oldest-next, empty start") {
  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_OLDEST, A0_ITER_NEXT));
  REQUIRE(!has_next());

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_0");
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] most recent-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_MOST_RECENT, A0_ITER_NEXT));

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] most recent-next, empty start") {
  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_MOST_RECENT, A0_ITER_NEXT));
  REQUIRE(!has_next());

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_0");
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] most recent-newest") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_MOST_RECENT, A0_ITER_NEWEST));

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");
  push_pkt("pkt_3");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_3");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] await new-next") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEXT));

  REQUIRE(!has_next());

  push_pkt("pkt_1");
  push_pkt("pkt_2");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(!has_next());

  push_pkt("pkt_3");
  push_pkt("pkt_4");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_3");
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_4");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] await new-next, empty start") {
  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEXT));
  REQUIRE(!has_next());

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_0");
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");
  push_pkt("pkt_3");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_3");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] await new-newest") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEWEST));

  REQUIRE(!has_next());

  push_pkt("pkt_1");
  push_pkt("pkt_2");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(!has_next());

  push_pkt("pkt_3");
  push_pkt("pkt_4");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_4");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] await new-newest, empty start") {
  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEWEST));

  REQUIRE(!has_next());

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");
  push_pkt("pkt_3");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_3");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}
