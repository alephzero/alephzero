#ifndef A0_UNUSED_H
#define A0_UNUSED_H

#include <a0/inline.h>

A0_STATIC_INLINE
void _a0_unused(__attribute__((unused)) int _, ...) {}

#define A0_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#define A0_UNUSED(X) _a0_unused(0, (X))

#endif  // A0_UNUSED_H
