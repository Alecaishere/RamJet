/*
 * rice - Ananicy clone in C
 * Copyright (c) 2024 - Ported from Rust by themadprofessor/rice
 * Licensed under MIT
 *
 * Configuration file parsing implementation.
 */

#define _GNU_SOURCE
#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <ftw.h>

/* Maximum line length for config files */
#define MAX_LINE 4096

int parse_file(FILE *fp, parse_line_cb cb, void *user_data) {
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), fp) != NULL) {
        /* Trim trailing whitespace / newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                           line[len - 1] == ' '  || line[len - 1] == '\t')) {
            line[--len] = '\0';
        }

        /* Skip empty lines and comments */
        if (len == 0 || line[0] == '#') {
            continue;
        }

        /* Call the callback with the raw JSON line */
        int ret = cb(line, user_data);
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

/* Helper: check if a filename ends with the given extension.
   ext should NOT include the dot (e.g. "rules" matches file.rules). */
static int has_extension(const char *filename, const char *ext) {
    if (!filename || !ext) return 0;

    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;

    /* dot points to the '.', so dot+1 is the extension */
    return strcmp(dot + 1, ext) == 0;
}

/* Context for nftw callback */
typedef struct {
    const char *ext;
    parse_line_cb cb;
    void *user_data;
} walk_ctx_t;

static walk_ctx_t g_walk_ctx;

static int nftw_callback(const char *fpath, const struct stat *sb,
                         int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)ftwbuf;

    if (typeflag != FTW_F) {
        return 0; /* Skip non-regular files */
    }

    /* Check file extension */
    const char *basename = strrchr(fpath, '/');
    basename = basename ? basename + 1 : fpath;

    if (!has_extension(basename, g_walk_ctx.ext)) {
        return 0;
    }

    /* Open and parse the file */
    FILE *fp = fopen(fpath, "r");
    if (!fp) {
        fprintf(stderr, "[rice] warn: failed to open %s: %s\n",
                fpath, strerror(errno));
        return 0; /* Non-fatal: continue walking */
    }

    int ret = parse_file(fp, g_walk_ctx.cb, g_walk_ctx.user_data);
    fclose(fp);

    if (ret != 0) {
        fprintf(stderr, "[rice] warn: error parsing %s\n", fpath);
    }

    return 0;
}

int walk_config_dir(const char *root_dir, const char *ext,
                    parse_line_cb cb, void *user_data) {
    g_walk_ctx.ext = ext;
    g_walk_ctx.cb = cb;
    g_walk_ctx.user_data = user_data;

    /* Use nftw to walk the directory tree, up to 16 open fds */
    int ret = nftw(root_dir, nftw_callback, 16, FTW_PHYS);
    if (ret != 0 && errno != 0) {
        fprintf(stderr, "[rice] warn: failed to walk %s: %s\n",
                root_dir, strerror(errno));
        return -1;
    }

    return 0;
}
