#include "Identity.h"
#include <string.h>
#define ED25519_NO_SEED  1
#include <ed_25519.h>
#include <Ed25519.h>

#ifdef USE_CC310_HW_CRYPTO
#include <Adafruit_nRFCrypto.h>
#include "nrf_cc310/include/crys_ec_edw_api.h"
#endif

namespace mesh {

Identity::Identity() {
  memset(pub_key, 0, sizeof(pub_key));
}

Identity::Identity(const char* pub_hex) {
  Utils::fromHex(pub_key, PUB_KEY_SIZE, pub_hex);
}

bool Identity::verify(const uint8_t* sig, const uint8_t* message, int msg_len) const {
#ifdef USE_CC310_HW_CRYPTO
  // nRF52840 CryptoCell CC310 hardware Ed25519 verification. The software
  // implementations need ~3KB of stack (which can overflow the Adafruit core's
  // 4KB loop task stack from the advert receive path); the hardware path
  // needs much less, around 600-700bytes. The CC310 workspace is static, faster,
  // should save power at scale as well.
  static CRYS_ECEDW_TempBuff_t cc310_tmp;
  CRYSError_t rc = CRYS_ECEDW_Verify((uint8_t*)sig, CRYS_ECEDW_SIGNATURE_BYTES,
                                     (uint8_t*)pub_key, CRYS_ECEDW_MOD_SIZE_IN_BYTES,
                                     (uint8_t*)message, (size_t)msg_len, &cc310_tmp);
  return rc == CRYS_OK;
#elif 0
  // NOTE: memory corruption bug was found in this function!!
  return ed25519_verify(sig, message, msg_len, pub_key);
#else
  return Ed25519::verify(sig, this->pub_key, message, msg_len);
#endif
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
  uint8_t seed[SEED_SIZE];
  rng->random(seed, SEED_SIZE);
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
  if (max_len < PRV_KEY_SIZE) return 0;  // not big enough

  if (max_len < PRV_KEY_SIZE + PUB_KEY_SIZE) {  // only room for prv_key
    memcpy(dest, prv_key, PRV_KEY_SIZE);
    return PRV_KEY_SIZE;
  }
  memcpy(dest, prv_key, PRV_KEY_SIZE);  // otherwise can fit prv + pub keys
  memcpy(&dest[PRV_KEY_SIZE], pub_key, PUB_KEY_SIZE);
  return PRV_KEY_SIZE + PUB_KEY_SIZE;
}

void LocalIdentity::readFrom(const uint8_t* src, size_t len) {
  if (len == PRV_KEY_SIZE + PUB_KEY_SIZE) {  // has prv + pub keys
    memcpy(prv_key, src, PRV_KEY_SIZE);
    memcpy(pub_key, &src[PRV_KEY_SIZE], PUB_KEY_SIZE);
  } else if (len == PRV_KEY_SIZE) {
    memcpy(prv_key, src, PRV_KEY_SIZE);
    // now need to re-calculate the pub_key
    ed25519_derive_pub(pub_key, prv_key);
  }
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t* message, int msg_len) const {
  ed25519_sign(sig, message, msg_len, pub_key, prv_key);
}

} // namespace mesh
