#include <a0/file.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/uuid.h>

#include <doctest.h>

#include <condition_variable>
#include <cstring>
#include <mutex>
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
    size_t msg_cnt;
    size_t done_cnt;
    std::mutex mu;
    std::condition_variable cv;
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
            std::unique_lock<std::mutex> lk{data->mu};
            data->msg_cnt++;
            if (done) {
              data->done_cnt++;
            }
            data->cv.notify_all();
          },
  };

  REQUIRE_OK(a0_prpc_client_connect(&client, a0::test::pkt("connect"), onmsg));

  {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.msg_cnt >= 5 && data.done_cnt >= 1;
    });
  }

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}

TEST_CASE_FIXTURE(PrpcFixture, "prpc] cancel") {
  struct data_t {
    size_t msg_cnt;
    size_t cancel_cnt;
    std::mutex mu;
    std::condition_variable cv;
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
            std::unique_lock<std::mutex> lk{data->mu};
            data->cancel_cnt++;
            data->cv.notify_all();
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
            std::unique_lock<std::mutex> lk{data->mu};
            data->msg_cnt++;
            data->cv.notify_all();
          },
  };

  auto conn = a0::test::pkt("connect");
  REQUIRE_OK(a0_prpc_client_connect(&client, conn, onmsg));

  {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.msg_cnt >= 1;
    });
  }

  a0_prpc_client_cancel(&client, conn.id);

  {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.cancel_cnt >= 1;
    });
  }

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}
