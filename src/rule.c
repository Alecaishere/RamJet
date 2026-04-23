/*
 * rice - Ananicy clone in C
 * Copyright (c) 2024 - Ported from Rust by themadprofessor/rice
 * Licensed under MIT
 *
 * Rule parsing, map, and application implementation.
 */

#define _GNU_SOURCE
#include "rule.h"
#include "parse.h"
#include "class.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include "cJSON.h"

#define ANANICY_CONFIG_DIR "/etc/ananicy.d"

/* Simple djb2 hash */
static unsigned long hash_str(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*s++)) {
        h = ((h << 5) + h) + c;
    }
    return h;
}

void rule_map_init(RuleMap *map) {
    memset(map->buckets, 0, sizeof(map->buckets));
}

void rule_map_free(RuleMap *map) {
    for (int i = 0; i < RULE_MAP_SIZE; i++) {
        RuleEntry *e = map->buckets[i];
        while (e) {
            RuleEntry *next = e->next;
            free(e);
            e = next;
        }
        map->buckets[i] = NULL;
    }
}

void rule_map_insert(RuleMap *map, const Rule *r) {
    unsigned long idx = hash_str(r->name) % RULE_MAP_SIZE;

    RuleEntry *entry = malloc(sizeof(RuleEntry));
    if (!entry) {
        fprintf(stderr, "[rice] error: out of memory inserting rule\n");
        return;
    }
    entry->rule = *r;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
}

const Rule *rule_map_get(const RuleMap *map, const char *name) {
    unsigned long idx = hash_str(name) % RULE_MAP_SIZE;

    const RuleEntry *e = map->buckets[idx];
    while (e) {
        if (strcmp(e->rule.name, name) == 0) {
            return &e->rule;
        }
        e = e->next;
    }
    return NULL;
}

const char *rule_effective_cgroup(const Rule *r, const ProcTypeMap *types) {
    if (r->cgroup[0] != '\0') {
        return r->cgroup;
    }
    if (r->proc_type_name[0] != '\0') {
        const ProcType *pt = proc_type_map_get(types, r->proc_type_name);
        if (pt && pt->cgroup[0] != '\0') {
            return pt->cgroup;
        }
    }
    return NULL;
}

int rule_effective_nice(const Rule *r, const ProcTypeMap *types) {
    if (r->nice != -1) {
        return r->nice;
    }
    if (r->proc_type_name[0] != '\0') {
        const ProcType *pt = proc_type_map_get(types, r->proc_type_name);
        if (pt && pt->nice != -1) {
            return pt->nice;
        }
    }
    return -1;
}

int rule_effective_ioclass(const Rule *r, const ProcTypeMap *types, IoClass *out) {
    if (r->has_ioclass) {
        *out = r->ioclass;
        return 0;
    }
    if (r->proc_type_name[0] != '\0') {
        const ProcType *pt = proc_type_map_get(types, r->proc_type_name);
        if (pt && pt->has_ioclass) {
            *out = pt->ioclass;
            return 0;
        }
    }
    return -1;
}

int rule_effective_ionice(const Rule *r, const ProcTypeMap *types) {
    if (r->ionice != -1) {
        return r->ionice;
    }
    if (r->proc_type_name[0] != '\0') {
        const ProcType *pt = proc_type_map_get(types, r->proc_type_name);
        if (pt && pt->ionice != -1) {
            return pt->ionice;
        }
    }
    return -1;
}

/* Apply nice value to a process using setpriority() */
static int apply_nice(int nice_val, pid_t pid) {
    if (setpriority(PRIO_PROCESS, (id_t)pid, nice_val) != 0) {
        switch (errno) {
            case ESRCH:
                fprintf(stderr, "[rice] error: process [%d] not found\n", pid);
                return -1;
            case EACCES:
                fprintf(stderr, "[rice] error: permission denied or nice value "
                        "larger than rlimit for pid %d\n", pid);
                return -1;
            case EPERM:
                fprintf(stderr, "[rice] error: permission denied for pid %d\n", pid);
                return -1;
            default:
                fprintf(stderr, "[rice] error: setpriority failed for pid %d: %s\n",
                        pid, strerror(errno));
                return -1;
        }
    }
    return 0;
}

/* Apply ionice to a process by running the ionice command.
   This mirrors the Rust implementation which also shells out to ionice. */
static int apply_ionice(IoClass ioclass, int ionice_val, pid_t pid) {
    char class_str[8];
    char pid_str[16];
    snprintf(class_str, sizeof(class_str), "%d", io_class_to_num(ioclass));
    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    /* Build the command: ionice -c <class> [-n <nice>] -p <pid> */
    if ((ioclass == IO_CLASS_REALTIME || ioclass == IO_CLASS_BEST_EFFORT) &&
        ionice_val >= 0) {
        char nice_str[16];
        snprintf(nice_str, sizeof(nice_str), "%d", ionice_val);

        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ionice -c %s -n %s -p %s", class_str, nice_str, pid_str);
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "[rice] error: ionice command failed for pid %d\n", pid);
            return -1;
        }
    } else {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ionice -c %s -p %s", class_str, pid_str);
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "[rice] error: ionice command failed for pid %d\n", pid);
            return -1;
        }
    }

    return 0;
}

int rule_apply(const Rule *r, const ProcTypeMap *types, pid_t pid) {
    /* Apply nice value */
    int nice_val = rule_effective_nice(r, types);
    if (nice_val != -1) {
        if (apply_nice(nice_val, pid) != 0) {
            /* Log but continue with other settings */
        }
    }

    /* Apply ioclass / ionice */
    IoClass ioclass;
    if (rule_effective_ioclass(r, types, &ioclass) == 0) {
        int ionice_val = rule_effective_ionice(r, types);
        if (apply_ionice(ioclass, ionice_val, pid) != 0) {
            /* Log but continue */
        }
    }

    return 0;
}

/* Callback context for building rules */
typedef struct {
    RuleMap *map;
    const ProcTypeMap *types;
} BuildRulesCtx;

static int parse_rule_line(const char *json_line, void *user_data) {
    BuildRulesCtx *ctx = (BuildRulesCtx *)user_data;
    Rule r;
    memset(&r, 0, sizeof(r));
    r.nice = -1;
    r.ionice = -1;
    r.has_ioclass = 0;

    cJSON *root = cJSON_Parse(json_line);
    if (!root) {
        fprintf(stderr, "[rice] warn: failed to parse rule JSON: %s\n",
                cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown error");
        return 0;
    }

    /* "name" field - required */
    cJSON *jname = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!jname || !cJSON_IsString(jname)) {
        cJSON_Delete(root);
        return 0;
    }
    strncpy(r.name, jname->valuestring, RULE_NAME_MAX - 1);
    r.name[RULE_NAME_MAX - 1] = '\0';

    /* "type" field - optional */
    cJSON *jtype = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (jtype && cJSON_IsString(jtype)) {
        strncpy(r.proc_type_name, jtype->valuestring, PROC_TYPE_NAME_MAX - 1);
        r.proc_type_name[PROC_TYPE_NAME_MAX - 1] = '\0';
    }

    /* nice */
    cJSON *jnice = cJSON_GetObjectItemCaseSensitive(root, "nice");
    if (jnice && cJSON_IsNumber(jnice)) {
        int nice = jnice->valueint;
        if (nice > 20 || nice < -19) {
            fprintf(stderr, "[rice] warn: invalid nice value %d for rule %s\n",
                    nice, r.name);
            cJSON_Delete(root);
            return 0;
        }
        r.nice = nice;
    }

    /* io-class (also aliased as "ioclass") */
    cJSON *jioclass = cJSON_GetObjectItemCaseSensitive(root, "io-class");
    if (!jioclass) {
        jioclass = cJSON_GetObjectItemCaseSensitive(root, "ioclass");
    }
    if (jioclass && cJSON_IsString(jioclass)) {
        if (io_class_from_string(jioclass->valuestring, &r.ioclass) == 0) {
            r.has_ioclass = 1;
        } else {
            fprintf(stderr, "[rice] warn: unknown ioclass '%s' in rule '%s'\n",
                    jioclass->valuestring, r.name);
        }
    }

    /* ionice */
    cJSON *jionice = cJSON_GetObjectItemCaseSensitive(root, "ionice");
    if (jionice && cJSON_IsNumber(jionice)) {
        int ion = jionice->valueint;
        if (ion > 7) {
            fprintf(stderr, "[rice] warn: invalid ionice value %d for rule %s\n",
                    ion, r.name);
            cJSON_Delete(root);
            return 0;
        }
        r.ionice = ion;
    }

    /* cgroup */
    cJSON *jcgroup = cJSON_GetObjectItemCaseSensitive(root, "cgroup");
    if (jcgroup && cJSON_IsString(jcgroup)) {
        strncpy(r.cgroup, jcgroup->valuestring, RULE_CGROUP_MAX - 1);
        r.cgroup[RULE_CGROUP_MAX - 1] = '\0';
    }

    cJSON_Delete(root);

    rule_map_insert(ctx->map, &r);
    return 0;
}

int build_rules(RuleMap *map, const ProcTypeMap *types) {
    rule_map_init(map);

    BuildRulesCtx ctx = { .map = map, .types = types };
    return walk_config_dir(ANANICY_CONFIG_DIR, "rules", parse_rule_line, &ctx);
}
