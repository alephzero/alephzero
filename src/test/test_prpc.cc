#include <a0/prpc.h>
#include <a0/shm.h>

#include <doctest.h>

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

#include "src/strutil.hpp"
#include "src/test_util.hpp"

static const char kTestShm[] = "/test.shm";

struct PrpcFixture {
  a0_shm_t shm;

  PrpcFixture() {
    a0_shm_unlink(kTestShm);

    a0_shm_options_t shmopt;
    shmopt.size = 16 * 1024 * 1024;
    a0_shm_open(kTestShm, &shmopt, &shm);
  }

  ~PrpcFixture() {
    a0_shm_close(&shm);
    a0_shm_unlink(kTestShm);
  }
};

TEST_CASE_FIXTURE(PrpcFixture, "Test prpc") {
  struct data_t {
    size_t msg_cnt{0};
    size_t done_cnt{0};
    std::mutex mu;
    std::condition_variable cv;
  } data;

  a0_prpc_connection_callback_t onconnect = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_prpc_connection_t conn) {
            a0_packet_id_t conn_id;
            REQUIRE(a0_packet_id(conn.pkt, &conn_id) == A0_OK);

            a0_buf_t payload;
            REQUIRE(a0_packet_payload(conn.pkt, &payload) == A0_OK);
            if (!strcmp((const char*)payload.ptr, "reply")) {
              a0_packet_t pkt;
              REQUIRE(a0_packet_build(0, nullptr, payload, a0::test::allocator(), &pkt) == A0_OK);
              REQUIRE(a0_prpc_send(conn, pkt, false) == A0_OK);
              REQUIRE(a0_prpc_send(conn, pkt, false) == A0_OK);
              REQUIRE(a0_prpc_send(conn, pkt, false) == A0_OK);
              REQUIRE(a0_prpc_send(conn, pkt, false) == A0_OK);
              REQUIRE(a0_prpc_send(conn, pkt, true) == A0_OK);
            }
          },
  };

  a0_packet_id_callback_t oncancel = {.user_data = nullptr, .fn = nullptr};

  a0_prpc_server_t server;
  REQUIRE(a0_prpc_server_init(&server, shm.buf, a0::test::allocator(), onconnect, oncancel) ==
          A0_OK);

  a0_prpc_client_t client;
  REQUIRE(a0_prpc_client_init(&client, shm.buf, a0::test::allocator()) == A0_OK);

  a0_prpc_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt, bool done) {
            a0_buf_t payload;
            REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);
            auto* data = (data_t*)user_data;
            {
              std::unique_lock<std::mutex> lk{data->mu};
              data->msg_cnt++;
              if (done) {
                data->done_cnt++;
              }
              data->cv.notify_all();
            }
          },
  };

  a0_packet_t req;
  REQUIRE(a0_packet_build(0, nullptr, a0::test::buf("reply"), a0::test::allocator(), &req) ==
          A0_OK);
  REQUIRE(a0_prpc_connect(&client, req, onmsg) == A0_OK);

  {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.msg_cnt >= 5 && data.done_cnt >= 1;
    });
  }

  REQUIRE(a0_prpc_client_close(&client) == A0_OK);
  REQUIRE(a0_prpc_server_close(&server) == A0_OK);
}

TEST_CASE_FIXTURE(PrpcFixture, "Test cancel prpc") {
  struct data_t {
    size_t msg_cnt{0};
    size_t cancel_cnt{0};
    std::mutex mu;
    std::condition_variable cv;
  } data;

  a0_prpc_connection_callback_t onconnect = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_prpc_connection_t conn) {
            a0_packet_id_t conn_id;
            REQUIRE(a0_packet_id(conn.pkt, &conn_id) == A0_OK);

            a0_buf_t payload;
            REQUIRE(a0_packet_payload(conn.pkt, &payload) == A0_OK);
            if (!strcmp((const char*)payload.ptr, "reply")) {
              a0_packet_t pkt;
              REQUIRE(a0_packet_build(0, nullptr, payload, a0::test::allocator(), &pkt) == A0_OK);
              REQUIRE(a0_prpc_send(conn, pkt, false) == A0_OK);
            }
          },
  };

  a0_packet_id_callback_t oncancel = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_id_t unused) {
            (void)unused;
            auto* data = (data_t*)user_data;
            {
              std::unique_lock<std::mutex> lk{data->mu};
              data->cancel_cnt++;
              data->cv.notify_all();
            }
          },
  };

  a0_prpc_server_t server;
  REQUIRE(a0_prpc_server_init(&server, shm.buf, a0::test::allocator(), onconnect, oncancel) ==
          A0_OK);

  a0_prpc_client_t client;
  REQUIRE(a0_prpc_client_init(&client, shm.buf, a0::test::allocator()) == A0_OK);

  a0_prpc_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt, bool) {
            a0_buf_t payload;
            REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);
            auto* data = (data_t*)user_data;
            {
              std::unique_lock<std::mutex> lk{data->mu};
              data->msg_cnt++;
              data->cv.notify_all();
            }
          },
  };

  a0_packet_t conn;
  REQUIRE(a0_packet_build(0, nullptr, a0::test::buf("reply"), a0::test::allocator(), &conn) ==
          A0_OK);
  REQUIRE(a0_prpc_connect(&client, conn, onmsg) == A0_OK);

  {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.msg_cnt >= 1;
    });
  }

  a0_packet_id_t conn_id;
  REQUIRE(a0_packet_id(conn, &conn_id) == A0_OK);
  a0_prpc_cancel(&client, conn_id);

  {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.cancel_cnt >= 1;
    });
  }

  REQUIRE(a0_prpc_client_close(&client) == A0_OK);
  REQUIRE(a0_prpc_server_close(&server) == A0_OK);
}
