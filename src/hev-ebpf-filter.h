/*
 ============================================================================
 Name        : hev-ebpf-filter.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : eBPF Packet Filtering (Linux)
 ============================================================================
 */

#ifndef __HEV_EBPF_FILTER_H__
#define __HEV_EBPF_FILTER_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __linux__
#define HEV_EBPF_SUPPORTED 1
#endif

typedef struct _HevEbpfFilter HevEbpfFilter;

/* Filter types */
typedef enum {
    HEV_EBPF_FILTER_ALLOW_ALL,
    HEV_EBPF_FILTER_DROP_ICMP,
    HEV_EBPF_FILTER_DROP_ARP,
    HEV_EBPF_FILTER_CUSTOM
} HevEbpfFilterType;

/**
 * hev_ebpf_filter_new:
 * @type: filter type
 *
 * Create new eBPF filter
 *
 * Returns: HevEbpfFilter or NULL
 *
 * Since: 2.0
 */
HevEbpfFilter *hev_ebpf_filter_new(HevEbpfFilterType type);

/**
 * hev_ebpf_filter_destroy:
 * @filter: HevEbpfFilter
 *
 * Destroy eBPF filter
 *
 * Since: 2.0
 */
void hev_ebpf_filter_destroy(HevEbpfFilter *filter);

/**
 * hev_ebpf_filter_attach:
 * @filter: HevEbpfFilter
 * @fd: socket file descriptor
 *
 * Attach filter to socket
 *
 * Returns: 0 on success, -1 on error
 *
 * Since: 2.0
 */
int hev_ebpf_filter_attach(HevEbpfFilter *filter, int fd);

/**
 * hev_ebpf_filter_detach:
 * @filter: HevEbpfFilter
 * @fd: socket file descriptor
 *
 * Detach filter from socket
 *
 * Returns: 0 on success, -1 on error
 *
 * Since: 2.0
 */
int hev_ebpf_filter_detach(HevEbpfFilter *filter, int fd);

/**
 * hev_ebpf_filter_load_custom:
 * @filter: HevEbpfFilter
 * @prog: BPF program code
 * @prog_len: program length
 *
 * Load custom BPF program
 *
 * Returns: 0 on success, -1 on error
 *
 * Since: 2.0
 */
int hev_ebpf_filter_load_custom(HevEbpfFilter *filter,
                               const void *prog,
                               size_t prog_len);

/**
 * hev_ebpf_filter_get_stats:
 * @filter: HevEbpfFilter
 * @passed: (out) packets passed
 * @dropped: (out) packets dropped
 *
 * Get filter statistics
 *
 * Since: 2.0
 */
void hev_ebpf_filter_get_stats(HevEbpfFilter *filter,
                              uint64_t *passed,
                              uint64_t *dropped);

/**
 * hev_ebpf_supported:
 *
 * Check if eBPF is supported
 *
 * Returns: 1 if supported, 0 otherwise
 *
 * Since: 2.0
 */
int hev_ebpf_supported(void);

#endif /* __HEV_EBPF_FILTER_H__ */
