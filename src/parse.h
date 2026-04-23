/*
 * rice - Ananicy clone in C
 * Copyright (c) 2024 - Ported from Rust by themadprofessor/rice
 * Licensed under MIT
 *
 * Configuration file parsing: walks /etc/ananicy.d for .rules, .types, .cgroups
 * files and parses each line as JSON, invoking a callback for each parsed object.
 */

#ifndef RICE_PARSE_H
#define RICE_PARSE_H

#include <stdio.h>

/* Callback type: called for each parsed JSON object from a config line.
   'json_line' is the raw JSON string, 'user_data' is caller-provided context.
   Return 0 to continue, -1 to stop with error. */
typedef int (*parse_line_cb)(const char *json_line, void *user_data);

/* Parse a single opened file, line by line.
   Skips blank lines and lines starting with '#'.
   Calls 'cb' for each valid JSON line found.
   Returns 0 on success, -1 on error. */
int parse_file(FILE *fp, parse_line_cb cb, void *user_data);

/* Walk 'root_dir' recursively, looking for files with extension 'ext'
   (e.g. "rules", "types", "cgroups").
   Opens each matching file and parses it with parse_file().
   Returns 0 on success, -1 on fatal error (individual file errors are logged
   but do not stop the walk). */
int walk_config_dir(const char *root_dir, const char *ext,
                    parse_line_cb cb, void *user_data);

#endif /* RICE_PARSE_H */
