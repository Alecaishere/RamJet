/*
 * RamJet - Rice clone in C
 * Copyright (c) 2026 - Ported from Rust by Alecaishere/CuerdOS Dev. Team
 *
 * Cgroup v1 management implementation.
 *
 * Unlike the Rust version which uses the `controlgroup` crate, this
 * implementation directly writes to the cgroup v1 filesystem under
 * /sys/fs/cgroup/cpu/<cgroup_name>/. This mirrors the same behavior:
 *   - Creates the cgroup directory
 *   - Writes cpu.shares, cpu.cfs_period_us, cpu.cfs_quota_us
 *   - Adds PIDs to the cgroup.tasks file
 */

#define _GNU_SOURCE
#include "cgroup.h"
#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"

#define ANANICY_CONFIG_DIR "/etc/ananicy.d"
#define CGROUP_CPU_BASE "/sys/fs/cgroup/cpu"
#define PERIOD_US 100000UL

/* djb2 hash */
static unsigned long hash_str(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*s++)) {
        h = ((h << 5) + h) + c;
    }
    return h;
}

void cgroup_map_init(CgroupMap *map) {
    memset(map->buckets, 0, sizeof(map->buckets));
}

void cgroup_map_free(CgroupMap *map) {
    for (int i = 0; i < CGROUP_MAP_SIZE; i++) {
        CgroupEntry *e = map->buckets[i];
        while (e) {
            CgroupEntry *next = e->next;

            /* Try to delete the cgroup directory (rmdir) */
            char path[512];
            snprintf(path, sizeof(path), CGROUP_CPU_BASE "/%s", e->def.name);
            if (rmdir(path) != 0) {
                fprintf(stderr, "[ramjet] error: failed to delete cgroup %s: %s\n",
                        e->def.name, strerror(errno));
            }

            free(e);
            e = next;
        }
        map->buckets[i] = NULL;
    }
}

/* Write a string to a file. Returns 0 on success, -1 on error. */
static int write_file(const char *path, const char *value) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "[ramjet] error: failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fputs(value, fp) == EOF) {
        fprintf(stderr, "[ramjet] error: failed to write to %s: %s\n", path, strerror(errno));
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/* Create the cgroup directory and configure CPU parameters */
static int create_cgroup_fs(const CgroupDef *def) {
    char path[512];
    char val[64];

    /* Create cgroup directory */
    snprintf(path, sizeof(path), CGROUP_CPU_BASE "/%s", def->name);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[ramjet] error: failed to create cgroup dir %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    /* cpu.shares = 1024 * quota / 100 */
    snprintf(path, sizeof(path), CGROUP_CPU_BASE "/%s/cpu.shares", def->name);
    unsigned long shares = 1024UL * def->cpu_quota / 100;
    snprintf(val, sizeof(val), "%lu", shares);
    if (write_file(path, val) != 0) return -1;

    /* cpu.cfs_period_us = PERIOD_US */
    snprintf(path, sizeof(path), CGROUP_CPU_BASE "/%s/cpu.cfs_period_us", def->name);
    snprintf(val, sizeof(val), "%lu", PERIOD_US);
    if (write_file(path, val) != 0) return -1;

    /* cpu.cfs_quota_us = PERIOD_US * ncpus * quota / 100 */
    snprintf(path, sizeof(path), CGROUP_CPU_BASE "/%s/cpu.cfs_quota_us", def->name);
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpus <= 0) ncpus = 1;
    long quota_us = (long)(PERIOD_US * (unsigned long)ncpus * def->cpu_quota / 100);
    snprintf(val, sizeof(val), "%ld", quota_us);
    if (write_file(path, val) != 0) return -1;

    return 0;
}

int cgroup_map_insert(CgroupMap *map, const CgroupDef *def) {
    /* Create the cgroup on the filesystem first */
    if (create_cgroup_fs(def) != 0) {
        fprintf(stderr, "[ramjet] warn: failed to create cgroup %s\n", def->name);
        return -1;
    }

    unsigned long idx = hash_str(def->name) % CGROUP_MAP_SIZE;

    CgroupEntry *entry = malloc(sizeof(CgroupEntry));
    if (!entry) {
        fprintf(stderr, "[ramjet] error: out of memory inserting cgroup\n");
        return -1;
    }
    entry->def = *def;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;

    return 0;
}

const CgroupDef *cgroup_map_get(const CgroupMap *map, const char *name) {
    unsigned long idx = hash_str(name) % CGROUP_MAP_SIZE;

    const CgroupEntry *e = map->buckets[idx];
    while (e) {
        if (strcmp(e->def.name, name) == 0) {
            return &e->def;
        }
        e = e->next;
    }
    return NULL;
}

int cgroup_apply(const CgroupDef *cg, pid_t pid) {
    char path[512];
    char val[16];

    snprintf(path, sizeof(path), CGROUP_CPU_BASE "/%s/cgroup.procs", cg->name);
    snprintf(val, sizeof(val), "%d", pid);

    if (write_file(path, val) != 0) {
        /* Try tasks file as fallback (for older kernels) */
        snprintf(path, sizeof(path), CGROUP_CPU_BASE "/%s/tasks", cg->name);
        if (write_file(path, val) != 0) {
            return -1;
        }
    }

    return 0;
}

/* Callback context for building cgroups */
typedef struct {
    CgroupMap *map;
} BuildCgroupsCtx;

static int parse_cgroup_line(const char *json_line, void *user_data) {
    BuildCgroupsCtx *ctx = (BuildCgroupsCtx *)user_data;

    cJSON *root = cJSON_Parse(json_line);
    if (!root) {
        fprintf(stderr, "[ramjet] warn: failed to parse cgroup JSON: %s\n",
                cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown error");
        return 0;
    }

    /* "cgroup" field - required */
    cJSON *jname = cJSON_GetObjectItemCaseSensitive(root, "cgroup");
    if (!jname || !cJSON_IsString(jname)) {
        cJSON_Delete(root);
        return 0;
    }

    /* "CPUQuota" or "cpu_quota" field - required */
    cJSON *jquota = cJSON_GetObjectItemCaseSensitive(root, "CPUQuota");
    if (!jquota) {
        jquota = cJSON_GetObjectItemCaseSensitive(root, "cpu_quota");
    }
    if (!jquota || !cJSON_IsNumber(jquota)) {
        cJSON_Delete(root);
        return 0;
    }

    int quota = jquota->valueint;
    if (quota > 100 || quota < 1) {
        fprintf(stderr, "[ramjet] warn: invalid CPUQuota %d for cgroup %s\n",
                quota, jname->valuestring);
        cJSON_Delete(root);
        return 0;
    }

    CgroupDef def;
    memset(&def, 0, sizeof(def));
    strncpy(def.name, jname->valuestring, CGROUP_NAME_MAX - 1);
    def.name[CGROUP_NAME_MAX - 1] = '\0';
    def.cpu_quota = (unsigned char)quota;

    cJSON_Delete(root);

    cgroup_map_insert(ctx->map, &def);
    return 0;
}

int build_cgroups(CgroupMap *map) {
    cgroup_map_init(map);

    BuildCgroupsCtx ctx = { .map = map };
    return walk_config_dir(ANANICY_CONFIG_DIR, "cgroups", parse_cgroup_line, &ctx);
}
