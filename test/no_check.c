/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * (C) 2014 by Argonne National Laboratory.
 *     See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <mpi.h>
#include "ctest.h"

/*
 * This test checks lockall and lock with MPI_MODE_NOCHECK.
 */
#define NUM_OPS 5
#define CHECK
#define OUTPUT_FAIL_DETAIL

double *winbuf = NULL;
double *locbuf = NULL;
double *checkbuf = NULL;
int rank, nprocs;
MPI_Win win = MPI_WIN_NULL;
int ITER = 2;

static void change_data(int nop, int x)
{
    int dst, i;
    for (dst = 0; dst < nprocs; dst++) {
        for (i = 0; i < nop; i++) {
            locbuf[dst * nop + i] = 1.0 * (x + 1) * (i + 1) + nop * dst;
        }
    }
}

static int check_data_all(int nop)
{
    int errs = 0;
    /* note that it is in an epoch */
    int dst, i;

    memset(checkbuf, 0, NUM_OPS * nprocs * sizeof(double));

    for (dst = 0; dst < nprocs; dst++) {
        MPI_Get(&checkbuf[dst * nop], nop, MPI_DOUBLE, dst, 0, nop, MPI_DOUBLE, win);
    }
    MPI_Win_flush_all(win);

    for (dst = 0; dst < nprocs; dst++) {
        for (i = 0; i < nop; i++) {
            if (CTEST_precise_double_diff(checkbuf[dst * nop + i], locbuf[dst * nop + i])) {
                fprintf(stderr, "[%d] winbuf[%d] %.1lf != %.1lf\n", dst, i,
                        checkbuf[dst * nop + i], locbuf[dst * nop + i]);
                errs++;
            }
        }
    }

#ifdef OUTPUT_FAIL_DETAIL
    if (errs > 0) {
        CTEST_print_double_array(locbuf, nop * nprocs, "locbuf");
        CTEST_print_double_array(checkbuf, nop * nprocs, "winbuf");
    }
#endif

    return errs;
}

static int check_data(int nop, int dst)
{
    int errs = 0;
    /* note that it is in an epoch */
    int i;

    memset(checkbuf, 0, NUM_OPS * nprocs * sizeof(double));

    MPI_Get(&checkbuf[dst * nop], nop, MPI_DOUBLE, dst, 0, nop, MPI_DOUBLE, win);
    MPI_Win_flush(dst, win);

    for (i = 0; i < nop; i++) {
        if (CTEST_precise_double_diff(checkbuf[dst * nop + i], locbuf[dst * nop + i])) {
            fprintf(stderr, "[%d] winbuf[%d] %.1lf != %.1lf\n", dst, i,
                    checkbuf[dst * nop + i], locbuf[dst * nop + i]);
            errs++;
        }
    }
#ifdef OUTPUT_FAIL_DETAIL
    if (errs > 0) {
        CTEST_print_double_array(&locbuf[dst * nop], nop, "locbuf");
        CTEST_print_double_array(&checkbuf[dst * nop], nop, "winbuf");
    }
#endif
    return errs;
}

static int run_test1(int nop)
{
    int i, x, errs = 0;
    int dst;

    if (rank == 0) {

        /* check lock_all/put[all] & flush_all + NOP * put[all] & flush_all/unlock_all */
        for (x = 0; x < ITER; x++) {
            MPI_Win_lock_all(MPI_MODE_NOCHECK, win);

            /* change date in every iteration */
            change_data(nop, x);

            for (dst = 0; dst < nprocs; dst++) {
                MPI_Put(&locbuf[dst * nop], 1, MPI_DOUBLE, dst, 0, 1, MPI_DOUBLE, win);
            }
            MPI_Win_flush_all(win);

            for (dst = 0; dst < nprocs; dst++) {
                for (i = 1; i < nop; i++) {
                    MPI_Put(&locbuf[dst * nop + i], 1, MPI_DOUBLE, dst, i, 1, MPI_DOUBLE, win);
#ifdef MVA
                    MPI_Win_flush(dst, win);    /* use it to poke progress in order to finish local CQEs */
#endif
                }
            }
            MPI_Win_flush_all(win);

            /* check in every iteration */
            errs += check_data_all(nop);

            MPI_Win_unlock_all(win);
        }
    }

    MPI_Bcast(&errs, 1, MPI_INT, 0, MPI_COMM_WORLD);

    return errs;
}

static int run_test2(int nop)
{
    int i, x, errs = 0;
    int dst;

    if (rank == 0) {

        /* check lock_all/NOP * put[all] & flush_all/unlock_all */
        for (x = 0; x < ITER; x++) {
            MPI_Win_lock_all(MPI_MODE_NOCHECK, win);

            /* change date in every interation */
            change_data(nop, x);

            for (dst = 0; dst < nprocs; dst++) {
                for (i = 0; i < nop; i++) {
                    MPI_Put(&locbuf[dst * nop + i], 1, MPI_DOUBLE, dst, i, 1, MPI_DOUBLE, win);
#ifdef MVA
                    MPI_Win_flush(dst, win);    /* use it to poke progress in order to finish local CQEs */
#endif
                }
            }
            MPI_Win_flush_all(win);

            /* check in every iteration */
            errs += check_data_all(nop);

            MPI_Win_unlock_all(win);
        }
    }

    MPI_Bcast(&errs, 1, MPI_INT, 0, MPI_COMM_WORLD);

    return errs;
}

/* following tests only run over more than 4 processes */
static int run_test3(int nop)
{
    int i, x, errs = 0, errs_total = 0;
    int dst_nprocs = nprocs / 2;
    int dst = rank + 1;

    if (nprocs < 4) {
        return errs;
    }

    if (rank == 0 || rank == dst_nprocs) {

        /* check lock/put & flush/unlock */
        for (x = 0; x < ITER; x++) {
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, dst, MPI_MODE_NOCHECK, win);

            /* change date in every iteration */
            change_data(nop, x);

            for (i = 0; i < nop; i++) {
                MPI_Put(&locbuf[dst * nop + i], 1, MPI_DOUBLE, dst, i, 1, MPI_DOUBLE, win);
            }

            MPI_Win_flush(dst, win);

            /* check in every iteration */
            errs += check_data(nop, dst);

            MPI_Win_unlock(dst, win);
        }
    }

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    return errs_total;
}

static int run_test4(int nop)
{
    int i, x, errs = 0, errs_total = 0;
    int dst_nprocs = nprocs / 2;
    int dst = rank + 1;

    if (nprocs < 4) {
        return errs;
    }

    if (rank == 0 || rank == dst_nprocs) {

        /* check lock/put & flush + NOP * put & flush/unlock */
        for (x = 0; x < ITER; x++) {
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, dst, MPI_MODE_NOCHECK, win);

            /* change date in every iteration */
            change_data(nop, x);

            MPI_Put(&locbuf[dst * nop], 1, MPI_DOUBLE, dst, 0, 1, MPI_DOUBLE, win);
            MPI_Win_flush(dst, win);

            for (i = 1; i < nop; i++) {
                MPI_Put(&locbuf[dst * nop + i], 1, MPI_DOUBLE, dst, i, 1, MPI_DOUBLE, win);
            }
            MPI_Win_flush(dst, win);

            /* check in every iteration */
            errs += check_data(nop, dst);

            MPI_Win_unlock(dst, win);
        }
    }

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    return errs_total;
}

int main(int argc, char *argv[])
{
    int size = NUM_OPS;
    int i, errs = 0;

    MPI_Init(&argc, &argv);

    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (nprocs < 2) {
        fprintf(stderr, "Please run using at least 2 processes\n");
        goto exit;
    }

    locbuf = calloc(NUM_OPS * nprocs, sizeof(double));
    checkbuf = calloc(NUM_OPS * nprocs, sizeof(double));
    for (i = 0; i < NUM_OPS * nprocs; i++) {
        locbuf[i] = 1.0 * i;
    }

    /* size in byte */
    MPI_Win_allocate(sizeof(double) * NUM_OPS, sizeof(double), MPI_INFO_NULL,
                     MPI_COMM_WORLD, &winbuf, &win);

    /*
     * P0: 0 + [0:NOPS-1] * nprocs
     * P1: 1 + [0:NOPS-1] * nprocs
     * ...
     */

    /* reset window */
    MPI_Win_lock(MPI_LOCK_SHARED, rank, 0, win);
    for (i = 0; i < NUM_OPS; i++) {
        winbuf[i] = 0.0;
    }
    MPI_Win_unlock(rank, win);

    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test1(size);
    if (errs)
        goto exit;

    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test2(size);
    if (errs)
        goto exit;

    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test3(size);
    if (errs)
        goto exit;

    MPI_Barrier(MPI_COMM_WORLD);
    errs = run_test4(size);
    if (errs)
        goto exit;

  exit:
    if (rank == 0)
        CTEST_report_result(errs);

    if (win != MPI_WIN_NULL)
        MPI_Win_free(&win);
    if (locbuf)
        free(locbuf);
    if (checkbuf)
        free(checkbuf);

    MPI_Finalize();

    return 0;
}
