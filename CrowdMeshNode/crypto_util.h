#pragma once

#include <Arduino.h>

static const size_t CRYPTO_PK_LEN = 32;
static const size_t CRYPTO_SK_LEN = 32;
static const size_t CRYPTO_NONCE_LEN = 12;
static const size_t CRYPTO_TAG_LEN = 16;
static const size_t CRYPTO_OVERHEAD = CRYPTO_NONCE_LEN + CRYPTO_TAG_LEN; // 28
static const size_t CRYPTO_MAX_PLAIN = 180 - CRYPTO_OVERHEAD;            // 152

/** Load or generate local X25519 identity (NVS). Call once at boot. */
bool cryptoIdentityBegin();

/** 32-byte local public key (never null after begin). */
const uint8_t* cryptoPublicKey();

/** Hex (64 chars + NUL) of local public key. */
void cryptoPublicKeyHex(char out[65]);

/** Remember a peer's X25519 public key (overwrites by MAC). */
bool cryptoRememberPeer(const uint8_t mac[6], const uint8_t pk[32]);

/** Parse 64-hex pubkey and remember peer. */
bool cryptoRememberPeerHex(const uint8_t mac[6], const char* pkHex64);

bool cryptoHasPeerKey(const uint8_t mac[6]);

/**
 * Encrypt plaintext for destMac.
 * outPayload must be >= CRYPTO_OVERHEAD + plainLen.
 * Writes nonce|ciphertext|tag, sets *outLen.
 * aad/aadLen authenticate the mesh header (through payloadLen).
 */
bool cryptoSeal(
    const uint8_t destMac[6],
    const uint8_t* aad, size_t aadLen,
    const uint8_t* plain, size_t plainLen,
    uint8_t* outPayload, size_t* outLen
);

/**
 * Decrypt payload from originMac.
 * plainOut capacity >= cipherLen (payloadLen - OVERHEAD).
 */
bool cryptoOpen(
    const uint8_t originMac[6],
    const uint8_t* aad, size_t aadLen,
    const uint8_t* payload, size_t payloadLen,
    uint8_t* plainOut, size_t* plainLen
);

/** Helpers */
bool cryptoParseHex(const char* hex, uint8_t* out, size_t outLen);
void cryptoToHex(const uint8_t* in, size_t len, char* outHex /* 2*len+1 */);
