#ifndef A0_UUID_H
#define A0_UUID_H

#ifdef __cplusplus
extern "C" {
#endif

// My kingdom for a constexpr.
#define A0_UUID_SIZE 37
typedef char a0_uuid_t[A0_UUID_SIZE];

void a0_uuidv4(a0_uuid_t out);

#ifdef __cplusplus
}
#endif

#endif  // A0_UUID_H
