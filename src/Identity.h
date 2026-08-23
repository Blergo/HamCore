#pragma once

#include <Utils.h>
#include <Stream.h>
#include <ed_25519.h>

namespace mesh {

/**
 * \brief  An identity in the mesh, with given Ed25519 public key, ie. a party whose signatures can be VERIFIED.
*/
class Identity {
public:
  uint8_t pub_key[PUB_KEY_SIZE];

  Identity();
  Identity(const char* pub_hex);
  Identity(const uint8_t* _pub) { memcpy(pub_key, _pub, PUB_KEY_SIZE); }

  int copyHashTo(uint8_t* dest) const { 
    memcpy(dest, pub_key, PATH_HASH_SIZE);    // hash is just prefix of pub_key
    return PATH_HASH_SIZE;
  }
  int copyHashTo(uint8_t* dest, uint8_t len) const { 
    memcpy(dest, pub_key, len);    // hash is just prefix of pub_key
    return len;
  }
  bool isHashMatch(const uint8_t* hash) const {
    return memcmp(hash, pub_key, PATH_HASH_SIZE) == 0;
  }
  bool isHashMatch(const uint8_t* hash, uint8_t len) const {
    return memcmp(hash, pub_key, len) == 0;
  }

  /**
   * \brief  Performs real Ed25519 signature verification. This authenticates
   *         which station sent a transmission -- it has no effect on whether
   *         the message content itself is readable, so it's kept for ham use.
   * \param sig IN - signature buffer.
   * \param message IN - the original message.
   * \param msg_len IN - the length in bytes of message.
   * \returns true if the signature is valid for this identity's public key.
  */
  bool verify(const uint8_t* sig, const uint8_t* message, int msg_len) const;

  bool matches(const Identity& other) const { return memcmp(pub_key, other.pub_key, PUB_KEY_SIZE) == 0; }
  bool matches(const uint8_t* other_pubkey) const { return memcmp(pub_key, other_pubkey, PUB_KEY_SIZE) == 0; }

  bool readFrom(Stream& s);
  bool writeTo(Stream& s) const;
  void printTo(Stream& s) const;
};

/**
 * \brief  An Identity generated on THIS device.
*/
class LocalIdentity : public Identity {
  uint8_t prv_key[PRV_KEY_SIZE];
public:
  LocalIdentity();
  LocalIdentity(const char* prv_hex, const char* pub_hex);
  LocalIdentity(RNG* rng);   // create new random

  /**
   * \brief  Validates an imported private key by re-deriving its public key
   *         and checking it's non-zero. A real check, not just a null check --
   *         this only gates whether an imported key is well-formed, it has no
   *         bearing on message confidentiality.
  */
  static bool validatePrivateKey(const uint8_t* prv_key) {
    if (prv_key == nullptr) return false;
    uint8_t derived_pub[PUB_KEY_SIZE];
    ed25519_derive_pub(derived_pub, prv_key);
    uint8_t zero[PUB_KEY_SIZE];
    memset(zero, 0, PUB_KEY_SIZE);
    return memcmp(derived_pub, zero, PUB_KEY_SIZE) != 0;
  }

  /**
   * \brief  Real Ed25519 signature -- authenticates which node sent a message,
   *         without hiding the message content itself.
   * \param sig OUT - buffer of at least SIGNATURE_SIZE bytes.
   * \param message IN - the raw message bytes.
   * \param msg_len IN - length of message.
  */
  void sign(uint8_t* sig, const uint8_t* message, int msg_len) const;

  bool readFrom(Stream& s);
  bool writeTo(Stream& s) const;
  void printTo(Stream& s) const;
  size_t writeTo(uint8_t* dest, size_t max_len);
  void readFrom(const uint8_t* src, size_t len);
};

} // namespace mesh
