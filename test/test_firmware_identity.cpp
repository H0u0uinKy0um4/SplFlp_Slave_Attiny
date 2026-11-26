// Host tests for the firmware identity module. Compile with:
//   g++ -std=c++11 -Wall -Werror -Iinclude test/test_firmware_identity.cpp
//     src/FirmwareIdentity.cpp -o /tmp/test_firmware_identity
//   /tmp/test_firmware_identity

#include "FirmwareIdentity.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  const FirmwareIdentity built = FirmwareIdentity::build();
  assert(built.compat() == 1);
  assert(std::strcmp(built.wire(), "1.0.2") == 0);
  assert(built.valid());

  uint8_t slot[8];
  const size_t size = FirmwareIdentity::encode(built, slot, sizeof(slot));
  assert(size == sizeof(slot));
  assert(std::memcmp(slot, "1.0.2\0\0\0", 8) == 0);

  char text[9];
  assert(FirmwareIdentity::decode(slot, sizeof(slot), text, sizeof(text)));
  assert(std::strcmp(text, "1.0.2") == 0);

  uint8_t erased[8];
  std::memset(erased, 0xFF, sizeof(erased));
  assert(!FirmwareIdentity::decode(erased, sizeof(erased), text, sizeof(text)));
  assert(!FirmwareIdentity::decode(slot, 0, text, sizeof(text)));

  uint8_t shortSlot[8];
  std::memset(shortSlot, 0xFF, sizeof(shortSlot));
  std::memcpy(shortSlot, "1.0", 3);
  assert(FirmwareIdentity::decode(shortSlot, sizeof(shortSlot), text, sizeof(text)));
  assert(std::strcmp(text, "1.0") == 0);

  assert(!FirmwareIdentity(0, "0.1").valid());
  assert(!FirmwareIdentity(10, "0.1").valid());
  assert(!FirmwareIdentity(1, "").valid());

  std::printf("OK: firmware identity codec\n");
  return 0;
}
