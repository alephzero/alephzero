#include <a0/arena.h>
#include <a0/arena.hpp>
#include <a0/buf.h>
#include <a0/middleware.h>
#include <a0/middleware.hpp>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/string_view.hpp>
#include <a0/transport.h>
#include <a0/writer.h>
#include <a0/writer.hpp>

#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/test_util.hpp"
#include "src/c_wrap.hpp"

struct WriterFixture {
  std::vector<uint8_t> arena_data;
  a0_arena_t arena;

  WriterFixture() {
    arena_data.resize(4096);
    arena.buf.ptr = arena_data.data();
    arena.buf.size = arena_data.size();
    arena.mode = A0_ARENA_MODE_SHARED;
  }

  void require_transport_state(std::vector<std::pair<std::vector<std::pair<std::string, std::string>>, std::string>> want_pkts) {
    a0_transport_t transport;
    REQUIRE_OK(a0_transport_init(&transport, arena));

    a0_locked_transport_t lk;
    REQUIRE_OK(a0_transport_lock(&transport, &lk));

    bool empty;
    REQUIRE_OK(a0_transport_empty(lk, &empty));
    REQUIRE(empty == want_pkts.empty());

    a0_transport_frame_t frame;

    REQUIRE_OK(a0_transport_jump_head(lk));

    for (size_t i = 0; i < want_pkts.size(); i++) {
      auto&& want_hdrs = want_pkts[i].first;
      auto&& want_payload = want_pkts[i].second;
      REQUIRE_OK(a0_transport_frame(lk, &frame));
      a0_packet_t got_pkt = a0::test::unflatten(a0::test::buf(frame));
      REQUIRE(got_pkt.headers_block.size == want_hdrs.size());

      for (size_t j = 0; j < got_pkt.headers_block.size; j++) {
        auto&& got_hdr = got_pkt.headers_block.headers[j];
        auto&& want_key = want_hdrs[j].first;
        auto&& want_val = want_hdrs[j].second;
        REQUIRE(std::string(got_hdr.key) == want_key);
        if (want_val != "???") {
          REQUIRE(std::string(got_hdr.val) == want_val);
        }
      }
      REQUIRE(a0::test::str(got_pkt.payload) == want_payload);

      bool has_next;
      REQUIRE_OK(a0_transport_has_next(lk, &has_next));
      if (i + 1 == want_pkts.size()) {
        REQUIRE(!has_next);
      } else {
        REQUIRE(has_next);
        REQUIRE_OK(a0_transport_step_next(lk));
      }
    }

    REQUIRE_OK(a0_transport_unlock(lk));
  }
};

TEST_CASE_FIXTURE(WriterFixture, "writer] basic") {
  a0_writer_t w;
  REQUIRE_OK(a0_writer_init(&w, arena));

  REQUIRE_OK(a0_writer_write(&w, a0::test::pkt({{"key", "val"}}, "msg #0")));
  REQUIRE_OK(a0_writer_write(&w, a0::test::pkt({{"key", "val"}}, "msg #1")));

  REQUIRE_OK(a0_writer_close(&w));

  require_transport_state(
      {{
           {{"key", "val"}},
           "msg #0",
       },
       {
           {{"key", "val"}},
           "msg #1",
       }});
}

TEST_CASE_FIXTURE(WriterFixture, "writer] cpp basic") {
  a0::Writer w(a0::cpp_wrap<a0::Arena>(arena));

  w.write(a0::Packet({{"key", "val"}}, "msg #0"));
  w.write(a0::Packet({{"key", "val"}}, "msg #1"));

  require_transport_state(
      {{
           {{"key", "val"}},
           "msg #0",
       },
       {
           {{"key", "val"}},
           "msg #1",
       }});
}

TEST_CASE_FIXTURE(WriterFixture, "writer] wrap middleware") {
  a0_writer_t w_0;
  REQUIRE_OK(a0_writer_init(&w_0, arena));

  a0_writer_t w_1;
  REQUIRE_OK(a0_writer_wrap(&w_0, a0_add_time_mono_header(), &w_1));

  a0_writer_t w_2;
  REQUIRE_OK(a0_writer_wrap(&w_1, a0_add_time_wall_header(), &w_2));

  a0_writer_t w_3;
  REQUIRE_OK(a0_writer_wrap(&w_2, a0_add_writer_id_header(), &w_3));

  a0_writer_t w_4;
  REQUIRE_OK(a0_writer_wrap(&w_3, a0_add_writer_seq_header(), &w_4));

  a0_writer_t w_5;
  REQUIRE_OK(a0_writer_wrap(&w_4, a0_add_transport_seq_header(), &w_5));

  REQUIRE_OK(a0_writer_write(&w_0, a0::test::pkt({{"key", "val"}}, "msg #0")));
  REQUIRE_OK(a0_writer_write(&w_1, a0::test::pkt({{"key", "val"}}, "msg #1")));
  REQUIRE_OK(a0_writer_write(&w_2, a0::test::pkt({{"key", "val"}}, "msg #2")));
  REQUIRE_OK(a0_writer_write(&w_3, a0::test::pkt({{"key", "val"}}, "msg #3")));
  REQUIRE_OK(a0_writer_write(&w_4, a0::test::pkt({{"key", "val"}}, "msg #4")));
  REQUIRE_OK(a0_writer_write(&w_5, a0::test::pkt({{"key", "val"}}, "msg #5")));

  REQUIRE_OK(a0_writer_close(&w_5));
  REQUIRE_OK(a0_writer_close(&w_4));
  REQUIRE_OK(a0_writer_close(&w_3));
  REQUIRE_OK(a0_writer_close(&w_2));
  REQUIRE_OK(a0_writer_close(&w_1));
  REQUIRE_OK(a0_writer_close(&w_0));

  require_transport_state(
      {{
           {
               {"key", "val"},
           },
           "msg #0",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"key", "val"},
           },
           "msg #1",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"a0_time_wall", "???"},
               {"key", "val"},
           },
           "msg #2",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"a0_time_wall", "???"},
               {"a0_writer_id", "???"},
               {"key", "val"},
           },
           "msg #3",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"a0_time_wall", "???"},
               {"a0_writer_id", "???"},
               {"a0_writer_seq", "0"},
               {"key", "val"},
           },
           "msg #4",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"a0_transport_seq", "5"},
               {"a0_time_wall", "???"},
               {"a0_writer_id", "???"},
               {"a0_writer_seq", "1"},
               {"key", "val"},
           },
           "msg #5",
       }});
}

TEST_CASE_FIXTURE(WriterFixture, "writer] cpp wrap middleware") {
  a0::Writer w_0(a0::cpp_wrap<a0::Arena>(arena));

  a0::Writer w_1 = w_0.wrap(a0::add_time_mono_header());
  a0::Writer w_2 = w_1.wrap(a0::add_time_wall_header());
  a0::Writer w_3 = w_2.wrap(a0::add_writer_id_header());
  a0::Writer w_4 = w_3.wrap(a0::add_writer_seq_header());
  a0::Writer w_5 = w_4.wrap(a0::add_transport_seq_header());

  w_0.write(a0::Packet({{"key", "val"}}, "msg #0"));
  w_1.write(a0::Packet({{"key", "val"}}, "msg #1"));
  w_2.write(a0::Packet({{"key", "val"}}, "msg #2"));
  w_3.write(a0::Packet({{"key", "val"}}, "msg #3"));
  w_4.write(a0::Packet({{"key", "val"}}, "msg #4"));
  w_5.write(a0::Packet({{"key", "val"}}, "msg #5"));

  require_transport_state(
      {{
           {
               {"key", "val"},
           },
           "msg #0",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"key", "val"},
           },
           "msg #1",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"a0_time_wall", "???"},
               {"key", "val"},
           },
           "msg #2",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"a0_time_wall", "???"},
               {"a0_writer_id", "???"},
               {"key", "val"},
           },
           "msg #3",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"a0_time_wall", "???"},
               {"a0_writer_id", "???"},
               {"a0_writer_seq", "0"},
               {"key", "val"},
           },
           "msg #4",
       },
       {
           {
               {"a0_time_mono", "???"},
               {"a0_transport_seq", "5"},
               {"a0_time_wall", "???"},
               {"a0_writer_id", "???"},
               {"a0_writer_seq", "1"},
               {"key", "val"},
           },
           "msg #5",
       }});
}

TEST_CASE_FIXTURE(WriterFixture, "writer] standard headers") {
  a0_writer_t w_0;
  REQUIRE_OK(a0_writer_init(&w_0, arena));

  a0_writer_t w_1;
  REQUIRE_OK(a0_writer_wrap(&w_0, a0_add_standard_headers(), &w_1));

  REQUIRE_OK(a0_writer_write(&w_0, a0::test::pkt({{"key", "val"}}, "msg #0")));
  REQUIRE_OK(a0_writer_write(&w_1, a0::test::pkt({{"key", "val"}}, "msg #1")));
  REQUIRE_OK(a0_writer_write(&w_1, a0::test::pkt({{"key", "val"}}, "msg #2")));

  REQUIRE_OK(a0_writer_close(&w_1));
  REQUIRE_OK(a0_writer_close(&w_0));

  require_transport_state(
      {{
           {
               {"key", "val"},
           },
           "msg #0",
       },
       {
           {
               {"a0_transport_seq", "1"},
               {"a0_time_mono", "???"},
               {"a0_writer_seq", "0"},
               {"a0_writer_id", "???"},
               {"a0_time_wall", "???"},
               {"key", "val"},
           },
           "msg #1",
       },
       {
           {
               {"a0_transport_seq", "2"},
               {"a0_time_mono", "???"},
               {"a0_writer_seq", "1"},
               {"a0_writer_id", "???"},
               {"a0_time_wall", "???"},
               {"key", "val"},
           },
           "msg #2",
       }});
}

TEST_CASE_FIXTURE(WriterFixture, "writer] cpp standard headers") {
  a0::Writer w_0(a0::cpp_wrap<a0::Arena>(arena));

  a0::Writer w_1 = w_0.wrap(a0::add_standard_headers());

  w_0.write(a0::Packet({{"key", "val"}}, "msg #0"));
  w_1.write(a0::Packet({{"key", "val"}}, "msg #1"));
  w_1.write(a0::Packet({{"key", "val"}}, "msg #2"));

  require_transport_state(
      {{
           {
               {"key", "val"},
           },
           "msg #0",
       },
       {
           {
               {"a0_transport_seq", "1"},
               {"a0_time_mono", "???"},
               {"a0_writer_seq", "0"},
               {"a0_writer_id", "???"},
               {"a0_time_wall", "???"},
               {"key", "val"},
           },
           "msg #1",
       },
       {
           {
               {"a0_transport_seq", "2"},
               {"a0_time_mono", "???"},
               {"a0_writer_seq", "1"},
               {"a0_writer_id", "???"},
               {"a0_time_wall", "???"},
               {"key", "val"},
           },
           "msg #2",
       }});
}

TEST_CASE_FIXTURE(WriterFixture, "writer] push middleware") {
  a0_writer_t w;
  REQUIRE_OK(a0_writer_init(&w, arena));
  REQUIRE_OK(a0_writer_push(&w, a0_add_writer_seq_header()));
  REQUIRE_OK(a0_writer_push(&w, a0_add_time_wall_header()));
  REQUIRE_OK(a0_writer_write(&w, a0::test::pkt({{"key", "val"}}, "msg #0")));
  REQUIRE_OK(a0_writer_close(&w));

  require_transport_state(
      {{
          {
              {"a0_writer_seq", "0"},
              {"a0_time_wall", "???"},
              {"key", "val"},
          },
          "msg #0",
      }});
}

TEST_CASE_FIXTURE(WriterFixture, "writer] cpp push middleware") {
  a0::Writer w(a0::cpp_wrap<a0::Arena>(arena));
  w.push(a0::add_writer_seq_header());
  w.push(a0::add_time_wall_header());

  w.write(a0::Packet({{"key", "val"}}, "msg #0"));

  require_transport_state(
      {{
          {
              {"a0_writer_seq", "0"},
              {"a0_time_wall", "???"},
              {"key", "val"},
          },
          "msg #0",
      }});
}

TEST_CASE_FIXTURE(WriterFixture, "writer] cpp") {
  a0::Writer w(a0::cpp_wrap<a0::Arena>(arena));

  w.write("msg #0");
  std::string payload = "msg #1";
  w.write(payload);
  a0::string_view payload_view = payload;
  w.write(payload_view);
  w.write(a0::Packet({{"key", "val"}}, "msg #2"));

  require_transport_state(
      {{
           {}, "msg #0",
       },
       {
           {}, "msg #1",
       },
       {
           {}, "msg #1",
       },
       {
           {{"key", "val"}},
           "msg #2",
       }});
}

#ifdef DEBUG
TEST_CASE_FIXTURE(WriterFixture, "writer] middleware close mis-order") {
  a0_writer_t w_0;
  REQUIRE_OK(a0_writer_init(&w_0, arena));

  a0_writer_t w_1;
  REQUIRE_OK(a0_writer_wrap(&w_0, a0_add_time_mono_header(), &w_1));

  REQUIRE_SIGNAL({ a0_writer_close(&w_0); });
  REQUIRE_OK(a0_writer_close(&w_1));
  REQUIRE_OK(a0_writer_close(&w_0));
}
#endif