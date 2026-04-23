/*
 * RamJet - Rice clone in C
 * Copyright (c) 2026 - Ported from Rust by Alecaishere/CuerdOS Dev. Team
 *
 *
 * IoClass enumeration for I/O scheduling classes.
 */

#ifndef RAMJET_CLASS_H
#define RAMJET_CLASS_H

typedef enum {
    IO_CLASS_IDLE = 0,
    IO_CLASS_BEST_EFFORT = 1,
    IO_CLASS_REALTIME = 2,
} IoClass;

/* Parse an IoClass from a JSON string value (e.g. "idle", "best-effort", "realtime").
   Returns 0 on success, -1 on failure. */
int io_class_from_string(const char *s, IoClass *out);

/* Convert IoClass to its numeric value as used by ionice -c */
int io_class_to_num(IoClass c);

/* Convert IoClass to a human-readable string */
const char *io_class_to_string(IoClass c);

#endif /* RAMJET_CLASS_H */
