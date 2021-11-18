#ifndef A0_UUID_H
#define A0_UUID_H

#ifdef __cplusplus
extern "C" {
#endif

enum { A0_UUID_SIZE = 36 };

// Add one for a null terminator.
typedef char a0_uuid_t[A0_UUID_SIZE + 1];

void a0_uuidv4(a0_uuid_t out);

#ifdef __cplusplus
}
#endif

#endif  // A0_UUID_H
