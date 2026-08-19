#include "Identity.h"
#include <string.h>

namespace mesh {

Identity::Identity() {
  memset(pub_key, 0, sizeof(pub_key));
}

Identity::Identity(const char* pub_hex) {
  Utils::fromHex(pub_key, PUB_KEY_SIZE, pub_hex);
}

bool Identity::verify(const uint8_t* sig, const uint8_t* message, int msg_len) const {
  // Stubbed for plain/unencrypted ham operation
  return true;
}

bool Identity::readFrom(Stream& s) {
  return (s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
}

bool Identity::writeTo(Stream& s) const {
  return (s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
}

void Identity::printTo(Stream& s) const {
  Utils::printHex(s, pub_key, PUB_KEY_SIZE);
}

LocalIdentity::LocalIdentity() {
  memset(prv_key, 0, sizeof(prv_key));
}

LocalIdentity::LocalIdentity(const char* prv_hex, const char* pub_hex) : Identity(pub_hex) {
  Utils::fromHex(prv_key, PRV_KEY_SIZE, prv_hex);
}

LocalIdentity::LocalIdentity(RNG* rng) {
  // Generate pseudo-random node identifiers instead of Ed25519 keypairs
  rng->random(pub_key, PUB_KEY_SIZE);
  memset(prv_key, 0, sizeof(prv_key));
}

bool LocalIdentity::readFrom(Stream& s) {
  bool success = (s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
  success = success && (s.readBytes(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE);
  return success;
}

bool LocalIdentity::writeTo(Stream& s) const {
  bool success = (s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
  success = success && (s.write(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE);
  return success;
}

void LocalIdentity::printTo(Stream& s) const {
  s.print("pub_key: "); Utils::printHex(s, pub_key, PUB_KEY_SIZE); s.println();
  s.print("prv_key: "); Utils::printHex(s, prv_key, PRV_KEY_SIZE); s.println();
}

size_t LocalIdentity::writeTo(uint8_t* dest, size_t max_len) {
  if (max_len < PRV_KEY_SIZE) return 0;

  if (max_len < PRV_KEY_SIZE + PUB_KEY_SIZE) {
    memcpy(dest, prv_key, PRV_KEY_SIZE);
    return PRV_KEY_SIZE;
  }
  memcpy(dest, prv_key, PRV_KEY_SIZE);
  memcpy(&dest[PRV_KEY_SIZE], pub_key, PUB_KEY_SIZE);
  return PRV_KEY_SIZE + PUB_KEY_SIZE;
}

void LocalIdentity::readFrom(const uint8_t* src, size_t len) {
  if (len == PRV_KEY_SIZE + PUB_KEY_SIZE) {
    memcpy(prv_key, src, PRV_KEY_SIZE);
    memcpy(pub_key, &src[PRV_KEY_SIZE], PUB_KEY_SIZE);
  } else if (len == PRV_KEY_SIZE) {
    memcpy(prv_key, src, PRV_KEY_SIZE);
  }
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t* message, int msg_len) const {
  // Fill signature buffer with dummy bytes (no Ed25519 calculation)
  memset(sig, 0, 64);
}

} // namespace mesh
