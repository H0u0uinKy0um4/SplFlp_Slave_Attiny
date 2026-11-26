#pragma once

#include <stddef.h>
#include <stdint.h>

// One implementation of the application's wire identity: the compat number,
// its composed text, the persisted slot codec and validation.
class FirmwareIdentity {
public:
  FirmwareIdentity(uint8_t compat, const char* release);

  static FirmwareIdentity build();

  uint8_t compat() const { return compat_; }
  const char* wire() const { return wire_; }
  bool valid() const;

  // A blank or erased slot yields false: that is an application from before
  // version tracking, not a version whose name is empty.
  static bool decode(const uint8_t* slot, size_t size, char* out, size_t outSize);
  static size_t encode(const FirmwareIdentity& identity, uint8_t* out, size_t size);

private:
  uint8_t compat_;
  char wire_[8];
};
