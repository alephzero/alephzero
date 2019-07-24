#include <a0/b64.h>

#include <cheat.h>
#include <cheats.h>


CHEAT_TEST(test_b64,
  cheat_assert(true);  // https://github.com/Tuplanolla/cheat/issues/6

  uint8_t src[] = "Hello, World!";

  uint8_t* encoded;
  size_t encoded_size;
  cheat_assert_int(b64_encode(src, sizeof(src), &encoded, &encoded_size), A0_OK);

  cheat_assert_string("SGVsbG8sIFdvcmxkIQA=", (char*)encoded);

  uint8_t* decoded;
  size_t decoded_size;
  cheat_assert_int(b64_decode(encoded, encoded_size, &decoded, &decoded_size), A0_OK);
  cheat_assert_string((char*)src, (char*)decoded);
)
