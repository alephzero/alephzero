#ifndef A0_MACROS_H
#define A0_MACROS_H

#define A0_CAT(a, b) A0_CAT_(a, b)
#define A0_CAT_(a, b) a ## b

#define A0_LIKELY(x) __builtin_expect((x), 1)
#define A0_UNLIKELY(x) __builtin_expect((x), 0)

#endif  // A0_MACROS_H
