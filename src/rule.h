/*
 * rice - Ananicy clone in C
 * Copyright (c) 2024 - Ported from Rust by themadprofessor/rice
 * Licensed under MIT
 *
 * Rule definitions: a rule maps a process name to optional nice, ioclass,
 * ionice, and cgroup values, with optional inheritance from a ProcType.
 */

#ifndef RICE_RULE_H
#define RICE_RULE_H

#include "class.h"
#include "proc_type.h"
#include <sys/types.h>

#define RULE_NAME_MAX 256
#define RULE_CGROUP_MAX 256

typedef struct {
    char name[RULE_NAME_MAX];
    char proc_type_name[PROC_TYPE_NAME_MAX]; /* Empty if no type */
    int nice;               /* -1 means not set */
    IoClass ioclass;        /* Only valid if has_ioclass is set */
    int has_ioclass;
    int ionice;             /* -1 means not set */
    char cgroup[RULE_CGROUP_MAX]; /* Empty means not set */
} Rule;

/* Hash map of Rule entries keyed by process name */
#define RULE_MAP_SIZE 1024

typedef struct RuleEntry {
    Rule rule;
    struct RuleEntry *next;
} RuleEntry;

typedef struct {
    RuleEntry *buckets[RULE_MAP_SIZE];
} RuleMap;

/* Initialize a RuleMap */
void rule_map_init(RuleMap *map);

/* Free all entries in a RuleMap */
void rule_map_free(RuleMap *map);

/* Insert a Rule into the map */
void rule_map_insert(RuleMap *map, const Rule *r);

/* Look up a Rule by process name. Returns NULL if not found. */
const Rule *rule_map_get(const RuleMap *map, const char *name);

/* Build the RuleMap by walking /etc/ananicy.d for .rules files.
   Types are used for inheritance (the rule's proc_type references a type).
   Returns 0 on success, -1 on fatal error. */
int build_rules(RuleMap *map, const ProcTypeMap *types);

/* Get the effective cgroup name for a rule, considering type inheritance.
   Returns pointer to the cgroup string, or NULL if none is set. */
const char *rule_effective_cgroup(const Rule *r, const ProcTypeMap *types);

/* Get the effective nice value for a rule, considering type inheritance.
   Returns the nice value, or -1 if none is set. */
int rule_effective_nice(const Rule *r, const ProcTypeMap *types);

/* Get the effective ioclass for a rule, considering type inheritance.
   Returns 0 on success with *out set, -1 if no ioclass is set. */
int rule_effective_ioclass(const Rule *r, const ProcTypeMap *types, IoClass *out);

/* Get the effective ionice value for a rule, considering type inheritance.
   Returns the ionice value, or -1 if none is set. */
int rule_effective_ionice(const Rule *r, const ProcTypeMap *types);

/* Apply a rule to a process: sets nice value and ionice.
   Returns 0 on success, -1 on error. */
int rule_apply(const Rule *r, const ProcTypeMap *types, pid_t pid);

#endif /* RICE_RULE_H */
