// Copyright (c) 2014-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_COMMON_H
#define BITCOIN_CRYPTO_COMMON_H

#include <tapyrus-config.h>

#include <stdint.h>
#include <string.h>

#include <compat/endian.h>

uint16_t static inline ReadLE16(const unsigned char* ptr)
{
    uint16_t x;
    memcpy((char*)&x, ptr, 2);
    return le16toh_internal(x);
}

uint32_t static inline ReadLE32(const unsigned char* ptr)
{
    uint32_t x;
    memcpy((char*)&x, ptr, 4);
    return le32toh_internal(x);
}

uint64_t static inline ReadLE64(const unsigned char* ptr)
{
    uint64_t x;
    memcpy((char*)&x, ptr, 8);
    return le64toh_internal(x);
}

void static inline WriteLE16(unsigned char* ptr, uint16_t x)
{
    uint16_t v = htole16_internal(x);
    memcpy(ptr, (char*)&v, 2);
}

void static inline WriteLE32(unsigned char* ptr, uint32_t x)
{
    uint32_t v = htole32_internal(x);
    memcpy(ptr, (char*)&v, 4);
}

void static inline WriteLE64(unsigned char* ptr, uint64_t x)
{
    uint64_t v = htole64_internal(x);
    memcpy(ptr, (char*)&v, 8);
}

uint32_t static inline ReadBE32(const unsigned char* ptr)
{
    uint32_t x;
    memcpy((char*)&x, ptr, 4);
    return be32toh_internal(x);
}

uint64_t static inline ReadBE64(const unsigned char* ptr)
{
    uint64_t x;
    memcpy((char*)&x, ptr, 8);
    return be64toh_internal(x);
}

void static inline WriteBE32(unsigned char* ptr, uint32_t x)
{
    uint32_t v = htobe32_internal(x);
    memcpy(ptr, (char*)&v, 4);
}

void static inline WriteBE64(unsigned char* ptr, uint64_t x)
{
    uint64_t v = htobe64_internal(x);
    memcpy(ptr, (char*)&v, 8);
}

/** Compute the smallest power of two that is larger than val. */
template<typename I>
static inline int CountBits(I val, int max = 64) {
#if defined(__cpp_lib_int_pow2) && __cpp_lib_int_pow2 >= 202002L
    // c++20 impl
    (void)max;
    return std::bit_width(static_cast<std::make_unsigned_t<I>>(val));
#elif defined(_MSC_VER)
    (void)max;
    unsigned long index;
    unsigned char ret;
    if (std::numeric_limits<I>::digits <= 32) {
        ret = _BitScanReverse(&index, val);
    } else {
        ret = _BitScanReverse64(&index, val);
    }
    if (!ret) return 0;
    return index + 1;
#elif defined(HAVE_CLZ)
    (void)max;
    if (val == 0) return 0;
    if (std::numeric_limits<unsigned>::digits >= std::numeric_limits<I>::digits) {
        return std::numeric_limits<unsigned>::digits - __builtin_clz(val);
    } else if (std::numeric_limits<unsigned long>::digits >= std::numeric_limits<I>::digits) {
        return std::numeric_limits<unsigned long>::digits - __builtin_clzl(val);
    } else {
        return std::numeric_limits<unsigned long long>::digits - __builtin_clzll(val);
    }
#else
    while (max && (val >> (max - 1) == 0)) --max;
    return max;
#endif
}

#endif // BITCOIN_CRYPTO_COMMON_H
