#include <a0/rpc.h>

#include <a0/internal/strutil.hh>
#include <a0/internal/test_util.hh>

#include <doctest.h>

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

static const char kRequestShm[] = "/rpc_request.shm";
static const char kResponseShm[] = "/rpc_response.shm";

struct RpcFixture {
  a0_shmobj_t request_shmobj;
  a0_shmobj_t response_shmobj;

  RpcFixture() {
    a0_shmobj_unlink(kRequestShm);
    a0_shmobj_unlink(kResponseShm);

    a0_shmobj_options_t shmopt;
    shmopt.size = 16 * 1024 * 1024;
    a0_shmobj_open(kRequestShm, &shmopt, &request_shmobj);
    a0_shmobj_open(kResponseShm, &shmopt, &response_shmobj);
  }

  ~RpcFixture() {
    a0_shmobj_close(&request_shmobj);
    a0_shmobj_unlink(kRequestShm);

    a0_shmobj_close(&response_shmobj);
    a0_shmobj_unlink(kResponseShm);
  }
};

struct test_alloc {
  std::map<size_t, std::string> dump;
  std::mutex mu;

  a0_alloc_t get() {
    return (a0_alloc_t){
        .user_data = this,
        .fn =
            [](void* data, size_t size, a0_buf_t* out) {
              auto* self = (test_alloc*)data;
              std::unique_lock<std::mutex> lk{self->mu};
              auto key = self->dump.size();
              self->dump[key].resize(size);
              out->size = size;
              out->ptr = (uint8_t*)self->dump[key].c_str();
            },
    };
  }
};

TEST_CASE_FIXTURE(RpcFixture, "Test rpc") {
  test_alloc alloc;

  a0_rpc_server_t server;

  struct server_data_t {
    a0_rpc_server_t* server;
    test_alloc* alloc;
  } server_data{&server, &alloc};

  a0_rpc_server_onrequest_t onrequest = {
      .user_data = &server_data,
      .fn =
          [](void* user_data, a0_rpc_request_t req) {
            auto* data = (server_data_t*)user_data;
            a0_buf_t payload;
            REQUIRE(a0_packet_payload(req.pkt, &payload) == A0_OK);
            // fwrite(payload.ptr, sizeof(uint8_t), payload.size, stderr);

            a0_packet_t pkt;
            REQUIRE(a0_packet_build(0, nullptr, payload, data->alloc->get(), &pkt) == A0_OK);

            REQUIRE(a0_rpc_reply(data->server, req, pkt) == A0_OK);
          },
  };

  REQUIRE(a0_rpc_server_init(&server, request_shmobj, response_shmobj, alloc.get(), onrequest) ==
          A0_OK);

  a0_rpc_client_t client;
  REQUIRE(a0_rpc_client_init(&client, request_shmobj, response_shmobj, alloc.get()) == A0_OK);

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
    REQUIRE(a0_packet_build(0, nullptr, buf(a0::strutil::fmt("msg #%d", i)), alloc.get(), &pkt) ==
            A0_OK);
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
