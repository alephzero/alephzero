#include <a0/prpc.h>
#include <a0/shm.h>

#include <doctest.h>

#include <functional>
#include <map>

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
    size_t msg_cnt;
    size_t done_cnt;
  };
  a0::sync<data_t> data;

  a0_prpc_connection_callback_t onconnect = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_prpc_connection_t conn) {
            a0_packet_id_t conn_id;
            REQUIRE_OK(a0_packet_id(conn.pkt, &conn_id));

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(conn.pkt, &payload));
            if (!strcmp((const char*)payload.ptr, "reply")) {
              a0_packet_t pkt;
              REQUIRE_OK(
                  a0_packet_build(A0_EMPTY_HEADER_LIST, payload, a0::test::allocator(), &pkt));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, true));
            }
          },
  };

  a0_packet_id_callback_t oncancel = A0_NOP_PACKET_ID_CB;

  a0_prpc_server_t server;
  REQUIRE_OK(a0_prpc_server_init(&server, shm.buf, a0::test::allocator(), onconnect, oncancel));

  a0_prpc_client_t client;
  REQUIRE_OK(a0_prpc_client_init(&client, shm.buf, a0::test::allocator()));

  a0_prpc_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt, bool done) {
            auto* data = (a0::sync<data_t>*)user_data;

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(pkt, &payload));
            data->notify_all([&](auto* data_) {
              data_->msg_cnt++;
              if (done) {
                data_->done_cnt++;
              }
            });
          },
  };

  a0_packet_t req;
  REQUIRE_OK(
      a0_packet_build(A0_EMPTY_HEADER_LIST, a0::test::buf("reply"), a0::test::allocator(), &req));
  REQUIRE_OK(a0_prpc_connect(&client, req, onmsg));

  data.wait([](auto* data_) {
    return data_->msg_cnt >= 5 && data_->done_cnt >= 1;
  });

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}

TEST_CASE_FIXTURE(PrpcFixture, "Test cancel prpc") {
  struct data_t {
    size_t msg_cnt;
    size_t cancel_cnt;
  };
  a0::sync<data_t> data;

  a0_prpc_connection_callback_t onconnect = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_prpc_connection_t conn) {
            a0_packet_id_t conn_id;
            REQUIRE_OK(a0_packet_id(conn.pkt, &conn_id));

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(conn.pkt, &payload));
            if (!strcmp((const char*)payload.ptr, "reply")) {
              a0_packet_t pkt;
              REQUIRE_OK(
                  a0_packet_build(A0_EMPTY_HEADER_LIST, payload, a0::test::allocator(), &pkt));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
            }
          },
  };

  a0_packet_id_callback_t oncancel = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_id_t unused) {
            (void)unused;
            auto* data = (a0::sync<data_t>*)user_data;
            data->notify_all([](auto* data_) {
              data_->cancel_cnt++;
            });
          },
  };

  a0_prpc_server_t server;
  REQUIRE_OK(a0_prpc_server_init(&server, shm.buf, a0::test::allocator(), onconnect, oncancel));

  a0_prpc_client_t client;
  REQUIRE_OK(a0_prpc_client_init(&client, shm.buf, a0::test::allocator()));

  a0_prpc_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt, bool) {
            auto* data = (a0::sync<data_t>*)user_data;

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(pkt, &payload));
            data->notify_all([](auto* data_) {
              data_->msg_cnt++;
            });
          },
  };

  a0_packet_t conn;
  REQUIRE_OK(
      a0_packet_build(A0_EMPTY_HEADER_LIST, a0::test::buf("reply"), a0::test::allocator(), &conn));
  REQUIRE_OK(a0_prpc_connect(&client, conn, onmsg));

  data.wait([](auto* data_) {
    return data_->msg_cnt >= 1;
  });

  a0_packet_id_t conn_id;
  REQUIRE_OK(a0_packet_id(conn, &conn_id));
  a0_prpc_cancel(&client, conn_id);

  data.wait([](auto* data_) {
    return data_->cancel_cnt >= 1;
  });

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}
