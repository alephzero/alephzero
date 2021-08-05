#include <a0/file.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/uuid.h>

#include <doctest.h>

#include <cstring>
#include <string>

#include "src/sync.hpp"
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
    size_t msg_cnt;
    size_t done_cnt;
  };
  a0::sync<data_t> data;

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
            auto* data = (a0::sync<data_t>*)user_data;
            data->notify_all([&](auto* data_) {
              data_->msg_cnt++;
              if (done) {
                data_->done_cnt++;
              }
            });
          },
  };

  REQUIRE_OK(a0_prpc_client_connect(&client, a0::test::pkt("connect"), onmsg));

  data.wait([](auto* data_) {
    return data_->msg_cnt >= 5 && data_->done_cnt >= 1;
  });

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}

TEST_CASE_FIXTURE(PrpcFixture, "prpc] cancel") {
  struct data_t {
    size_t msg_cnt;
    size_t cancel_cnt;
  };
  a0::sync<data_t> data;

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
            auto* data = (a0::sync<data_t>*)user_data;
            data->notify_all([](auto* data_) {
              data_->cancel_cnt++;
            });
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
            auto* data = (a0::sync<data_t>*)user_data;
            data->notify_all([](auto* data_) {
              data_->msg_cnt++;
            });
          },
  };

  auto conn = a0::test::pkt("connect");
  REQUIRE_OK(a0_prpc_client_connect(&client, conn, onmsg));

  data.wait([](auto* data_) {
    return data_->msg_cnt >= 1;
  });

  a0_prpc_client_cancel(&client, conn.id);

  data.wait([](auto* data_) {
    return data_->cancel_cnt >= 1;
  });

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}
