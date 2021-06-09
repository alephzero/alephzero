<h1 align="center">
  <br>
  <img src="https://raw.githubusercontent.com/alephzero/logo/master/rendered/alephzero.svg" style="width: 300px">
  <br>
  AlephZero
</h1>

<h3 align="center">Fast, robust, broker-less IPC.</h3>

<p align="center">
  <a href="https://github.com/alephzero/alephzero/actions?query=workflow%3ACI"><img src="https://github.com/alephzero/alephzero/workflows/CI/badge.svg"></a>
  <a href="https://codecov.io/gh/alephzero/alephzero"><img src="https://codecov.io/gh/alephzero/alephzero/branch/master/graph/badge.svg"></a>
  <a href="https://alephzero.readthedocs.io/en/latest/?badge=latest"><img src="https://readthedocs.org/projects/alephzero/badge/?version=latest"></a>
  <a href="http://unlicense.org"><img src="https://img.shields.io/badge/license-Unlicense-blue.svg"></a>
</p>

<p align="center">
  <a href="#examples">Examples</a> •
  <a href="#key-features">Key Features</a> •
  <a href="#installation">Installation</a> •
  <a href="#deep-dive">Deep Dive</a>
</p>


## Examples

```cc
#include <a0.h>
```

### Publisher
```cc
a0::Publisher p(a0::File("my_pubsub_topic"));
p.pub("foo");
```

### Subscriber
```cc
a0::Subscriber sub(
    a0::File("my_pubsub_topic"),
    A0_INIT_AWAIT_NEW,  // or OLDEST or MOST_RECENT
    A0_ITER_NEXT,       // or NEWEST
    [](a0::PacketView pkt_view) {
      std::cout << "Got: " << pkt_view.payload() << std::endl;
    });
```

### RPC Server
```cc
a0::RpcServer server(
    a0::File("my_rpc_topic"),
    /* onrequest = */ [](a0::RpcRequest req) {
        std::cout << "Got: " << req.pkt().payload() << std::endl;
        req.reply("echo " + std::string(req.pkt().payload()));
    },
    /* oncancel = */ nullptr);
```

### RPC Client
```cc
a0::RpcClient client(a0::File("my_rpc_topic"));
client.send("client msg", [](a0::PacketView reply) {
  std::cout << "Got: " << reply.payload() << std::endl;
});
```


More example and an interactive experience at: https://github.com/alephzero/playground

## Key Features

### Shared Memory

TODO: Something

### Multiple modes of communication:

* PubSub
* RPC
* PRPC (Progressive RPC)
* Sessions (TODO)

TODO: Moar

### Robustness

TODO: Something

## Installation

### Install From Source

#### Ubuntu Dependencies

```sh
apt install g++ make
```

#### Alpine Dependencies

```sh
apk add g++ linux-headers make
```

#### Download And Install

```sh
git clone https://github.com/alephzero/alephzero.git
cd alephzero
make install -j
```

### Install From Package

Coming soon-ish. Let me know if you want this.

## Deep Dive

[Presentation from March 25, 2020]( https://docs.google.com/presentation/d/12KE9UucjZPtpVnM1NljxOqBolBBKECWJdrCoE2yJaBw/edit#slide=id.p)

TODO: Moar
