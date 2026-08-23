#include "Identity.h"
#include <string.h>
#include <ed_25519.h>

namespace mesh {

Identity::Identity() {
  memset(pub_key, 0, sizeof(pub_key));
}

Identity::Identity(const char* pub_hex) {
  Utils::fromHex(pub_key, PUB_KEY_SIZE, pub_hex);
}

bool Identity::verify(const uint8_t* sig, const uint8_t* message, int msg_len) const {
  // Real Ed25519 verification. This authenticates WHO sent a transmission (like a
  // callsign) -- it does not hide message content, so there's no Part 97 reason to
  // stub it out. Leaving this stubbed lets any station spoof any other station's
  // identity on the mesh.
  return ed25519_verify(sig, message, msg_len, pub_key) != 0;
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
  // Generate a REAL Ed25519 keypair. This is node identity/authentication, not
  // message encryption -- signatures don't obscure content, they just let other
  // stations verify who actually sent a transmission. A random, unrelated pub_key
  // (no matching private key) can't produce valid signatures at all.
  uint8_t seed[PRV_KEY_SIZE];
  rng->random(seed, PRV_KEY_SIZE);
  ed25519_create_keypair(pub_key, prv_key, seed);
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
  // Used by the BLE/CLI "export private key" commands, which deal in the raw
  // 64-byte private key alone (the public key is always derivable from it).
  if (max_len < PRV_KEY_SIZE) return 0;
  memcpy(dest, prv_key, PRV_KEY_SIZE);
  return PRV_KEY_SIZE;
}

void LocalIdentity::readFrom(const uint8_t* src, size_t len) {
  // Used by the BLE/CLI "import private key" commands: src is the raw 64-byte
  // private key only -- re-derive the matching public key from it, rather than
  // treating leading bytes of the private key as if they were a public key.
  if (len >= PRV_KEY_SIZE) {
    memcpy(prv_key, src, PRV_KEY_SIZE);
    ed25519_derive_pub(pub_key, prv_key);
  }
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t* message, int msg_len) const {
  // Real Ed25519 signature -- authenticates this node's transmissions without
  // hiding their content (message bytes are unchanged, sent in the clear either way).
  ed25519_sign(sig, message, msg_len, pub_key, prv_key);
}

} // namespace mesh
