/*
 * RamJet - Rice clone in C
 * Copyright (c) 2026 - Ported from Rust by Alecaishere/CuerdOS Dev. Team
 *
 * Core RamJet library implementation: process iteration and rule application.
 */

#define _GNU_SOURCE
#include "ramjet.h"
#include "rule.h"
#include "cgroup.h"
#include "proc_type.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Read the exe symlink for a PID from /proc/<pid>/exe.
   Writes the basename into buf. Returns 0 on success, -1 on error. */
static int get_proc_exe(pid_t pid, char *buf, size_t bufsz) {
    char link_path[64];
    char target[PATH_MAX];

    snprintf(link_path, sizeof(link_path), "/proc/%d/exe", pid);

    ssize_t len = readlink(link_path, target, sizeof(target) - 1);
    if (len < 0) {
        return -1;
    }
    target[len] = '\0';

    /* Extract basename */
    const char *base = strrchr(target, '/');
    base = base ? base + 1 : target;

    strncpy(buf, base, bufsz - 1);
    buf[bufsz - 1] = '\0';
    return 0;
}

/* Check if a directory entry name is a number (i.e., a PID directory) */
static int is_number(const char *s) {
    if (!s || *s == '\0') return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

/* Iterate threads in /proc/<pid>/task/ */
static int iterate_threads(pid_t pid, proc_callback cb, void *user_data) {
    char task_dir[64];
    snprintf(task_dir, sizeof(task_dir), "/proc/%d/task", pid);

    DIR *d = opendir(task_dir);
    if (!d) {
        /* Process may have exited - not an error */
        return 0;
    }

    char exe_name[256];
    if (get_proc_exe(pid, exe_name, sizeof(exe_name)) != 0) {
        closedir(d);
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!is_number(ent->d_name)) continue;

        pid_t tid = (pid_t)atoi(ent->d_name);
        /* Apply callback with the parent's exe name and the thread's tid */
        int ret = cb(tid, exe_name, user_data);
        if (ret != 0) {
            closedir(d);
            return ret;
        }
    }

    closedir(d);
    return 0;
}

int iterate_procs(proc_callback cb, void *user_data) {
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        fprintf(stderr, "[ramjet] error: failed to open /proc: %s\n", strerror(errno));
        return -1;
    }

    struct dirent *ent;
    int found_any = 0;

    while ((ent = readdir(proc_dir)) != NULL) {
        if (!is_number(ent->d_name)) continue;

        pid_t pid = (pid_t)atoi(ent->d_name);

        /* Check exe is accessible */
        char exe_name[256];
        if (get_proc_exe(pid, exe_name, sizeof(exe_name)) != 0) {
            continue;
        }

        found_any = 1;

        /* Call callback for the main process */
        int ret = cb(pid, exe_name, user_data);
        if (ret != 0) {
            closedir(proc_dir);
            return ret;
        }

        /* Iterate threads */
        iterate_threads(pid, cb, user_data);
    }

    closedir(proc_dir);

    if (!found_any) {
        fprintf(stderr, "[rice] error: no valid processes found\n");
        return -1;
    }

    return 0;
}

/* Context for apply_all_rules callback */
typedef struct {
    const RuleMap *rules;
    const ProcTypeMap *types;
    CgroupMap *cgroups;
    int error_count;
} ApplyContext;

static int apply_rule_callback(pid_t pid, const char *exe_name, void *user_data) {
    ApplyContext *ctx = (ApplyContext *)user_data;

    const Rule *r = rule_map_get(ctx->rules, exe_name);
    if (!r) return 0; /* No rule for this process */

    /* Apply nice and ionice */
    if (rule_apply(r, ctx->types, pid) != 0) {
        ctx->error_count++;
    }

    /* Apply cgroup if applicable */
    const char *cgroup_name = rule_effective_cgroup(r, ctx->types);
    if (cgroup_name) {
        const CgroupDef *cg = cgroup_map_get(ctx->cgroups, cgroup_name);
        if (cg) {
            if (cgroup_apply(cg, pid) != 0) {
                ctx->error_count++;
            }
        }
    }

    return 0;
}

int apply_all_rules(const RuleMap *rules, const ProcTypeMap *types,
                    CgroupMap *cgroups) {
    ApplyContext ctx = {
        .rules = rules,
        .types = types,
        .cgroups = cgroups,
        .error_count = 0,
    };

    iterate_procs(apply_rule_callback, &ctx);
    return ctx.error_count;
}
