#include "FirmwareIdentity.h"
#include "FirmwareVersion.h"

#include <string.h>

FirmwareIdentity::FirmwareIdentity(uint8_t compat, const char* release)
  : compat_(compat), wire_() {
  uint8_t index = 0;
  uint8_t value = compat;
  if (value >= 10) {
    wire_[index++] = '?';
  } else {
    if (value != 0) wire_[index++] = (char)('0' + value);
  }
  if (index < sizeof(wire_) - 1) wire_[index++] = '.';

  const size_t length = release == 0 ? 0 : strlen(release);
  for (size_t i = 0; i < length && index < sizeof(wire_) - 1; ++i)
    wire_[index++] = release[i];
  wire_[index] = '\0';
}

FirmwareIdentity FirmwareIdentity::build() {
  return FirmwareIdentity((uint8_t)FW_BUS_COMPAT, FW_RELEASE);
}

bool FirmwareIdentity::valid() const {
  return compat_ >= 1 && compat_ <= 9 &&
         strlen(wire_) > 2 && strlen(wire_) <= 7 &&
         wire_[1] == '.' && wire_[0] == (char)('0' + compat_);
}

bool FirmwareIdentity::decode(const uint8_t* slot, size_t size,
                              char* out, size_t outSize) {
  if (slot == 0 || out == 0 || outSize == 0) return false;
  size_t length = 0;
  for (size_t i = 0; i < size && length + 1 < outSize; ++i) {
    uint8_t c = slot[i];
    if (c == 0x00 || c == 0xFF) break;
    out[length++] = (char)c;
  }
  out[length] = '\0';
  return length != 0;
}

size_t FirmwareIdentity::encode(const FirmwareIdentity& identity,
                                uint8_t* out, size_t size) {
  if (out == 0) return 0;
  const char* text = identity.wire();
  size_t length = strlen(text);
  for (size_t i = 0; i < size; ++i)
    out[i] = i < length ? (uint8_t)text[i] : 0x00;
  return size;
}
