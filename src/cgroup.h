/*
 * rice - Ananicy clone in C
 * Copyright (c) 2024 - Ported from Rust by themadprofessor/rice
 * Licensed under MIT
 *
 * Cgroup v1 management: parse cgroup definitions from config and apply
 * them to processes by writing to the cgroup filesystem.
 */

#ifndef RICE_CGROUP_H
#define RICE_CGROUP_H

#include <sys/types.h>

#define CGROUP_NAME_MAX 256

typedef struct {
    char name[CGROUP_NAME_MAX];
    unsigned char cpu_quota; /* Percentage, 1-100 */
} CgroupDef;

/* A live cgroup that has been created in the filesystem */
typedef struct CgroupEntry {
    CgroupDef def;
    struct CgroupEntry *next;
} CgroupEntry;

#define CGROUP_MAP_SIZE 64

typedef struct {
    CgroupEntry *buckets[CGROUP_MAP_SIZE];
} CgroupMap;

/* Initialize a CgroupMap */
void cgroup_map_init(CgroupMap *map);

/* Free all entries in a CgroupMap (also deletes cgroups from filesystem) */
void cgroup_map_free(CgroupMap *map);

/* Insert a CgroupDef into the map (creating the cgroup on the filesystem).
   Returns 0 on success, -1 on error. */
int cgroup_map_insert(CgroupMap *map, const CgroupDef *def);

/* Look up a CgroupDef by name. Returns NULL if not found. */
const CgroupDef *cgroup_map_get(const CgroupMap *map, const char *name);

/* Apply a cgroup to a process by adding the PID to the cgroup's tasks file.
   Returns 0 on success, -1 on error. */
int cgroup_apply(const CgroupDef *cg, pid_t pid);

/* Build the CgroupMap by walking /etc/ananicy.d for .cgroups files.
   Returns 0 on success, -1 on fatal error. */
int build_cgroups(CgroupMap *map);

#endif /* RICE_CGROUP_H */
