/*
 * RamJet - Rice clone in C
 * Copyright (c) 2026 - Ported from Rust by Alecaishere/CuerdOS Dev. Team
 *
 *
 * Core RamJet library: process iteration and rule application.
 */

#ifndef RAMJET_H
#define RAMJET_H

#include "rule.h"
#include "cgroup.h"
#include "proc_type.h"
#include <sys/types.h>

/* Iterate over all running processes and threads in /proc.
   For each process/thread, calls the callback with the pid and exe name.
   The 'exe_name' is the basename of the process executable.
   'user_data' is passed through to the callback.
   Returns 0 on success, -1 on error. */
typedef int (*proc_callback)(pid_t pid, const char *exe_name, void *user_data);
int iterate_procs(proc_callback cb, void *user_data);

/* Apply all loaded rules to all matching running processes.
   'rules' contains the rule map, 'types' the type map for inheritance,
   and 'cgroups' the cgroup map for cgroup assignment.
   Returns the number of errors encountered. */
int apply_all_rules(const RuleMap *rules, const ProcTypeMap *types,
                    CgroupMap *cgroups);

#endif /* RAMJET_H */
