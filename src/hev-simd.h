/*
 ============================================================================
 Name        : hev-simd.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : SIMD-optimized Packet Processing
 ============================================================================
 */

#ifndef __HEV_SIMD_H__
#define __HEV_SIMD_H__

#include <stddef.h>
#include <stdint.h>

/* Check for SIMD support */
#if defined(__x86_64__) || defined(__i386__)
    #define HEV_SIMD_SUPPORTED 1
    #if defined(__AVX2__)
        #define HEV_SIMD_AVX2 1
    #elif defined(__SSE2__)
        #define HEV_SIMD_SSE2 1
    #endif
#elif defined(__aarch64__) || defined(__ARM_NEON)
    #define HEV_SIMD_SUPPORTED 1
    #define HEV_SIMD_NEON 1
#endif

/**
 * hev_simd_checksum:
 * @data: data buffer
 * @len: length in bytes
 *
 * Calculate Internet checksum using SIMD
 * Falls back to scalar if SIMD not available
 *
 * Returns: 16-bit checksum
 *
 * Since: 2.0
 */
uint16_t hev_simd_checksum(const uint8_t *data, size_t len);

/**
 * hev_simd_memcpy:
 * @dst: destination
 * @src: source
 * @len: length
 *
 * Fast memory copy using SIMD
 *
 * Since: 2.0
 */
void hev_simd_memcpy(void *dst, const void *src, size_t len);

/**
 * hev_simd_memcmp:
 * @a: first buffer
 * @b: second buffer
 * @len: length
 *
 * Fast memory compare using SIMD
 *
 * Returns: 0 if equal, non-zero otherwise
 *
 * Since: 2.0
 */
int hev_simd_memcmp(const void *a, const void *b, size_t len);

/**
 * hev_simd_supported:
 *
 * Check if SIMD is supported on this CPU
 *
 * Returns: 1 if supported, 0 otherwise
 *
 * Since: 2.0
 */
int hev_simd_supported(void);

/**
 * hev_simd_get_features:
 *
 * Get SIMD feature string
 *
 * Returns: string like "AVX2", "SSE2", "NEON", or "None"
 *
 * Since: 2.0
 */
const char *hev_simd_get_features(void);

#endif /* __HEV_SIMD_H__ */
