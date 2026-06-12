/*
 * puls-benchmark.h
 *
 * Disk performance benchmark module.
 *
 * Copyright (C) 2026 Barın Güzeldemirci <baringuzeldemir@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef PULS_BENCHMARK_H
#define PULS_BENCHMARK_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
    PULS_BENCHMARK_TEST_SEQ1M_Q8T1,
    PULS_BENCHMARK_TEST_SEQ1M_Q1T1,
    PULS_BENCHMARK_TEST_RND4K_Q32T1,
    PULS_BENCHMARK_TEST_RND4K_Q1T1,
    PULS_BENCHMARK_TEST_COUNT
} PulsBenchmarkTest;

typedef struct {
    gdouble read_mbs;
    gdouble write_mbs;
    gboolean read_done;
    gboolean write_done;
} PulsBenchmarkResult;

typedef void (*PulsBenchmarkProgressCallback) (PulsBenchmarkTest current_test,
                                               gboolean is_write,
                                               gdouble progress,
                                               gdouble current_speed_mbs,
                                               gpointer user_data);

typedef void (*PulsBenchmarkFinishedCallback) (const PulsBenchmarkResult results[PULS_BENCHMARK_TEST_COUNT],
                                               gboolean cancelled,
                                               const gchar *error_msg,
                                               gpointer user_data);

void puls_benchmark_run_async (const gchar *test_directory,
                              guint num_runs,
                              guint64 test_size_bytes,
                              PulsBenchmarkProgressCallback progress_cb,
                              PulsBenchmarkFinishedCallback finished_cb,
                              GCancellable *cancellable,
                              gpointer user_data);

G_END_DECLS

#endif /* PULS_BENCHMARK_H */
