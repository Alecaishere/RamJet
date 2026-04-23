/*
 * RamJet - Rice clone in C
 * Copyright (c) 2026 - Ported from Rust by Alecaishere/CuerdOS Dev. Team
 *
 *
 * Process type definitions: a named type with optional nice, ioclass,
 * ionice, cgroup, and oom_score_adj values.
 */

#ifndef RAMJET_PROC_TYPE_H
#define RAMJET_PROC_TYPE_H

#include "class.h"
#include <sys/types.h>

#define PROC_TYPE_NAME_MAX 256
#define PROC_TYPE_CGROUP_MAX 256

typedef struct {
    char name[PROC_TYPE_NAME_MAX];
    int nice;               /* -1 means not set */
    IoClass ioclass;        /* Only valid if has_ioclass is set */
    int has_ioclass;
    int ionice;             /* -1 means not set */
    char cgroup[PROC_TYPE_CGROUP_MAX]; /* Empty string means not set */
    int oom_score_adj;      /* -1 means not set */
} ProcType;

/* Hash map of ProcType entries keyed by name */
#define PROC_TYPE_MAP_SIZE 512

typedef struct ProcTypeEntry {
    ProcType type;
    struct ProcTypeEntry *next;
} ProcTypeEntry;

typedef struct {
    ProcTypeEntry *buckets[PROC_TYPE_MAP_SIZE];
} ProcTypeMap;

/* Initialize a ProcTypeMap */
void proc_type_map_init(ProcTypeMap *map);

/* Free all entries in a ProcTypeMap */
void proc_type_map_free(ProcTypeMap *map);

/* Insert a ProcType into the map (copies the data) */
void proc_type_map_insert(ProcTypeMap *map, const ProcType *pt);

/* Look up a ProcType by name. Returns NULL if not found. */
const ProcType *proc_type_map_get(const ProcTypeMap *map, const char *name);

/* Build the ProcTypeMap by walking /etc/ananicy.d for .types files.
   Returns 0 on success, -1 on fatal error. */
int build_types(ProcTypeMap *map);

#endif /* RAMJET_PROC_TYPE_H */
