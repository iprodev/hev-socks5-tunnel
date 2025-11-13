/*
 ============================================================================
 Name        : hev-cpu-affinity.h
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : CPU Affinity and NUMA Support
 ============================================================================
 */

#ifndef __HEV_CPU_AFFINITY_H__
#define __HEV_CPU_AFFINITY_H__

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

typedef struct _HevCpuTopology HevCpuTopology;

struct _HevCpuTopology {
    int num_cpus;
    int num_numa_nodes;
    int *cpu_to_numa;  /* Maps CPU ID to NUMA node */
    int **numa_cpus;   /* CPUs per NUMA node */
    int *numa_cpu_count;
};

/**
 * hev_cpu_topology_detect:
 *
 * Detect CPU topology (cores, NUMA nodes)
 *
 * Returns: HevCpuTopology or NULL
 *
 * Since: 2.0
 */
HevCpuTopology *hev_cpu_topology_detect(void);

/**
 * hev_cpu_topology_free:
 * @topo: HevCpuTopology
 *
 * Free topology data
 *
 * Since: 2.0
 */
void hev_cpu_topology_free(HevCpuTopology *topo);

/**
 * hev_cpu_set_affinity:
 * @thread: pthread
 * @cpu: CPU core ID
 *
 * Set thread affinity to specific CPU
 *
 * Returns: 0 on success, -1 on error
 *
 * Since: 2.0
 */
int hev_cpu_set_affinity(pthread_t thread, int cpu);

/**
 * hev_cpu_set_affinity_numa:
 * @thread: pthread
 * @numa_node: NUMA node ID
 *
 * Set thread affinity to NUMA node
 *
 * Returns: 0 on success, -1 on error
 *
 * Since: 2.0
 */
int hev_cpu_set_affinity_numa(pthread_t thread, int numa_node);

/**
 * hev_cpu_get_current:
 *
 * Get current CPU core ID
 *
 * Returns: CPU ID or -1
 *
 * Since: 2.0
 */
int hev_cpu_get_current(void);

/**
 * hev_cpu_get_count:
 *
 * Get number of CPU cores
 *
 * Returns: CPU count
 *
 * Since: 2.0
 */
int hev_cpu_get_count(void);

/**
 * hev_numa_alloc:
 * @size: allocation size
 * @node: NUMA node (-1 for local)
 *
 * Allocate memory on specific NUMA node
 *
 * Returns: pointer or NULL
 *
 * Since: 2.0
 */
void *hev_numa_alloc(size_t size, int node);

/**
 * hev_numa_free:
 * @ptr: pointer
 * @size: allocation size
 *
 * Free NUMA-allocated memory
 *
 * Since: 2.0
 */
void hev_numa_free(void *ptr, size_t size);

#endif /* __HEV_CPU_AFFINITY_H__ */
