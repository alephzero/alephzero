#include <a0/rpc.h>

#include <a0/internal/strutil.hh>
#include <a0/internal/test_util.hh>

#include <doctest.h>

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

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

struct CloseHelper {
  bool closed{false};
  std::mutex mu;
  std::condition_variable cv;

  a0_callback_t callback() {
    return (a0_callback_t){
        .user_data = this,
        .fn =
            [](void* user_data) {
              auto* self = (CloseHelper*)user_data;
              {
                std::unique_lock<std::mutex> lk{self->mu};
                self->closed = true;
              }
              self->cv.notify_all();
            },
    };
  }

  void await_callback() {
    std::unique_lock<std::mutex> lk{mu};
    cv.wait(lk, [&]() {
      return closed;
    });
  }
};

TEST_CASE_FIXTURE(RpcFixture, "Test rpc") {
  struct data_t {
    size_t reply_cnt{0};
    size_t cancel_cnt{0};
    std::mutex mu;
    std::condition_variable cv;
  } data;

  a0_rpc_server_t server;

  a0_packet_callback_t onrequest = {
      .user_data = &server,
      .fn =
          [](void* user_data, a0_packet_t req) {
            a0_packet_id_t req_id;
            REQUIRE(a0_packet_id(req, &req_id) == A0_OK);

            a0_buf_t payload;
            REQUIRE(a0_packet_payload(req, &payload) == A0_OK);
            if (!strcmp((const char*)payload.ptr, "reply")) {
              a0_packet_t pkt;
              REQUIRE(a0_packet_build(0, nullptr, payload, a0::test::allocator(), &pkt) == A0_OK);
              REQUIRE(a0_rpc_reply((a0_rpc_server_t*)user_data, req_id, pkt) == A0_OK);
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

  REQUIRE(
      a0_rpc_server_init_unmanaged(&server, shmobj, a0::test::allocator(), onrequest, oncancel) ==
      A0_OK);

  a0_rpc_client_t client;
  REQUIRE(a0_rpc_client_init_unmanaged(&client, shmobj, a0::test::allocator()) == A0_OK);

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

  REQUIRE(a0_rpc_client_await_close(&client) == A0_OK);
  REQUIRE(a0_rpc_server_await_close(&server) == A0_OK);
}
