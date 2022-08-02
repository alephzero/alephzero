#include <a0/file.h>
#include <a0/latch.h>
#include <a0/packet.hpp>
#include <a0/prpc.h>
#include <a0/prpc.hpp>
#include <a0/string_view.hpp>

#include <doctest.h>

#include <functional>
#include <ostream>
#include <string>

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

TEST_CASE_FIXTURE(PrpcFixture, "prpc] cpp basic") {
  struct data_t {
    a0_latch_t msg_latch;
    a0_latch_t done_latch;
  } data{};
  a0_latch_init(&data.msg_latch, 5);
  a0_latch_init(&data.done_latch, 1);

  auto onconnect = [&](a0::PrpcConnection conn) {
    REQUIRE(conn.pkt().payload() == "connect");
    conn.send("progress", false);
    conn.send("progress", false);
    conn.send("progress", false);
    conn.send("progress", false);
    conn.send("progress", true);
  };

  a0::PrpcServer server("test", onconnect);

  a0::PrpcClient client("test");

  client.connect("connect", [&](a0::Packet, bool done) {
    a0_latch_count_down(&data.msg_latch, 1);
    if (done) {
      a0_latch_count_down(&data.done_latch, 1);
    }
  });

  a0_latch_wait(&data.msg_latch);
  a0_latch_wait(&data.done_latch);
}

TEST_CASE_FIXTURE(PrpcFixture, "prpc] cpp cancel") {
  struct data_t {
    a0_latch_t msg_latch;
    a0_latch_t cancel_latch;
  } data{};
  a0_latch_init(&data.msg_latch, 1);
  a0_latch_init(&data.cancel_latch, 1);

  auto onconnect = [&](a0::PrpcConnection conn) {
    if (conn.pkt().payload() == "connect") {
      conn.send("progress", false);
    }
  };

  auto oncancel = [&](a0::string_view) {
    a0_latch_count_down(&data.cancel_latch, 1);
  };

  a0::PrpcServer server("test", onconnect, oncancel);

  a0::PrpcClient client("test");

  a0::Packet conn("connect");
  client.connect(conn, [&](a0::Packet, bool) {
    a0_latch_count_down(&data.msg_latch, 1);
  });

  a0_latch_wait(&data.msg_latch);

  client.cancel(conn.id());

  a0_latch_wait(&data.cancel_latch);
}
