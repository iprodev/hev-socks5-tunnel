/*
 ============================================================================
 Name        : hev-ebpf-filter.c
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : eBPF Filter Implementation
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hev-ebpf-filter.h"

#ifdef HEV_EBPF_SUPPORTED
#include <linux/bpf.h>
#include <linux/filter.h>
#include <sys/socket.h>
#include <unistd.h>

struct _HevEbpfFilter {
    int prog_fd;
    HevEbpfFilterType type;
    uint64_t passed;
    uint64_t dropped;
};

/* Classic BPF program to drop ICMP */
static struct sock_filter drop_icmp_prog[] = {
    /* Load protocol field (offset 9 in IP header) */
    BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 9),
    /* Compare with IPPROTO_ICMP (1) */
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 1, 0, 1),
    /* Drop if ICMP */
    BPF_STMT(BPF_RET | BPF_K, 0),
    /* Accept otherwise */
    BPF_STMT(BPF_RET | BPF_K, 0xFFFFFFFF),
};

/* Classic BPF program to drop ARP */
static struct sock_filter drop_arp_prog[] = {
    /* Load EtherType (offset 12-13) */
    BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
    /* Compare with ETH_P_ARP (0x0806) */
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x0806, 0, 1),
    /* Drop if ARP */
    BPF_STMT(BPF_RET | BPF_K, 0),
    /* Accept otherwise */
    BPF_STMT(BPF_RET | BPF_K, 0xFFFFFFFF),
};

HevEbpfFilter *
hev_ebpf_filter_new(HevEbpfFilterType type)
{
    HevEbpfFilter *filter;
    
    filter = calloc(1, sizeof(HevEbpfFilter));
    if (!filter)
        return NULL;
    
    filter->type = type;
    filter->prog_fd = -1;
    
    return filter;
}

void
hev_ebpf_filter_destroy(HevEbpfFilter *filter)
{
    if (!filter)
        return;
    
    if (filter->prog_fd >= 0)
        close(filter->prog_fd);
    
    free(filter);
}

int
hev_ebpf_filter_attach(HevEbpfFilter *filter, int fd)
{
    if (!filter || fd < 0)
        return -1;
    
    struct sock_fprog fprog;
    
    switch (filter->type) {
    case HEV_EBPF_FILTER_DROP_ICMP:
        fprog.len = sizeof(drop_icmp_prog) / sizeof(drop_icmp_prog[0]);
        fprog.filter = drop_icmp_prog;
        break;
        
    case HEV_EBPF_FILTER_DROP_ARP:
        fprog.len = sizeof(drop_arp_prog) / sizeof(drop_arp_prog[0]);
        fprog.filter = drop_arp_prog;
        break;
        
    case HEV_EBPF_FILTER_ALLOW_ALL:
    default:
        return 0; /* No filter */
    }
    
    /* Attach filter to socket */
    if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, 
                   &fprog, sizeof(fprog)) < 0) {
        return -1;
    }
    
    return 0;
}

int
hev_ebpf_filter_detach(HevEbpfFilter *filter, int fd)
{
    if (!filter || fd < 0)
        return -1;
    
    int val = 0;
    return setsockopt(fd, SOL_SOCKET, SO_DETACH_FILTER, &val, sizeof(val));
}

int
hev_ebpf_filter_load_custom(HevEbpfFilter *filter,
                           const void *prog,
                           size_t prog_len)
{
    /* Custom BPF program loading would go here */
    /* This requires BPF syscall and proper program verification */
    return -1; /* Not implemented in this version */
}

void
hev_ebpf_filter_get_stats(HevEbpfFilter *filter,
                         uint64_t *passed,
                         uint64_t *dropped)
{
    if (!filter)
        return;
    
    if (passed)
        *passed = filter->passed;
    
    if (dropped)
        *dropped = filter->dropped;
}

int
hev_ebpf_supported(void)
{
    return 1; /* Classic BPF is always supported on Linux */
}

#else /* !HEV_EBPF_SUPPORTED */

struct _HevEbpfFilter {
    int dummy;
};

HevEbpfFilter *
hev_ebpf_filter_new(HevEbpfFilterType type)
{
    return NULL;
}

void
hev_ebpf_filter_destroy(HevEbpfFilter *filter)
{
}

int
hev_ebpf_filter_attach(HevEbpfFilter *filter, int fd)
{
    return -1;
}

int
hev_ebpf_filter_detach(HevEbpfFilter *filter, int fd)
{
    return -1;
}

int
hev_ebpf_filter_load_custom(HevEbpfFilter *filter,
                           const void *prog,
                           size_t prog_len)
{
    return -1;
}

void
hev_ebpf_filter_get_stats(HevEbpfFilter *filter,
                         uint64_t *passed,
                         uint64_t *dropped)
{
}

int
hev_ebpf_supported(void)
{
    return 0;
}

#endif /* HEV_EBPF_SUPPORTED */
