#include <a0/alloc.h>
#include <a0/arena.h>
#include <a0/arena.hpp>
#include <a0/buf.h>
#include <a0/err.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/reader.h>
#include <a0/reader.hpp>
#include <a0/string_view.hpp>
#include <a0/transport.h>
#include <a0/transport.hpp>

#include <doctest.h>
#include <fcntl.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/c_wrap.hpp"
#include "src/test_util.hpp"

struct ReaderBaseFixture {
  std::vector<uint8_t> arena_data;
  a0_arena_t arena;
  std::vector<std::thread> threads;

  ReaderBaseFixture() {
    arena_data.resize(4096);
    arena.buf = {arena_data.data(), arena_data.size()};
    arena.mode = A0_ARENA_MODE_SHARED;
  }

  void push_pkt(std::string payload) {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, arena));

    a0_transport_locked_t lk;
    REQUIRE_OK(a0_transport_lock(&transport, &lk));

    a0_alloc_t alloc;
    a0_transport_allocator(&lk, &alloc);
    a0_packet_serialize(a0::test::pkt(std::move(payload)), alloc, NULL);
    a0_transport_commit(lk);

    REQUIRE_OK(a0_transport_unlock(lk));
  }

  void thread_sleep_push_pkt(std::string payload) {
    threads.emplace_back([this, payload]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      push_pkt(payload);
    });
  }

  void join_threads() {
    for (auto&& t : threads) {
      t.join();
    }
    threads.clear();
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
        .fn = [](void* user_data, a0_transport_locked_t, a0_flat_packet_t fpkt) {
          auto* want = (data_t*)user_data;
          REQUIRE(a0::test::pkt_cmp(want->pkt, a0::test::unflatten(fpkt)).content_match);
          want->executed = true;
        },
    };

    REQUIRE_OK(a0_reader_sync_zc_next(&rsz, cb));
    REQUIRE(data.executed);
  };

  void REQUIRE_NEXT_BLOCKING(std::string want_payload) {
    struct data_t {
      a0_packet_t pkt;
      bool executed;
    } data{a0::test::pkt(std::move(want_payload)), false};

    a0_zero_copy_callback_t cb = {
        .user_data = &data,
        .fn = [](void* user_data, a0_transport_locked_t, a0_flat_packet_t fpkt) {
          auto* want = (data_t*)user_data;
          REQUIRE(a0::test::pkt_cmp(want->pkt, a0::test::unflatten(fpkt)).content_match);
          want->executed = true;
        },
    };

    REQUIRE_OK(a0_reader_sync_zc_next_blocking(&rsz, cb));
    REQUIRE(data.executed);
  };

  void REQUIRE_NEXT_CPP(a0::ReaderSyncZeroCopy cpp_rsz, std::string want_payload) {
    bool executed = false;
    cpp_rsz.next([&](a0::TransportLocked, a0::FlatPacket fpkt) {
      REQUIRE(fpkt.payload() == want_payload);
      executed = true;
    });
    REQUIRE(executed);
  }

  void REQUIRE_NEXT_BLOCKING_CPP(a0::ReaderSyncZeroCopy cpp_rsz, std::string want_payload) {
    bool executed = false;
    cpp_rsz.next_blocking([&](a0::TransportLocked, a0::FlatPacket fpkt) {
      REQUIRE(fpkt.payload() == want_payload);
      executed = true;
    });
    REQUIRE(executed);
  }
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

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] cpp oldest-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  a0::ReaderSyncZeroCopy cpp_rsz(a0::cpp_wrap<a0::Arena>(arena), A0_INIT_OLDEST, A0_ITER_NEXT);
  REQUIRE(cpp_rsz.has_next());
  REQUIRE_NEXT_CPP(cpp_rsz, "pkt_0");
  REQUIRE(cpp_rsz.has_next());
  REQUIRE_NEXT_CPP(cpp_rsz, "pkt_1");
  REQUIRE(!cpp_rsz.has_next());

  push_pkt("pkt_2");

  REQUIRE(cpp_rsz.has_next());
  REQUIRE_NEXT_CPP(cpp_rsz, "pkt_2");
  REQUIRE(!cpp_rsz.has_next());
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
      .fn = [](void* user_data, a0_transport_locked_t, a0_flat_packet_t) {
        ((data_t*)user_data)->executed = true;
      },
  };

  REQUIRE(a0_reader_sync_zc_next(&rsz, cb) == A0_ERR_AGAIN);
  REQUIRE(!data.executed);

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] blocking oldest available") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_OLDEST, A0_ITER_NEXT));

  REQUIRE_NEXT_BLOCKING("pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] cpp blocking oldest available") {
  push_pkt("pkt_0");

  a0::ReaderSyncZeroCopy cpp_rsz(a0::cpp_wrap<a0::Arena>(arena), A0_INIT_OLDEST, A0_ITER_NEXT);

  REQUIRE_NEXT_BLOCKING_CPP(cpp_rsz, "pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING_CPP(cpp_rsz, "pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING_CPP(cpp_rsz, "pkt_2");

  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] blocking oldest not available") {
  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_OLDEST, A0_ITER_NEXT));

  thread_sleep_push_pkt("pkt_0");
  REQUIRE_NEXT_BLOCKING("pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] blocking recent available") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_MOST_RECENT, A0_ITER_NEXT));

  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  thread_sleep_push_pkt("pkt_3");
  REQUIRE_NEXT_BLOCKING("pkt_3");

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] blocking recent not available") {
  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_MOST_RECENT, A0_ITER_NEXT));

  thread_sleep_push_pkt("pkt_0");
  REQUIRE_NEXT_BLOCKING("pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] blocking new available") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEXT));

  push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  thread_sleep_push_pkt("pkt_3");
  REQUIRE_NEXT_BLOCKING("pkt_3");

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncZCFixture, "reader_sync_zc] blocking new not available") {
  REQUIRE_OK(a0_reader_sync_zc_init(&rsz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEXT));

  thread_sleep_push_pkt("pkt_0");
  REQUIRE_NEXT_BLOCKING("pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  REQUIRE_OK(a0_reader_sync_zc_close(&rsz));
  join_threads();
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

  void REQUIRE_NEXT_BLOCKING(std::string want_payload) {
    a0_packet_t pkt;
    REQUIRE_OK(a0_reader_sync_next_blocking(&rs, &pkt));
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

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] cpp oldest-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  a0::ReaderSync cpp_rs(a0::cpp_wrap<a0::Arena>(arena), A0_INIT_OLDEST, A0_ITER_NEXT);
  REQUIRE(cpp_rs.has_next());
  REQUIRE(cpp_rs.next().payload() == "pkt_0");
  REQUIRE(cpp_rs.has_next());
  REQUIRE(cpp_rs.next().payload() == "pkt_1");
  REQUIRE(!cpp_rs.has_next());

  push_pkt("pkt_2");

  REQUIRE(cpp_rs.has_next());
  REQUIRE(cpp_rs.next().payload() == "pkt_2");
  REQUIRE(!cpp_rs.has_next());

  REQUIRE_THROWS_WITH(
      cpp_rs.next(),
      "Not available yet");
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
  REQUIRE(a0_reader_sync_next(&rs, &pkt) == A0_ERR_AGAIN);

  REQUIRE_OK(a0_reader_sync_close(&rs));
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] blocking oldest-next") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_OLDEST, A0_ITER_NEXT));

  REQUIRE_NEXT_BLOCKING("pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  REQUIRE_OK(a0_reader_sync_close(&rs));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] cpp blocking oldest-next") {
  push_pkt("pkt_0");

  a0::ReaderSync cpp_rs(a0::cpp_wrap<a0::Arena>(arena), A0_INIT_OLDEST, A0_ITER_NEXT);
  REQUIRE(cpp_rs.next_blocking().payload() == "pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE(cpp_rs.next_blocking().payload() == "pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE(cpp_rs.next_blocking().payload() == "pkt_2");

  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] blocking oldest-next, empty start") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_OLDEST, A0_ITER_NEXT));

  thread_sleep_push_pkt("pkt_0");
  REQUIRE_NEXT_BLOCKING("pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  REQUIRE_OK(a0_reader_sync_close(&rs));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] cpp blocking oldest-next, empty start") {
  a0::ReaderSync cpp_rs(a0::cpp_wrap<a0::Arena>(arena), A0_INIT_OLDEST, A0_ITER_NEXT);

  thread_sleep_push_pkt("pkt_0");
  REQUIRE(cpp_rs.next_blocking().payload() == "pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE(cpp_rs.next_blocking().payload() == "pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE(cpp_rs.next_blocking().payload() == "pkt_2");

  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] blocking recent-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_MOST_RECENT, A0_ITER_NEXT));

  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  thread_sleep_push_pkt("pkt_3");
  REQUIRE_NEXT_BLOCKING("pkt_3");

  REQUIRE_OK(a0_reader_sync_close(&rs));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] blocking recent-next, empty start") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_MOST_RECENT, A0_ITER_NEXT));

  thread_sleep_push_pkt("pkt_0");
  REQUIRE_NEXT_BLOCKING("pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  REQUIRE_OK(a0_reader_sync_close(&rs));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] blocking new-next") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEXT));

  push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  thread_sleep_push_pkt("pkt_3");
  REQUIRE_NEXT_BLOCKING("pkt_3");

  REQUIRE_OK(a0_reader_sync_close(&rs));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] blocking new-next, empty start") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEXT));

  thread_sleep_push_pkt("pkt_0");
  REQUIRE_NEXT_BLOCKING("pkt_0");

  thread_sleep_push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  thread_sleep_push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  REQUIRE_OK(a0_reader_sync_close(&rs));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] blocking new-newest") {
  push_pkt("pkt_0");

  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEWEST));

  push_pkt("pkt_1");
  REQUIRE_NEXT_BLOCKING("pkt_1");

  push_pkt("pkt_2");
  push_pkt("pkt_3");
  REQUIRE_NEXT_BLOCKING("pkt_3");

  thread_sleep_push_pkt("pkt_4");
  REQUIRE_NEXT_BLOCKING("pkt_4");

  REQUIRE_OK(a0_reader_sync_close(&rs));
  join_threads();
}

TEST_CASE_FIXTURE(ReaderSyncFixture, "reader_sync] blocking new-newest, empty start") {
  REQUIRE_OK(a0_reader_sync_init(&rs, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEWEST));

  thread_sleep_push_pkt("pkt_0");
  REQUIRE_NEXT_BLOCKING("pkt_0");

  push_pkt("pkt_1");
  push_pkt("pkt_2");
  REQUIRE_NEXT_BLOCKING("pkt_2");

  thread_sleep_push_pkt("pkt_3");
  REQUIRE_NEXT_BLOCKING("pkt_3");

  REQUIRE_OK(a0_reader_sync_close(&rs));
  join_threads();
}

struct ReaderZCFixture : ReaderBaseFixture {
  a0_reader_zc_t rz;

  struct data_t {
    std::vector<std::string> collected_payloads;
    std::mutex mu;
    std::condition_variable cv;
  } data;

  a0_zero_copy_callback_t make_callback() {
    return a0_zero_copy_callback_t{
        .user_data = &data,
        .fn = [](void* user_data, a0_transport_locked_t, a0_flat_packet_t fpkt) {
          auto* data = (data_t*)user_data;
          a0_buf_t payload;
          a0_flat_packet_payload(fpkt, &payload);

          std::unique_lock<std::mutex> lk{data->mu};
          data->collected_payloads.push_back(a0::test::str(payload));
          data->cv.notify_all();
        },
    };
  }

  std::function<void(a0::TransportLocked, a0::FlatPacket)> make_cpp_callback() {
    return [&](a0::TransportLocked, a0::FlatPacket fpkt) {
      std::unique_lock<std::mutex> lk{data.mu};
      data.collected_payloads.push_back(std::string(fpkt.payload()));
      data.cv.notify_all();
    };
  }

  void WAIT_AND_REQUIRE_PAYLOADS(std::vector<std::string> want_payloads) {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.collected_payloads.size() >= want_payloads.size();
    });
    REQUIRE(data.collected_payloads == want_payloads);
  }
};

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] close no packet") {
  REQUIRE_OK(a0_reader_zc_init(&rz, arena, A0_INIT_OLDEST, A0_ITER_NEXT, make_callback()));
  REQUIRE_OK(a0_reader_zc_close(&rz));
}

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] oldest-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_zc_init(&rz, arena, A0_INIT_OLDEST, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_zc_close(&rz));
}

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] cpp oldest-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  a0::ReaderZeroCopy cpp_rz(
      a0::cpp_wrap<a0::Arena>(arena),
      A0_INIT_OLDEST,
      A0_ITER_NEXT,
      make_cpp_callback());

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});
}

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] oldest-next, empty start") {
  REQUIRE_OK(a0_reader_zc_init(&rz, arena, A0_INIT_OLDEST, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1"});

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_zc_close(&rz));
}

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] most recent-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_zc_init(&rz, arena, A0_INIT_MOST_RECENT, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_zc_close(&rz));
}

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] most recent-next, empty start") {
  REQUIRE_OK(a0_reader_zc_init(&rz, arena, A0_INIT_MOST_RECENT, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1"});

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_zc_close(&rz));
}

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] await new-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_zc_init(&rz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_2"});

  push_pkt("pkt_3");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_2", "pkt_3"});

  REQUIRE_OK(a0_reader_zc_close(&rz));
}

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] await new-next, empty start") {
  REQUIRE_OK(a0_reader_zc_init(&rz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1"});

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_zc_close(&rz));
}

TEST_CASE_FIXTURE(ReaderZCFixture, "reader_zc] await new-newest") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_zc_init(&rz, arena, A0_INIT_AWAIT_NEW, A0_ITER_NEWEST, make_callback()));

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_2"});

  {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, arena));

    a0_transport_locked_t lk;
    REQUIRE_OK(a0_transport_lock(&transport, &lk));

    a0_alloc_t alloc;
    a0_transport_allocator(&lk, &alloc);
    a0_packet_serialize(a0::test::pkt("pkt_3"), alloc, NULL);
    a0_transport_commit(lk);

    a0_transport_allocator(&lk, &alloc);
    a0_packet_serialize(a0::test::pkt("pkt_4"), alloc, NULL);
    a0_transport_commit(lk);

    REQUIRE_OK(a0_transport_unlock(lk));
  }

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_2", "pkt_4"});

  REQUIRE_OK(a0_reader_zc_close(&rz));
}

struct ReaderFixture : ReaderBaseFixture {
  a0_reader_t r;

  struct data_t {
    std::vector<std::string> collected_payloads;
    std::mutex mu;
    std::condition_variable cv;
  } data;

  a0_packet_callback_t make_callback() {
    return a0_packet_callback_t{
        .user_data = &data,
        .fn = [](void* user_data, a0_packet_t pkt) {
          auto* data = (data_t*)user_data;

          std::unique_lock<std::mutex> lk{data->mu};
          data->collected_payloads.push_back(a0::test::str(pkt.payload));
          data->cv.notify_all();
        },
    };
  }

  std::function<void(a0::Packet)> make_cpp_callback() {
    return [&](a0::Packet pkt) {
      std::unique_lock<std::mutex> lk{data.mu};
      data.collected_payloads.push_back(std::string(pkt.payload()));
      data.cv.notify_all();
    };
  }

  void WAIT_AND_REQUIRE_PAYLOADS(std::vector<std::string> want_payloads) {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.collected_payloads.size() >= want_payloads.size();
    });
    REQUIRE(data.collected_payloads == want_payloads);
  }
};

TEST_CASE_FIXTURE(ReaderFixture, "reader] close no packet") {
  REQUIRE_OK(a0_reader_init(&r, arena, a0::test::alloc(), A0_INIT_OLDEST, A0_ITER_NEXT, make_callback()));
  REQUIRE_OK(a0_reader_close(&r));
}

TEST_CASE_FIXTURE(ReaderFixture, "reader] oldest-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_init(&r, arena, a0::test::alloc(), A0_INIT_OLDEST, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_close(&r));
}

TEST_CASE_FIXTURE(ReaderFixture, "reader] cpp oldest-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  a0::Reader cpp_r(
      a0::cpp_wrap<a0::Arena>(arena),
      A0_INIT_OLDEST,
      A0_ITER_NEXT,
      make_cpp_callback());

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});
}

TEST_CASE_FIXTURE(ReaderFixture, "reader] oldest-next, empty start") {
  REQUIRE_OK(a0_reader_init(&r, arena, a0::test::alloc(), A0_INIT_OLDEST, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1"});

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_close(&r));
}

TEST_CASE_FIXTURE(ReaderFixture, "reader] most recent-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_init(&r, arena, a0::test::alloc(), A0_INIT_MOST_RECENT, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_close(&r));
}

TEST_CASE_FIXTURE(ReaderFixture, "reader] most recent-next, empty start") {
  REQUIRE_OK(a0_reader_init(&r, arena, a0::test::alloc(), A0_INIT_MOST_RECENT, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1"});

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_close(&r));
}

TEST_CASE_FIXTURE(ReaderFixture, "reader] await new-next") {
  push_pkt("pkt_0");
  push_pkt("pkt_1");

  REQUIRE_OK(a0_reader_init(&r, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_2"});

  push_pkt("pkt_3");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_2", "pkt_3"});

  REQUIRE_OK(a0_reader_close(&r));
}

TEST_CASE_FIXTURE(ReaderFixture, "reader] await new-next, empty start") {
  REQUIRE_OK(a0_reader_init(&r, arena, a0::test::alloc(), A0_INIT_AWAIT_NEW, A0_ITER_NEXT, make_callback()));

  push_pkt("pkt_0");
  push_pkt("pkt_1");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1"});

  push_pkt("pkt_2");

  WAIT_AND_REQUIRE_PAYLOADS({"pkt_0", "pkt_1", "pkt_2"});

  REQUIRE_OK(a0_reader_close(&r));
}
