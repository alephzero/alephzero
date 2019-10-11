#include <a0/alephzero.hpp>

#include <doctest.h>
#include <errno.h>
#include <math.h>

#include "src/test_util.hpp"

static const char kTestShm[] = "/test.shm";

struct CppPubsubFixture {
  a0::Shm shm;

  CppPubsubFixture() {
    a0::Shm::unlink(kTestShm);

    shm = a0::Shm(kTestShm, a0::Shm::Options{.size = 16 * 1024 * 1024});
  }

  ~CppPubsubFixture() {
    a0::Shm::unlink(kTestShm);
  }
};

TEST_CASE_FIXTURE(CppPubsubFixture, "Test pubsub sync") {
  {
    a0::Publisher p(shm);
    p.pub("msg #0");
    p.pub("msg #1");
  }

  {
    a0::SubscriberSync sub(shm, A0_INIT_OLDEST, A0_ITER_NEXT);

    REQUIRE(sub.has_next());
    auto pkt_view = sub.next();

    REQUIRE(pkt_view.num_headers() == 2);

    std::map<std::string, std::string> hdrs;
    for (size_t i = 0; i < pkt_view.num_headers(); i++) {
      auto hdr = pkt_view.header(i);
      hdrs[std::string(hdr.first)] = hdr.second;
    }
    REQUIRE(hdrs.count("a0_id"));
    REQUIRE(hdrs.count("a0_clock"));

    REQUIRE(pkt_view.payload() == "msg #0");

    REQUIRE(sub.has_next());
    REQUIRE(sub.next().payload() == "msg #1");

    REQUIRE(!sub.has_next());
  }

  {
    a0::SubscriberSync sub(shm, A0_INIT_MOST_RECENT, A0_ITER_NEWEST);

    REQUIRE(sub.has_next());
    REQUIRE(sub.next().payload() == "msg #1");

    REQUIRE(!sub.has_next());
  }
}
