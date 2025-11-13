/*
 ============================================================================
 Name        : hev-cpu-affinity.c
 Author      : Performance Optimization Team
 Copyright   : Copyright (c) 2025
 Description : CPU Affinity Implementation
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hev-cpu-affinity.h"

#ifdef __linux__
#define _GNU_SOURCE
#include <sched.h>
#include <numa.h>
#include <numaif.h>
#endif

HevCpuTopology *
hev_cpu_topology_detect(void)
{
    HevCpuTopology *topo;
    
    topo = calloc(1, sizeof(HevCpuTopology));
    if (!topo)
        return NULL;
    
#ifdef __linux__
    /* Detect CPU count */
    topo->num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    
    /* Detect NUMA nodes */
    if (numa_available() >= 0) {
        topo->num_numa_nodes = numa_max_node() + 1;
        
        /* Allocate NUMA mappings */
        topo->cpu_to_numa = calloc(topo->num_cpus, sizeof(int));
        topo->numa_cpus = calloc(topo->num_numa_nodes, sizeof(int *));
        topo->numa_cpu_count = calloc(topo->num_numa_nodes, sizeof(int));
        
        /* Map CPUs to NUMA nodes */
        for (int cpu = 0; cpu < topo->num_cpus; cpu++) {
            topo->cpu_to_numa[cpu] = numa_node_of_cpu(cpu);
        }
        
        /* Count CPUs per NUMA node */
        for (int node = 0; node < topo->num_numa_nodes; node++) {
            int count = 0;
            for (int cpu = 0; cpu < topo->num_cpus; cpu++) {
                if (topo->cpu_to_numa[cpu] == node)
                    count++;
            }
            topo->numa_cpu_count[node] = count;
            topo->numa_cpus[node] = calloc(count, sizeof(int));
            
            /* Fill CPU list for this node */
            int idx = 0;
            for (int cpu = 0; cpu < topo->num_cpus; cpu++) {
                if (topo->cpu_to_numa[cpu] == node)
                    topo->numa_cpus[node][idx++] = cpu;
            }
        }
    } else {
        topo->num_numa_nodes = 1;
    }
#else
    topo->num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    topo->num_numa_nodes = 1;
#endif
    
    return topo;
}

void
hev_cpu_topology_free(HevCpuTopology *topo)
{
    if (!topo)
        return;
    
    if (topo->cpu_to_numa)
        free(topo->cpu_to_numa);
    
    if (topo->numa_cpus) {
        for (int i = 0; i < topo->num_numa_nodes; i++) {
            if (topo->numa_cpus[i])
                free(topo->numa_cpus[i]);
        }
        free(topo->numa_cpus);
    }
    
    if (topo->numa_cpu_count)
        free(topo->numa_cpu_count);
    
    free(topo);
}

int
hev_cpu_set_affinity(pthread_t thread, int cpu)
{
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    
    return pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
#else
    return -1; /* Not supported */
#endif
}

int
hev_cpu_set_affinity_numa(pthread_t thread, int numa_node)
{
#ifdef __linux__
    if (numa_available() < 0)
        return -1;
    
    struct bitmask *mask = numa_allocate_cpumask();
    numa_node_to_cpus(numa_node, mask);
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int cpu = 0; cpu < numa_num_configured_cpus(); cpu++) {
        if (numa_bitmask_isbitset(mask, cpu))
            CPU_SET(cpu, &cpuset);
    }
    
    numa_free_cpumask(mask);
    
    return pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
#else
    return -1;
#endif
}

int
hev_cpu_get_current(void)
{
#ifdef __linux__
    return sched_getcpu();
#else
    return -1;
#endif
}

int
hev_cpu_get_count(void)
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}

void *
hev_numa_alloc(size_t size, int node)
{
#ifdef __linux__
    if (numa_available() < 0)
        return malloc(size);
    
    if (node < 0)
        return numa_alloc_local(size);
    else
        return numa_alloc_onnode(size, node);
#else
    return malloc(size);
#endif
}

void
hev_numa_free(void *ptr, size_t size)
{
#ifdef __linux__
    if (numa_available() >= 0)
        numa_free(ptr, size);
    else
        free(ptr);
#else
    free(ptr);
#endif
}
