#include <a0/rpc.h>
#include <a0/shm.h>

#include <doctest.h>

#include <functional>
#include <map>

#include "src/strutil.hpp"
#include "src/test_util.hpp"

static const char kTestShm[] = "/test.shm";

struct RpcFixture {
  a0_shm_t shm;

  RpcFixture() {
    a0_shm_unlink(kTestShm);

    a0_shm_options_t shmopt;
    shmopt.size = 16 * 1024 * 1024;
    a0_shm_open(kTestShm, &shmopt, &shm);
  }

  ~RpcFixture() {
    a0_shm_close(&shm);
    a0_shm_unlink(kTestShm);
  }
};

TEST_CASE_FIXTURE(RpcFixture, "Test rpc") {
  struct data_t {
    size_t reply_cnt;
    size_t cancel_cnt;
  };
  a0::sync<data_t> data;

  a0_rpc_request_callback_t onrequest = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_rpc_request_t req) {
            a0_packet_id_t req_id;
            REQUIRE_OK(a0_packet_id(req.pkt, &req_id));

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(req.pkt, &payload));
            if (!strcmp((const char*)payload.ptr, "reply")) {
              a0_packet_t pkt;
              REQUIRE_OK(
                  a0_packet_build(A0_EMPTY_HEADER_LIST, payload, a0::test::allocator(), &pkt));
              REQUIRE_OK(a0_rpc_reply(req, pkt));
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

  a0_rpc_server_t server;
  REQUIRE_OK(a0_rpc_server_init(&server, shm.buf, a0::test::allocator(), onrequest, oncancel));

  a0_rpc_client_t client;
  REQUIRE_OK(a0_rpc_client_init(&client, shm.buf, a0::test::allocator()));

  a0_packet_callback_t onreply = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            auto* data = (a0::sync<data_t>*)user_data;

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(pkt, &payload));
            data->notify_all([](auto* data_) {
              data_->reply_cnt++;
            });
          },
  };

  for (int i = 0; i < 5; i++) {
    a0_packet_t req;
    REQUIRE_OK(
        a0_packet_build(A0_EMPTY_HEADER_LIST, a0::test::buf("reply"), a0::test::allocator(), &req));
    REQUIRE_OK(a0_rpc_send(&client, req, onreply));
  }

  for (int i = 0; i < 5; i++) {
    a0_packet_t req;
    REQUIRE_OK(a0_packet_build(A0_EMPTY_HEADER_LIST,
                               a0::test::buf("await_cancel"),
                               a0::test::allocator(),
                               &req));
    REQUIRE_OK(a0_rpc_send(&client, req, onreply));

    a0_packet_id_t req_id;
    REQUIRE_OK(a0_packet_id(req, &req_id));
    a0_rpc_cancel(&client, req_id);
  }

  data.wait([](auto* data_) {
    return data_->reply_cnt >= 5 && data_->cancel_cnt >= 5;
  });

  REQUIRE_OK(a0_rpc_client_close(&client));
  REQUIRE_OK(a0_rpc_server_close(&server));
}

TEST_CASE_FIXTURE(RpcFixture, "Test rpc empty oncancel onreply") {
  a0_rpc_request_callback_t onrequest = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_rpc_request_t req) {
            a0_packet_id_t req_id;
            REQUIRE_OK(a0_packet_id(req.pkt, &req_id));

            a0_buf_t payload;
            REQUIRE_OK(a0_packet_payload(req.pkt, &payload));

            a0_packet_t pkt;
            REQUIRE_OK(a0_packet_build(A0_EMPTY_HEADER_LIST, payload, a0::test::allocator(), &pkt));
            REQUIRE_OK(a0_rpc_reply(req, pkt));
          },
  };

  a0_packet_id_callback_t oncancel = A0_NOP_PACKET_ID_CB;

  a0_rpc_server_t server;
  REQUIRE_OK(a0_rpc_server_init(&server, shm.buf, a0::test::allocator(), onrequest, oncancel));

  a0_rpc_client_t client;
  REQUIRE_OK(a0_rpc_client_init(&client, shm.buf, a0::test::allocator()));

  a0_packet_callback_t onreply = A0_NOP_PACKET_CB;

  for (int i = 0; i < 5; i++) {
    a0_packet_t req;
    REQUIRE_OK(
        a0_packet_build(A0_EMPTY_HEADER_LIST, a0::test::buf("msg"), a0::test::allocator(), &req));
    REQUIRE_OK(a0_rpc_send(&client, req, onreply));

    a0_packet_id_t req_id;
    REQUIRE_OK(a0_packet_id(req, &req_id));
    a0_rpc_cancel(&client, req_id);
  }

  // TODO: Find a better way.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  REQUIRE_OK(a0_rpc_client_close(&client));
  REQUIRE_OK(a0_rpc_server_close(&server));
}
