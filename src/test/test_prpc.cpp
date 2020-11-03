#include <a0/arena.h>
#include <a0/common.h>
#include <a0/packet.h>
#include <a0/prpc.h>
#include <a0/uuid.h>

#include <doctest.h>

#include <condition_variable>
#include <cstring>
#include <memory>

#include "src/sync.hpp"
#include "src/test_util.hpp"

static const char TEST_FILE[] = "test.file";

struct PrpcFixture {
  a0_file_t file;

  PrpcFixture() {
    a0_file_remove(TEST_FILE);

    a0_file_open(TEST_FILE, nullptr, &file);
  }

  ~PrpcFixture() {
    a0_file_close(&file);
    a0_file_remove(TEST_FILE);
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
            if (!strcmp((const char*)conn.pkt.payload.ptr, "reply")) {
              a0_packet_t pkt;
              a0_packet_init(&pkt);
              pkt.payload = conn.pkt.payload;
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, true));
            }
          },
  };

  a0_prpc_server_t server;
  REQUIRE_OK(a0_prpc_server_init(&server, file.arena, a0::test::allocator(), onconnect, {}));

  a0_prpc_client_t client;
  REQUIRE_OK(a0_prpc_client_init(&client, file.arena, a0::test::allocator()));

  a0_prpc_callback_t onmsg = {
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

  a0_packet_t req;
  a0_packet_init(&req);
  req.payload = a0::test::buf("reply");
  REQUIRE_OK(a0_prpc_connect(&client, req, onmsg));

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
            if (!strcmp((const char*)conn.pkt.payload.ptr, "reply")) {
              a0_packet_t pkt;
              a0_packet_init(&pkt);
              pkt.payload = conn.pkt.payload;
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
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
  REQUIRE_OK(a0_prpc_server_init(&server, file.arena, a0::test::allocator(), onconnect, oncancel));

  a0_prpc_client_t client;
  REQUIRE_OK(a0_prpc_client_init(&client, file.arena, a0::test::allocator()));

  a0_prpc_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t, bool) {
            auto* data = (a0::sync<data_t>*)user_data;
            data->notify_all([](auto* data_) {
              data_->msg_cnt++;
            });
          },
  };

  a0_packet_t conn;
  a0_packet_init(&conn);
  conn.payload = a0::test::buf("reply");
  REQUIRE_OK(a0_prpc_connect(&client, conn, onmsg));

  data.wait([](auto* data_) {
    return data_->msg_cnt >= 1;
  });

  a0_prpc_cancel(&client, conn.id);

  data.wait([](auto* data_) {
    return data_->cancel_cnt >= 1;
  });

  REQUIRE_OK(a0_prpc_client_close(&client));
  REQUIRE_OK(a0_prpc_server_close(&server));
}

TEST_CASE_FIXTURE(PrpcFixture, "prpc] server async close") {
  struct data_t {
    size_t msg_cnt;
    size_t done_cnt;
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

  a0_prpc_connection_callback_t onconnect = {
      .user_data = &onclose,
      .fn =
          [](void* data, a0_prpc_connection_t conn) {
            if (!strcmp((const char*)conn.pkt.payload.ptr, "reply")) {
              a0_packet_t pkt;
              a0_packet_init(&pkt);
              pkt.payload = conn.pkt.payload;
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));

              REQUIRE_OK(a0_prpc_server_async_close(conn.server, *(a0_callback_t*)data));

              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, true));
            }
          },
  };

  a0_prpc_server_t server;
  REQUIRE_OK(a0_prpc_server_init(&server, file.arena, a0::test::allocator(), onconnect, {}));

  a0_prpc_client_t client;
  REQUIRE_OK(a0_prpc_client_init(&client, file.arena, a0::test::allocator()));

  a0_prpc_callback_t onmsg = {
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

  a0_packet_t req;
  a0_packet_init(&req);
  req.payload = a0::test::buf("reply");
  REQUIRE_OK(a0_prpc_connect(&client, req, onmsg));

  data.wait([](auto* data_) {
    return data_->msg_cnt >= 5 && data_->done_cnt >= 1 && data_->close_cnt >= 1;
  });

  REQUIRE_OK(a0_prpc_client_close(&client));
}

TEST_CASE_FIXTURE(PrpcFixture, "prpc] client async close") {
  a0_prpc_connection_callback_t onconnect = {
      .user_data = nullptr,
      .fn =
          [](void*, a0_prpc_connection_t conn) {
            if (!strcmp((const char*)conn.pkt.payload.ptr, "reply")) {
              a0_packet_t pkt;
              a0_packet_init(&pkt);
              pkt.payload = conn.pkt.payload;
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, false));
              REQUIRE_OK(a0_prpc_send(conn, pkt, true));
            }
          },
  };

  a0_prpc_server_t server;
  REQUIRE_OK(a0_prpc_server_init(&server, file.arena, a0::test::allocator(), onconnect, {}));

  a0_prpc_client_t client;
  REQUIRE_OK(a0_prpc_client_init(&client, file.arena, a0::test::allocator()));

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
    a0_prpc_client_t* client;
  } data{onclose, &client};

  a0_prpc_callback_t onmsg = {
      .user_data = &data,
      .fn =
          [](void* user_data, a0_packet_t, bool done) {
            auto* data = (data_t*)user_data;
            if (done) {
              REQUIRE_OK(a0_prpc_client_async_close(data->client, data->onclose));
            }
          },
  };

  a0_packet_t req;
  a0_packet_init(&req);
  req.payload = a0::test::buf("reply");
  REQUIRE_OK(a0_prpc_connect(&client, req, onmsg));

  close_event.wait();

  REQUIRE_OK(a0_prpc_server_close(&server));
}
