#include <a0/packet.h>
#include <a0/transport.h>
#include <a0/reader.h>

#include <doctest.h>
#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "src/test_util.hpp"
#include "src/transport_tools.hpp"


struct ReaderBaseFixture {
  std::vector<uint8_t> arena_data;
  a0_arena_t arena;

  ReaderBaseFixture() {
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
};


struct ReaderSyncZCFixture : ReaderBaseFixture {
  a0_reader_sync_zc_t rsz;

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

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] next without has_next") {
  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEWEST));

  struct data_t {
    bool executed;
  } data{false};

  a0_zero_copy_callback_t cb = {
      .user_data = &data,
      .fn = [](void* user_data, a0_locked_transport_t, a0_flat_packet_t) {
        ((data_t*)user_data)->executed = true;
      },
  };

  REQUIRE(a0_reader_sync_zc_next(&rsz, cb) == EAGAIN);
  REQUIRE(!data.executed);

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}


struct ReaderSyncFixture : ReaderBaseFixture {
  a0_reader_sync_t rs;

  bool has_next() {
    bool has_next_;
    REQUIRE_OK(a0_reader_sync_has_next(&rs, &has_next_));
    return has_next_;
  }

  void REQUIRE_NEXT(std::string want_payload) {
    a0_packet_t pkt;
    REQUIRE_OK(a0_reader_sync_next(&rs, &pkt));
    REQUIRE(a0::test::str(pkt.payload) == want_payload);
  };
};

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] oldest-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_OLDEST, A0_ITER_NEXT));
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_0");
  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] oldest-next, empty start") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_OLDEST, A0_ITER_NEXT));
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

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] most recent-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_MOST_RECENT, A0_ITER_NEXT));

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_2");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] most recent-next, empty start") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_MOST_RECENT, A0_ITER_NEXT));
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

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] most recent-newest") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_MOST_RECENT, A0_ITER_NEWEST));

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_1");
  REQUIRE(!has_next());

  push_pkt("pkt_2");
  push_pkt("pkt_3");

  REQUIRE(has_next());
  REQUIRE_NEXT("pkt_3");
  REQUIRE(!has_next());

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] await new-next") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEXT));

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

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] await new-next, empty start") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEXT));
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

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] await new-newest") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEWEST));

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

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] await new-newest, empty start") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEWEST));

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

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] next without has_next") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEWEST));

  a0_packet_t pkt;
  REQUIRE(a0_reader_sync_next(&rs, &pkt) == EAGAIN);

  REQUIRE_OK(a0_reader_sync_close(&rs));
}


struct ReaderReadOneFixture : ReaderBaseFixture {};

TEST_CASE_FIXTURE(ReaderReadOneFixture, "reader_read_one] non-blocking oldest") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  a0_packet_t pkt;
  REQUIRE_OK(a0_reader_read_one(arena, a0::test::alloc(), A0_INIT_OLDEST, O_NONBLOCK, &pkt));

  REQUIRE(a0::test::str(pkt.payload) == "pkt_0");
}

TEST_CASE_FIXTURE(ReaderReadOneFixture, "reader_read_one] non-blocking oldest, empty") {
  a0_packet_t pkt;
  REQUIRE(a0_reader_read_one(arena, a0::test::alloc(), A0_INIT_OLDEST, O_NONBLOCK, &pkt) == EAGAIN);
}

TEST_CASE_FIXTURE(ReaderReadOneFixture, "reader_read_one] non-blocking recent") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  a0_packet_t pkt;
  REQUIRE_OK(a0_reader_read_one(arena, a0::test::alloc(), A0_INIT_MOST_RECENT, O_NONBLOCK, &pkt));

  REQUIRE(a0::test::str(pkt.payload) == "pkt_1");
}

TEST_CASE_FIXTURE(ReaderReadOneFixture, "reader_read_one] non-blocking recent, empty") {
  a0_packet_t pkt;
  REQUIRE(a0_reader_read_one(arena, a0::test::alloc(), A0_INIT_MOST_RECENT, O_NONBLOCK, &pkt) == EAGAIN);
}

TEST_CASE_FIXTURE(ReaderReadOneFixture, "reader_read_one] non-blocking new") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  a0_packet_t pkt;
  REQUIRE(a0_reader_read_one(arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, O_NONBLOCK, &pkt) == EAGAIN);
}

TEST_CASE_FIXTURE(ReaderReadOneFixture, "reader_read_one] non-blocking new, empty") {
  a0_packet_t pkt;
  REQUIRE(a0_reader_read_one(arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, O_NONBLOCK, &pkt) == EAGAIN);
}
