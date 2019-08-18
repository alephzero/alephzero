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

TEST_CASE_FIXTURE(RpcFixture, "Test rpc") {
  a0_rpc_server_t server;

  a0_packet_callback_t onrequest = {
      .user_data = &server,
      .fn =
          [](void* user_data, a0_packet_t req) {
            a0_buf_t payload;
            REQUIRE(a0_packet_payload(req, &payload) == A0_OK);

            a0_packet_t pkt;
            REQUIRE(a0_packet_build(0, nullptr, payload, a0::test::allocator(), &pkt) == A0_OK);

            REQUIRE(a0_rpc_reply((a0_rpc_server_t*)user_data, req, pkt) == A0_OK);
          },
  };

  a0_packet_callback_t oncancel = {
      .user_data = nullptr,
      .fn = nullptr,
  };

  REQUIRE(
      a0_rpc_server_init_unmanaged(&server, shmobj, a0::test::allocator(), onrequest, oncancel) ==
      A0_OK);

  a0_rpc_client_t client;
  REQUIRE(a0_rpc_client_init_unmanaged(&client, shmobj, a0::test::allocator()) == A0_OK);

  struct client_onreply_data_t {
    size_t cnt{0};
    std::mutex mu;
    std::condition_variable cv;
  } client_onreply_data;

  a0_packet_callback_t onreply = {
      .user_data = &client_onreply_data,
      .fn =
          [](void* user_data, a0_packet_t pkt) {
            a0_buf_t payload;
            REQUIRE(a0_packet_payload(pkt, &payload) == A0_OK);
            auto* data = (client_onreply_data_t*)user_data;
            {
              std::unique_lock<std::mutex> lk{data->mu};
              data->cnt++;
            }
            data->cv.notify_all();
          },
  };

  for (int i = 0; i < 10; i++) {
    a0_packet_t pkt;
    REQUIRE(a0_packet_build(0,
                            nullptr,
                            a0::test::buf(a0::strutil::fmt("msg #%d", i)),
                            a0::test::allocator(),
                            &pkt) == A0_OK);
    REQUIRE(a0_rpc_send(&client, pkt, onreply) == A0_OK);
  }

  {
    std::unique_lock<std::mutex> lk{client_onreply_data.mu};
    client_onreply_data.cv.wait(lk, [&]() {
      return client_onreply_data.cnt >= 10;
    });
  }

  struct client_onclose_data_t {
    bool closed{false};
    std::mutex mu;
    std::condition_variable cv;
  } client_onclose_data;

  a0_callback_t client_onclose = {
      .user_data = &client_onclose_data,
      .fn =
          [](void* user_data) {
            auto* data = (client_onclose_data_t*)user_data;
            {
              std::unique_lock<std::mutex> lk{data->mu};
              data->closed = true;
            }
            data->cv.notify_all();
          },
  };

  REQUIRE(a0_rpc_client_close(&client, client_onclose) == A0_OK);
  {
    std::unique_lock<std::mutex> lk{client_onclose_data.mu};
    client_onclose_data.cv.wait(lk, [&]() {
      return client_onclose_data.closed;
    });
  }

  struct server_onclose_data_t {
    bool closed{false};
    std::mutex mu;
    std::condition_variable cv;
  } server_onclose_data;

  a0_callback_t server_onclose = {
      .user_data = &server_onclose_data,
      .fn =
          [](void* user_data) {
            auto* data = (server_onclose_data_t*)user_data;
            {
              std::unique_lock<std::mutex> lk{data->mu};
              data->closed = true;
            }
            data->cv.notify_all();
          },
  };

  REQUIRE(a0_rpc_server_close(&server, server_onclose) == A0_OK);
  {
    std::unique_lock<std::mutex> lk{server_onclose_data.mu};
    server_onclose_data.cv.wait(lk, [&]() {
      return server_onclose_data.closed;
    });
  }
}
