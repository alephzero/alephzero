#include <a0/arena.h>
#include <a0/common.h>
#include <a0/packet.h>
#include <a0/rpc.h>
#include <a0/uuid.h>

#include <doctest.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <thread>

#include "src/sync.hpp"
#include "src/test_util.hpp"

static const char TEST_FILE[] = "test.file";

struct RpcFixture {
  a0_file_t file;

  RpcFixture() {
    a0_file_remove(TEST_FILE);

    a0_file_open(TEST_FILE, nullptr, &file);
  }

  ~RpcFixture() {
    a0_file_close(&file);
    a0_file_remove(TEST_FILE);
  }
};

TEST_CASE_FIXTURE(RpcFixture, "rpc] basic") {
  struct data_t {
    size_t reply_cnt;
    size_t cancel_cnt;
  };
  a0::sync<data_t> data;

  a0_rpc_request_callback_t onrequest = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_rpc_request_t req) {
            if (!strcmp((const char*)req.pkt.payload.ptr, "reply")) {
              a0_packet_t resp;
              a0_packet_init(&resp);
              resp.payload = req.pkt.payload;
              REQUIRE_OK(a0_rpc_reply(req, resp));
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

  a0_rpc_server_t server;
  REQUIRE_OK(a0_rpc_server_init(&server, file.arena, a0::test::allocator(), onrequest, oncancel));

  a0_rpc_client_t client;
  REQUIRE_OK(a0_rpc_client_init(&client, file.arena, a0::test::allocator()));

  a0_packet_callback_t onreply = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t) {
            auto* data = (a0::sync<data_t>*)user_data;
            data->notify_all([](auto* data_) {
              data_->reply_cnt++;
            });
          },
  };

  for (int i = 0; i < 5; i++) {
    a0_packet_t req;
    a0_packet_init(&req);
    req.payload = a0::test::buf("reply");
    REQUIRE_OK(a0_rpc_send(&client, req, onreply));
  }

  for (int i = 0; i < 5; i++) {
    a0_packet_t req;
    a0_packet_init(&req);
    req.payload = a0::test::buf("dont't reply");
    REQUIRE_OK(a0_rpc_send(&client, req, onreply));
    REQUIRE_OK(a0_rpc_cancel(&client, req.id));
  }

  data.wait([](auto* data_) {
    return data_->reply_cnt >= 5 && data_->cancel_cnt >= 5;
  });

  REQUIRE_OK(a0_rpc_client_close(&client));
  REQUIRE_OK(a0_rpc_server_close(&server));
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] empty oncancel onreply") {
  a0_rpc_request_callback_t onrequest = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_rpc_request_t req) {
            a0_packet_t resp;
            a0_packet_init(&resp);
            resp.payload = req.pkt.payload;
            REQUIRE_OK(a0_rpc_reply(req, resp));
          },
  };

  a0_rpc_server_t server;
  REQUIRE_OK(a0_rpc_server_init(&server, file.arena, a0::test::allocator(), onrequest, {}));

  a0_rpc_client_t client;
  REQUIRE_OK(a0_rpc_client_init(&client, file.arena, a0::test::allocator()));

  for (int i = 0; i < 5; i++) {
    a0_packet_t req;
    a0_packet_init(&req);
    req.payload = a0::test::buf("msg");
    REQUIRE_OK(a0_rpc_send(&client, req, {}));
    a0_rpc_cancel(&client, req.id);
  }

  // TODO: Find a better way.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  REQUIRE_OK(a0_rpc_client_close(&client));
  REQUIRE_OK(a0_rpc_server_close(&server));
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] server async close") {
  struct data_t {
    size_t reply_cnt;
    size_t close_cnt;
  };
  a0::sync<data_t> data;

  a0_callback_t onclose = {
      .user_data = &data,
      .fn =
          [](void* user_data) {
            auto* data = (a0::sync<data_t>*)user_data;
            data->notify_all([](auto* data_) {
              data_->close_cnt++;
            });
          },
  };

  a0_rpc_request_callback_t onrequest = {
      .user_data = &onclose,
      .fn =
          [](void* data, a0_rpc_request_t req) {
            REQUIRE_OK(a0_rpc_server_async_close(req.server, *(a0_callback_t*)data));

            a0_packet_t resp;
            a0_packet_init(&resp);
            resp.payload = req.pkt.payload;
            REQUIRE_OK(a0_rpc_reply(req, resp));
          },
  };

  a0_rpc_server_t server;
  REQUIRE_OK(a0_rpc_server_init(&server, file.arena, a0::test::allocator(), onrequest, {}));

  a0_rpc_client_t client;
  REQUIRE_OK(a0_rpc_client_init(&client, file.arena, a0::test::allocator()));

  a0_packet_callback_t onreply = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t) {
            auto* data = (a0::sync<data_t>*)user_data;
            data->notify_all([](auto* data_) {
              data_->reply_cnt++;
            });
          },
  };

  {
    a0_packet_t req;
    a0_packet_init(&req);
    req.payload = a0::test::buf("msg");
    REQUIRE_OK(a0_rpc_send(&client, req, onreply));
  }

  data.wait([](auto* data_) {
    return data_->reply_cnt >= 1 && data_->close_cnt >= 1;
  });

  REQUIRE_OK(a0_rpc_client_close(&client));
}

TEST_CASE_FIXTURE(RpcFixture, "rpc] client async close") {
  a0_rpc_request_callback_t onrequest = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_rpc_request_t req) {
            a0_packet_t resp;
            a0_packet_init(&resp);
            resp.payload = req.pkt.payload;
            REQUIRE_OK(a0_rpc_reply(req, resp));
          },
  };

  a0_rpc_server_t server;
  REQUIRE_OK(a0_rpc_server_init(&server, file.arena, a0::test::allocator(), onrequest, {}));

  a0_rpc_client_t client;
  REQUIRE_OK(a0_rpc_client_init(&client, file.arena, a0::test::allocator()));

  a0::Event close_event;

  a0_callback_t onclose = {
      .user_data = &close_event,
      .fn =
          [](void* data) {
            ((a0::Event*)data)->set();
          },
  };

  struct data_t {
    a0_callback_t onclose;
    a0_rpc_client_t* client;
  } data{onclose, &client};

  a0_packet_callback_t onreply = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t) {
            auto* data = (data_t*)user_data;
            REQUIRE_OK(a0_rpc_client_async_close(data->client, data->onclose));
          },
  };

  {
    a0_packet_t req;
    a0_packet_init(&req);
    req.payload = a0::test::buf("msg");
    REQUIRE_OK(a0_rpc_send(&client, req, onreply));
  }

  close_event.wait();

  REQUIRE_OK(a0_rpc_server_close(&server));
}
