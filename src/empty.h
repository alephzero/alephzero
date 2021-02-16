#ifndef A0_SRC_EMPTY_H
#define A0_SRC_EMPTY_H

// Bah. Why is there no consistent way to zero initialize a struct?
#ifdef __cplusplus
#define A0_EMPTY \
  {}
#else
#define A0_EMPTY \
  { 0 }
#endif

#endif  // A0_SRC_EMPTY_H
