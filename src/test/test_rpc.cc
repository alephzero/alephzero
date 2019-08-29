#include <a0/rpc.h>

#include <doctest.h>

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

#include "src/strutil.hh"
#include "src/test_util.hh"

static const char kTestShm[] = "/test.shm";

struct RpcFixture {
  a0_shmobj_t shmobj;

  RpcFixture() {
    a0_shmobj_unlink(kTestShm);

    a0_shmobj_options_t shmopt;
    shmopt.size = 16 * 1024 * 1024;
    a0_shmobj_open(kTestShm, &shmopt, &shmobj);
  }

  ~RpcFixture() {
    a0_shmobj_close(&shmobj);
    a0_shmobj_unlink(kTestShm);
  }
};

TEST_CASE_FIXTURE(RpcFixture, "Test rpc") {
  struct data_t {
    size_t reply_cnt{0};
    size_t cancel_cnt{0};
    std::mutex mu;
    std::condition_variable cv;
  } data;

  a0_rpc_request_callback_t onrequest = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_rpc_request_t req) {
            a0_packet_id_t req_id;
            REQUIRE(a0_packet_id(req.pkt, &req_id) == A0_OK);

            a0_buf_t payload;
            REQUIRE(a0_packet_payload(req.pkt, &payload) == A0_OK);
            if (!strcmp((const char*)payload.ptr, "reply")) {
              a0_packet_t pkt;
              REQUIRE(a0_packet_build(0, nullptr, payload, a0::test::allocator(), &pkt) == A0_OK);
              REQUIRE(a0_rpc_reply(req, pkt) == A0_OK);
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
            }
            data->cv.notify_all();
          },
  };

  a0_rpc_server_t server;
  REQUIRE(a0_rpc_server_init(&server, shmobj, a0::test::allocator(), onrequest, oncancel) == A0_OK);

  a0_rpc_client_t client;
  REQUIRE(a0_rpc_client_init(&client, shmobj, a0::test::allocator()) == A0_OK);

  a0_packet_callback_t onreply = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            a0_buf_t payload;
            REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);
            auto* data = (data_t*)user_data;
            {
              std::unique_lock<std::mutex> lk{data->mu};
              data->reply_cnt++;
            }
            data->cv.notify_all();
          },
  };

  for (int i = 0; i < 5; i++) {
    a0_packet_t req;
    REQUIRE(a0_packet_build(0, nullptr, a0::test::buf("reply"), a0::test::allocator(), &req) ==
            A0_OK);
    REQUIRE(a0_rpc_send(&client, req, onreply) == A0_OK);
  }

  for (int i = 0; i < 5; i++) {
    a0_packet_t req;
    REQUIRE(
        a0_packet_build(0, nullptr, a0::test::buf("await_cancel"), a0::test::allocator(), &req) ==
        A0_OK);
    REQUIRE(a0_rpc_send(&client, req, onreply) == A0_OK);

    a0_packet_id_t req_id;
    REQUIRE(a0_packet_id(req, &req_id) == A0_OK);
    a0_rpc_cancel(&client, req_id);
  }

  {
    std::unique_lock<std::mutex> lk{data.mu};
    data.cv.wait(lk, [&]() {
      return data.reply_cnt >= 5 && data.cancel_cnt >= 5;
    });
  }

  REQUIRE(a0_rpc_client_close(&client) == A0_OK);
  REQUIRE(a0_rpc_server_close(&server) == A0_OK);
}
