#include <a0/file.h>
#include <a0/packet.h>
#include <a0/packet.hpp>
#include <a0/prpc.h>
#include <a0/prpc.hpp>
#include <a0/string_view.hpp>
#include <a0/uuid.h>

#include <doctest.h>

#include <functional>
#include <ostream>
#include <string>

#include "src/test_util.hpp"

struct PrpcFixture {
  a0_prpc_topic_t topic = {"test", nullptr};
  const char* topic_path = "alephzero/test.prpc.a0";

  PrpcFixture() {
    clear();
  }

  ~PrpcFixture() {
    clear();
  }

  void clear() {
    a0_file_remove(topic_path);
  }
};

TEST_CASE_FIXTURE(PrpcFixture, "prpc] basic") {
  struct data_t {
    a0::test::Latch msg_latch{5};
    a0::test::Latch done_latch{1};
  } data{};

  a0_prpc_connection_callback_t onconnect = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_prpc_connection_t conn) {
            REQUIRE(a0::test::str(conn.pkt.payload) == "connect");
            auto progress = a0::test::pkt("progress");
            REQUIRE_OK(a0_prpc_server_send(conn, progress, false));
            REQUIRE_OK(a0_prpc_server_send(conn, progress, false));
            REQUIRE_OK(a0_prpc_server_send(conn, progress, false));
            REQUIRE_OK(a0_prpc_server_send(conn, progress, false));
            REQUIRE_OK(a0_prpc_server_send(conn, progress, true));
          },
  };

  a0_prpc_server_t server;
  REQUIRE_OK(a0_prpc_server_init(&server, topic, a0::test::alloc(), onconnect, {}));

  a0_prpc_client_t client;
  REQUIRE_OK(a0_prpc_client_init(&client, topic, a0::test::alloc()));

  a0_prpc_progress_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t, bool done) {
            auto* data = (data_t*)user_data;
            data->msg_latch.count_down();
            if (done) {
              data->done_latch.count_down();
            }
          },
  };

  REQUIRE_OK(a0_prpc_client_connect(&client, a0::test::pkt("connect"), onmsg));

  data.msg_latch.wait();
  data.done_latch.wait();

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}

TEST_CASE_FIXTURE(PrpcFixture, "prpc] cpp basic") {
  struct data_t {
    a0::test::Latch msg_latch{5};
    a0::test::Latch done_latch{1};
  } data{};

  auto onconnect = [&](a0::PrpcConnection conn) {
    REQUIRE(conn.pkt().payload() == "connect");
    conn.send("progress", false);
    conn.send("progress", false);
    conn.send("progress", false);
    conn.send("progress", false);
    conn.send("progress", true);
  };

  a0::PrpcServer server("test", onconnect, {});

  a0::PrpcClient client("test");

  client.connect("connect", [&](a0::Packet, bool done) {
    data.msg_latch.count_down();
    if (done) {
      data.done_latch.count_down();
    }
  });

  data.msg_latch.wait();
  data.done_latch.wait();
}

TEST_CASE_FIXTURE(PrpcFixture, "prpc] cancel") {
  struct data_t {
    a0::test::Latch msg_latch{1};
    a0::test::Latch cancel_latch{1};
  } data{};

  a0_prpc_connection_callback_t onconnect = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_prpc_connection_t conn) {
            if (a0::test::str(conn.pkt.payload) == "connect") {
              REQUIRE_OK(a0_prpc_server_send(conn, a0::test::pkt(conn.pkt.payload), false));
            }
          },
  };

  a0_packet_id_callback_t oncancel = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_uuid_t) {
            auto* data = (data_t*)user_data;
            data->cancel_latch.count_down();
          },
  };

  a0_prpc_server_t server;
  REQUIRE_OK(a0_prpc_server_init(&server, topic, a0::test::alloc(), onconnect, oncancel));

  a0_prpc_client_t client;
  REQUIRE_OK(a0_prpc_client_init(&client, topic, a0::test::alloc()));

  a0_prpc_progress_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t, bool) {
            auto* data = (data_t*)user_data;
            data->msg_latch.count_down();
          },
  };

  auto conn = a0::test::pkt("connect");
  REQUIRE_OK(a0_prpc_client_connect(&client, conn, onmsg));

  data.msg_latch.wait();

  a0_prpc_client_cancel(&client, conn.id);

  data.cancel_latch.wait();

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}
