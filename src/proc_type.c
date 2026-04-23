/*
 * RamJet - Rice clone in C
 * Copyright (c) 2026 - Ported from Rust by Alecaishere/CuerdOS Dev. Team
 *
 *
 * Process type parsing and map implementation.
 */

#define _GNU_SOURCE
#include "proc_type.h"
#include "parse.h"
#include "class.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void proc_type_map_init(ProcTypeMap *map) {
    memset(map->buckets, 0, sizeof(map->buckets));
}

void proc_type_map_free(ProcTypeMap *map) {
    for (int i = 0; i < PROC_TYPE_MAP_SIZE; i++) {
        ProcTypeEntry *e = map->buckets[i];
        while (e) {
            ProcTypeEntry *next = e->next;
            free(e);
            e = next;
        }
        map->buckets[i] = NULL;
    }
}

void proc_type_map_insert(ProcTypeMap *map, const ProcType *pt) {
    unsigned long idx = hash_str(pt->name) % PROC_TYPE_MAP_SIZE;

    ProcTypeEntry *entry = malloc(sizeof(ProcTypeEntry));
    if (!entry) {
        fprintf(stderr, "[ramjet] error: out of memory inserting proc type\n");
        return;
    }
    entry->type = *pt;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
}

const ProcType *proc_type_map_get(const ProcTypeMap *map, const char *name) {
    unsigned long idx = hash_str(name) % PROC_TYPE_MAP_SIZE;

    const ProcTypeEntry *e = map->buckets[idx];
    while (e) {
        if (strcmp(e->type.name, name) == 0) {
            return &e->type;
        }
        e = e->next;
    }
    return NULL;
}

/* Callback context for building types */
typedef struct {
    ProcTypeMap *map;
} BuildTypesCtx;

static int parse_type_line(const char *json_line, void *user_data) {
    BuildTypesCtx *ctx = (BuildTypesCtx *)user_data;
    ProcType pt;
    memset(&pt, 0, sizeof(pt));
    pt.nice = -1;
    pt.ionice = -1;
    pt.oom_score_adj = -1;
    pt.has_ioclass = 0;

    cJSON *root = cJSON_Parse(json_line);
    if (!root) {
        fprintf(stderr, "[ramjet] warn: failed to parse type JSON: %s\n",
                cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown error");
        return 0; /* Non-fatal */
    }

    /* "type" field (also aliased as "proc_type" in some configs) */
    cJSON *jtype = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!jtype || !cJSON_IsString(jtype)) {
        /* Skip entries without a type name */
        cJSON_Delete(root);
        return 0;
    }
    strncpy(pt.name, jtype->valuestring, PROC_TYPE_NAME_MAX - 1);
    pt.name[PROC_TYPE_NAME_MAX - 1] = '\0';

    /* nice */
    cJSON *jnice = cJSON_GetObjectItemCaseSensitive(root, "nice");
    if (jnice && cJSON_IsNumber(jnice)) {
        pt.nice = jnice->valueint;
    }

    /* ioclass */
    cJSON *jioclass = cJSON_GetObjectItemCaseSensitive(root, "ioclass");
    if (jioclass && cJSON_IsString(jioclass)) {
        if (io_class_from_string(jioclass->valuestring, &pt.ioclass) == 0) {
            pt.has_ioclass = 1;
        } else {
            fprintf(stderr, "[ramjet] warn: unknown ioclass '%s' in type '%s'\n",
                    jioclass->valuestring, pt.name);
        }
    }

    /* ionice */
    cJSON *jionice = cJSON_GetObjectItemCaseSensitive(root, "ionice");
    if (jionice && cJSON_IsNumber(jionice)) {
        pt.ionice = jionice->valueint;
    }

    /* cgroup */
    cJSON *jcgroup = cJSON_GetObjectItemCaseSensitive(root, "cgroup");
    if (jcgroup && cJSON_IsString(jcgroup)) {
        strncpy(pt.cgroup, jcgroup->valuestring, PROC_TYPE_CGROUP_MAX - 1);
        pt.cgroup[PROC_TYPE_CGROUP_MAX - 1] = '\0';
    }

    /* oom_score_adj (note: original Rust has typo "oom_scote_adj") */
    cJSON *joom = cJSON_GetObjectItemCaseSensitive(root, "oom_score_adj");
    if (!joom) {
        joom = cJSON_GetObjectItemCaseSensitive(root, "oom_scote_adj");
    }
    if (joom && cJSON_IsNumber(joom)) {
        pt.oom_score_adj = joom->valueint;
    }

    cJSON_Delete(root);

    proc_type_map_insert(ctx->map, &pt);
    return 0;
}

int build_types(ProcTypeMap *map) {
    proc_type_map_init(map);

    BuildTypesCtx ctx = { .map = map };
    return walk_config_dir(ANANICY_CONFIG_DIR, "types", parse_type_line, &ctx);
}
