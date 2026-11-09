#include "crypto_util.h"

#include <Preferences.h>
#include <string.h>

#include <esp_random.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/error.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>

static const char* HKDF_INFO = "CrowdMesh-v2";
static const uint8_t HKDF_SALT[8] = { 'C', 'r', 'o', 'w', 'd', 'M', 'e', 's' };

static const int MAX_PEER_KEYS = 16;

struct PeerKey {
  uint8_t mac[6];
  uint8_t pk[CRYPTO_PK_LEN];
  bool used;
};

static Preferences gCryptoPrefs;
static uint8_t gSk[CRYPTO_SK_LEN];
static uint8_t gPk[CRYPTO_PK_LEN];
static bool gReady = false;
static PeerKey gPeers[MAX_PEER_KEYS];

static int rngCb(void* /*ctx*/, unsigned char* out, size_t len) {
  esp_fill_random(out, len);
  return 0;
}

static bool macEq(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

bool cryptoParseHex(const char* hex, uint8_t* out, size_t outLen) {
  if (hex == nullptr || out == nullptr) return false;
  size_t n = strlen(hex);
  if (n != outLen * 2) return false;
  for (size_t i = 0; i < outLen; i++) {
    char b[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
    char* end = nullptr;
    unsigned long v = strtoul(b, &end, 16);
    if (end == b) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

void cryptoToHex(const uint8_t* in, size_t len, char* outHex) {
  static const char* digits = "0123456789ABCDEF";
  for (size_t i = 0; i < len; i++) {
    outHex[i * 2] = digits[(in[i] >> 4) & 0xF];
    outHex[i * 2 + 1] = digits[in[i] & 0xF];
  }
  outHex[len * 2] = '\0';
}

static bool x25519DerivePublic(const uint8_t sk[32], uint8_t pk[32]) {
  mbedtls_ecp_group grp;
  mbedtls_mpi d;
  mbedtls_ecp_point Q;
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_ecp_point_init(&Q);

  int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
  if (ret != 0) {
    Serial.printf("[crypto] ecp_group_load=%d\n", ret);
    goto fail;
  }

  // Curve25519 scalar is little-endian in mbedtls.
  ret = mbedtls_mpi_read_binary_le(&d, sk, 32);
  if (ret != 0) goto fail;

  ret = mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, rngCb, nullptr);
  if (ret != 0) {
    Serial.printf("[crypto] ecp_mul=%d\n", ret);
    goto fail;
  }

  {
    size_t olen = 0;
    ret = mbedtls_ecp_point_write_binary(
        &grp, &Q, MBEDTLS_ECP_PF_COMPRESSED, &olen, pk, 32);
    if (ret != 0 || olen != 32) {
      // Some builds write 33 bytes with a prefix — try uncompressed/raw X.
      unsigned char buf[33];
      olen = 0;
      ret = mbedtls_ecp_point_write_binary(
          &grp, &Q, MBEDTLS_ECP_PF_COMPRESSED, &olen, buf, sizeof(buf));
      if (ret == 0 && olen == 33) {
        memcpy(pk, buf + 1, 32);
      } else if (ret == 0 && olen == 32) {
        memcpy(pk, buf, 32);
      } else {
        Serial.printf("[crypto] write_binary ret=%d olen=%u\n", ret, (unsigned)olen);
        goto fail;
      }
    }
  }

  mbedtls_ecp_point_free(&Q);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  return true;

fail:
  mbedtls_ecp_point_free(&Q);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  return false;
}

static bool x25519Shared(
    const uint8_t sk[32],
    const uint8_t peerPk[32],
    uint8_t shared[32]
) {
  mbedtls_ecp_group grp;
  mbedtls_mpi d;
  mbedtls_ecp_point Qp;
  mbedtls_ecp_point z;
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_ecp_point_init(&Qp);
  mbedtls_ecp_point_init(&z);

  int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
  if (ret != 0) goto fail;

  ret = mbedtls_mpi_read_binary_le(&d, sk, 32);
  if (ret != 0) goto fail;

  ret = mbedtls_ecp_point_read_binary(&grp, &Qp, peerPk, 32);
  if (ret != 0) {
    // Try with 0x40 Montgomery prefix used by some stacks.
    uint8_t tmp[33];
    tmp[0] = 0x40;
    memcpy(tmp + 1, peerPk, 32);
    ret = mbedtls_ecp_point_read_binary(&grp, &Qp, tmp, 33);
    if (ret != 0) {
      Serial.printf("[crypto] read peer pk=%d\n", ret);
      goto fail;
    }
  }

  ret = mbedtls_ecp_mul(&grp, &z, &d, &Qp, rngCb, nullptr);
  if (ret != 0) {
    Serial.printf("[crypto] shared mul=%d\n", ret);
    goto fail;
  }

  {
    size_t olen = 0;
    unsigned char buf[33];
    ret = mbedtls_ecp_point_write_binary(
        &grp, &z, MBEDTLS_ECP_PF_COMPRESSED, &olen, buf, sizeof(buf));
    if (ret != 0) goto fail;
    if (olen == 32) {
      memcpy(shared, buf, 32);
    } else if (olen == 33) {
      memcpy(shared, buf + 1, 32);
    } else {
      goto fail;
    }
  }

  mbedtls_ecp_point_free(&z);
  mbedtls_ecp_point_free(&Qp);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  return true;

fail:
  mbedtls_ecp_point_free(&z);
  mbedtls_ecp_point_free(&Qp);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  return false;
}

static bool deriveAesKey(const uint8_t shared[32], uint8_t outKey[32]) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (md == nullptr) return false;
  int ret = mbedtls_hkdf(
      md,
      HKDF_SALT, sizeof(HKDF_SALT),
      shared, 32,
      reinterpret_cast<const unsigned char*>(HKDF_INFO), strlen(HKDF_INFO),
      outKey, 32);
  return ret == 0;
}

static const uint8_t* findPeerPk(const uint8_t mac[6]) {
  for (int i = 0; i < MAX_PEER_KEYS; i++) {
    if (gPeers[i].used && macEq(gPeers[i].mac, mac)) {
      return gPeers[i].pk;
    }
  }
  return nullptr;
}

bool cryptoIdentityBegin() {
  memset(gPeers, 0, sizeof(gPeers));
  gCryptoPrefs.begin("cmcrypto", false);

  size_t skLen = gCryptoPrefs.getBytesLength("x25519_sk");
  if (skLen == CRYPTO_SK_LEN) {
    gCryptoPrefs.getBytes("x25519_sk", gSk, CRYPTO_SK_LEN);
  } else {
    esp_fill_random(gSk, CRYPTO_SK_LEN);
    // X25519 clamping
    gSk[0] &= 248;
    gSk[31] &= 127;
    gSk[31] |= 64;
    gCryptoPrefs.putBytes("x25519_sk", gSk, CRYPTO_SK_LEN);
    Serial.println("[crypto] generated new X25519 identity");
  }

  if (!x25519DerivePublic(gSk, gPk)) {
    Serial.println("[crypto] failed to derive public key");
    gReady = false;
    return false;
  }

  char hex[65];
  cryptoToHex(gPk, CRYPTO_PK_LEN, hex);
  Serial.printf("[crypto] pk=%s\n", hex);
  gReady = true;
  return true;
}

const uint8_t* cryptoPublicKey() {
  return gPk;
}

void cryptoPublicKeyHex(char out[65]) {
  cryptoToHex(gPk, CRYPTO_PK_LEN, out);
}

bool cryptoRememberPeer(const uint8_t mac[6], const uint8_t pk[32]) {
  if (mac == nullptr || pk == nullptr) return false;
  for (int i = 0; i < MAX_PEER_KEYS; i++) {
    if (gPeers[i].used && macEq(gPeers[i].mac, mac)) {
      memcpy(gPeers[i].pk, pk, CRYPTO_PK_LEN);
      return true;
    }
  }
  for (int i = 0; i < MAX_PEER_KEYS; i++) {
    if (!gPeers[i].used) {
      gPeers[i].used = true;
      memcpy(gPeers[i].mac, mac, 6);
      memcpy(gPeers[i].pk, pk, CRYPTO_PK_LEN);
      return true;
    }
  }
  // Evict slot 0
  gPeers[0].used = true;
  memcpy(gPeers[0].mac, mac, 6);
  memcpy(gPeers[0].pk, pk, CRYPTO_PK_LEN);
  return true;
}

bool cryptoRememberPeerHex(const uint8_t mac[6], const char* pkHex64) {
  uint8_t pk[CRYPTO_PK_LEN];
  if (!cryptoParseHex(pkHex64, pk, CRYPTO_PK_LEN)) return false;
  return cryptoRememberPeer(mac, pk);
}

bool cryptoHasPeerKey(const uint8_t mac[6]) {
  return findPeerPk(mac) != nullptr;
}

bool cryptoSeal(
    const uint8_t destMac[6],
    const uint8_t* aad, size_t aadLen,
    const uint8_t* plain, size_t plainLen,
    uint8_t* outPayload, size_t* outLen
) {
  if (!gReady || plain == nullptr || outPayload == nullptr || outLen == nullptr) {
    return false;
  }
  if (plainLen == 0 || plainLen > CRYPTO_MAX_PLAIN) return false;

  const uint8_t* peerPk = findPeerPk(destMac);
  if (peerPk == nullptr) {
    Serial.println("[crypto] seal: no peer key");
    return false;
  }

  uint8_t shared[32];
  if (!x25519Shared(gSk, peerPk, shared)) return false;

  uint8_t aesKey[32];
  if (!deriveAesKey(shared, aesKey)) return false;

  uint8_t nonce[CRYPTO_NONCE_LEN];
  esp_fill_random(nonce, CRYPTO_NONCE_LEN);

  uint8_t* cipher = outPayload + CRYPTO_NONCE_LEN;
  uint8_t* tag = outPayload + CRYPTO_NONCE_LEN + plainLen;
  memcpy(outPayload, nonce, CRYPTO_NONCE_LEN);

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aesKey, 256);
  if (ret != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }
  ret = mbedtls_gcm_crypt_and_tag(
      &gcm, MBEDTLS_GCM_ENCRYPT, plainLen,
      nonce, CRYPTO_NONCE_LEN,
      aad, aadLen,
      plain, cipher,
      CRYPTO_TAG_LEN, tag);
  mbedtls_gcm_free(&gcm);
  if (ret != 0) {
    Serial.printf("[crypto] gcm encrypt=%d\n", ret);
    return false;
  }

  *outLen = CRYPTO_NONCE_LEN + plainLen + CRYPTO_TAG_LEN;
  return true;
}

bool cryptoOpen(
    const uint8_t originMac[6],
    const uint8_t* aad, size_t aadLen,
    const uint8_t* payload, size_t payloadLen,
    uint8_t* plainOut, size_t* plainLen
) {
  if (!gReady || payload == nullptr || plainOut == nullptr || plainLen == nullptr) {
    return false;
  }
  if (payloadLen < CRYPTO_OVERHEAD) return false;

  const uint8_t* peerPk = findPeerPk(originMac);
  if (peerPk == nullptr) {
    Serial.println("[crypto] open: no peer key");
    return false;
  }

  size_t ctLen = payloadLen - CRYPTO_OVERHEAD;
  if (ctLen == 0 || ctLen > CRYPTO_MAX_PLAIN) return false;

  const uint8_t* nonce = payload;
  const uint8_t* cipher = payload + CRYPTO_NONCE_LEN;
  const uint8_t* tag = payload + CRYPTO_NONCE_LEN + ctLen;

  uint8_t shared[32];
  if (!x25519Shared(gSk, peerPk, shared)) return false;

  uint8_t aesKey[32];
  if (!deriveAesKey(shared, aesKey)) return false;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aesKey, 256);
  if (ret != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }
  ret = mbedtls_gcm_auth_decrypt(
      &gcm, ctLen,
      nonce, CRYPTO_NONCE_LEN,
      aad, aadLen,
      tag, CRYPTO_TAG_LEN,
      cipher, plainOut);
  mbedtls_gcm_free(&gcm);
  if (ret != 0) {
    Serial.printf("[crypto] gcm decrypt=%d\n", ret);
    return false;
  }

  *plainLen = ctLen;
  return true;
}
