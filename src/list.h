#ifndef A0_INTERNAL_LIST_H
#define A0_INTERNAL_LIST_H

typedef struct a0_list_node_s a0_list_node_t;

struct a0_list_node_s {
  void* data;
  a0_list_node_t* next;
};

#endif  // A0_INTERNAL_LIST_H
