/*
 * RamJet - Rice clone in C
 * Copyright (c) 2026 - Ported from Rust by Alecaishere/CuerdOS Dev. Team
 *
 * Main entry point: loads configuration, registers signal handlers,
 * and runs the main loop applying rules every 5 seconds.
 */

#define _GNU_SOURCE
#include "ramjet.h"
#include "rule.h"
#include "cgroup.h"
#include "proc_type.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

/* Global flag for graceful termination */
static volatile sig_atomic_t g_terminated = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_terminated = 1;
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        fprintf(stderr, "[ramjet] error: failed to register SIGINT handler: %s\n",
                strerror(errno));
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        fprintf(stderr, "[ramjet] error: failed to register SIGTERM handler: %s\n",
                strerror(errno));
    }
}

/* Sleep for the specified duration, but wake up early if terminated.
   Returns 0 if the full sleep completed, 1 if interrupted by termination. */
static int interruptible_sleep(time_t seconds) {
    struct timespec remaining = {
        .tv_sec = seconds,
        .tv_nsec = 0,
    };

    while (!g_terminated) {
        struct timespec req = remaining;
        if (nanosleep(&req, &remaining) == 0) {
            return 0; /* Full sleep completed */
        }
        if (errno != EINTR) {
            /* Some real error */
            perror("[ramjet] nanosleep");
            return -1;
        }
        /* EINTR: check if we should terminate */
        if (g_terminated) {
            return 1;
        }
    }

    return 1;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    setup_signals();

    /* Load cgroups */
    CgroupMap cgroups;
    if (build_cgroups(&cgroups) != 0) {
        fprintf(stderr, "[ramjet] warn: failed to load some cgroups\n");
    }

    /* Count cgroups */
    int cgroup_count = 0;
    for (int i = 0; i < CGROUP_MAP_SIZE; i++) {
        CgroupEntry *e = cgroups.buckets[i];
        while (e) { cgroup_count++; e = e->next; }
    }
    fprintf(stderr, "[ramjet] info: %d cgroups loaded\n", cgroup_count);

    /* Load types */
    ProcTypeMap types;
    if (build_types(&types) != 0) {
        fprintf(stderr, "[ramjet] warn: failed to load some types\n");
    }

    /* Count types */
    int type_count = 0;
    for (int i = 0; i < PROC_TYPE_MAP_SIZE; i++) {
        ProcTypeEntry *e = types.buckets[i];
        while (e) { type_count++; e = e->next; }
    }
    fprintf(stderr, "[ramjet] info: %d types loaded\n", type_count);

    /* Load rules */
    RuleMap rules;
    if (build_rules(&rules, &types) != 0) {
        fprintf(stderr, "[ramjet] warn: failed to load some rules\n");
    }

    /* Count rules */
    int rule_count = 0;
    for (int i = 0; i < RULE_MAP_SIZE; i++) {
        RuleEntry *e = rules.buckets[i];
        while (e) { rule_count++; e = e->next; }
    }
    fprintf(stderr, "[ramjet] info: %d rules loaded\n", rule_count);

    /* Main loop */
    while (!g_terminated) {
        int errors = apply_all_rules(&rules, &types, &cgroups);
        if (errors > 0) {
            fprintf(stderr, "[ramjet] error: %d errors during rule application\n", errors);
        }

        /* Sleep 5 seconds, interruptible by signals */
        interruptible_sleep(5);
    }

    /* Cleanup */
    rule_map_free(&rules);
    proc_type_map_free(&types);
    cgroup_map_free(&cgroups);

    fprintf(stderr, "[ramjet] info: shutting down\n");
    return 0;
}
