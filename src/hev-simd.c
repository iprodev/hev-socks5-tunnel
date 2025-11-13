/*
 ============================================================================
 Name        : hev-simd.c
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : SIMD Implementation
 ============================================================================
 */

#include <string.h>
#include "hev-simd.h"

#ifdef HEV_SIMD_AVX2
#include <immintrin.h>
#elif defined(HEV_SIMD_SSE2)
#include <emmintrin.h>
#elif defined(HEV_SIMD_NEON)
#include <arm_neon.h>
#endif

/* Scalar fallback for checksum */
static uint16_t
checksum_scalar(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    
    /* 16-bit words */
    while (len > 1) {
        sum += *(uint16_t *)data;
        data += 2;
        len -= 2;
    }
    
    /* Odd byte */
    if (len > 0)
        sum += *data;
    
    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    
    return (uint16_t)~sum;
}

#ifdef HEV_SIMD_AVX2
static uint16_t
checksum_avx2(const uint8_t *data, size_t len)
{
    __m256i sum = _mm256_setzero_si256();
    
    /* Process 32 bytes at a time */
    while (len >= 32) {
        __m256i chunk = _mm256_loadu_si256((__m256i *)data);
        
        /* Extend bytes to words and add */
        __m256i lo = _mm256_unpacklo_epi8(chunk, _mm256_setzero_si256());
        __m256i hi = _mm256_unpackhi_epi8(chunk, _mm256_setzero_si256());
        
        sum = _mm256_add_epi16(sum, lo);
        sum = _mm256_add_epi16(sum, hi);
        
        data += 32;
        len -= 32;
    }
    
    /* Horizontal sum */
    __m128i sum128 = _mm_add_epi16(
        _mm256_extracti128_si256(sum, 0),
        _mm256_extracti128_si256(sum, 1)
    );
    
    sum128 = _mm_add_epi16(sum128, _mm_srli_si128(sum128, 8));
    sum128 = _mm_add_epi16(sum128, _mm_srli_si128(sum128, 4));
    sum128 = _mm_add_epi16(sum128, _mm_srli_si128(sum128, 2));
    
    uint32_t result = _mm_extract_epi16(sum128, 0);
    
    /* Handle remaining bytes with scalar */
    if (len > 0) {
        uint16_t tail = checksum_scalar(data, len);
        result += tail;
    }
    
    /* Fold and invert */
    while (result >> 16)
        result = (result & 0xFFFF) + (result >> 16);
    
    return (uint16_t)~result;
}
#endif

#ifdef HEV_SIMD_NEON
static uint16_t
checksum_neon(const uint8_t *data, size_t len)
{
    uint32x4_t sum = vdupq_n_u32(0);
    
    /* Process 16 bytes at a time */
    while (len >= 16) {
        uint8x16_t chunk = vld1q_u8(data);
        
        /* Extend to 16-bit and add */
        uint16x8_t lo = vmovl_u8(vget_low_u8(chunk));
        uint16x8_t hi = vmovl_u8(vget_high_u8(chunk));
        
        sum = vaddw_u16(sum, vget_low_u16(lo));
        sum = vaddw_u16(sum, vget_high_u16(lo));
        sum = vaddw_u16(sum, vget_low_u16(hi));
        sum = vaddw_u16(sum, vget_high_u16(hi));
        
        data += 16;
        len -= 16;
    }
    
    /* Horizontal sum */
    uint32_t result = vgetq_lane_u32(sum, 0) + 
                     vgetq_lane_u32(sum, 1) +
                     vgetq_lane_u32(sum, 2) + 
                     vgetq_lane_u32(sum, 3);
    
    /* Handle remaining bytes */
    if (len > 0) {
        uint16_t tail = checksum_scalar(data, len);
        result += tail;
    }
    
    /* Fold and invert */
    while (result >> 16)
        result = (result & 0xFFFF) + (result >> 16);
    
    return (uint16_t)~result;
}
#endif

uint16_t
hev_simd_checksum(const uint8_t *data, size_t len)
{
#ifdef HEV_SIMD_AVX2
    return checksum_avx2(data, len);
#elif defined(HEV_SIMD_NEON)
    return checksum_neon(data, len);
#else
    return checksum_scalar(data, len);
#endif
}

void
hev_simd_memcpy(void *dst, const void *src, size_t len)
{
#ifdef HEV_SIMD_AVX2
    /* AVX2 memcpy */
    uint8_t *d = dst;
    const uint8_t *s = src;
    
    while (len >= 32) {
        __m256i chunk = _mm256_loadu_si256((__m256i *)s);
        _mm256_storeu_si256((__m256i *)d, chunk);
        d += 32;
        s += 32;
        len -= 32;
    }
    
    if (len > 0)
        memcpy(d, s, len);
#else
    memcpy(dst, src, len);
#endif
}

int
hev_simd_memcmp(const void *a, const void *b, size_t len)
{
#ifdef HEV_SIMD_AVX2
    const uint8_t *pa = a;
    const uint8_t *pb = b;
    
    while (len >= 32) {
        __m256i va = _mm256_loadu_si256((__m256i *)pa);
        __m256i vb = _mm256_loadu_si256((__m256i *)pb);
        __m256i cmp = _mm256_cmpeq_epi8(va, vb);
        
        if (_mm256_movemask_epi8(cmp) != -1)
            return 1; /* Not equal */
        
        pa += 32;
        pb += 32;
        len -= 32;
    }
    
    if (len > 0)
        return memcmp(pa, pb, len);
    
    return 0;
#else
    return memcmp(a, b, len);
#endif
}

int
hev_simd_supported(void)
{
#ifdef HEV_SIMD_SUPPORTED
    return 1;
#else
    return 0;
#endif
}

const char *
hev_simd_get_features(void)
{
#ifdef HEV_SIMD_AVX2
    return "AVX2";
#elif defined(HEV_SIMD_SSE2)
    return "SSE2";
#elif defined(HEV_SIMD_NEON)
    return "NEON";
#else
    return "None";
#endif
}
