#include <a0/node.h>

#include <stdlib.h>

const char* a0_node() {
  return getenv("A0_NODE");
}
