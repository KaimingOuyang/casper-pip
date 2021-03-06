/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * (C) 2016 by Argonne National Laboratory.
 *     See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>
#include "ctest.h"

/*
 * This test checks single-way isend and irecv with wait.
 */

#define NUM_OPS 10
#ifdef TEST_LMSG
#define COUNT 10000     /* count of double */
#else
#define COUNT 100       /* count of double */
#endif

double *sbuf = NULL, *rbuf = NULL;
int rank, nprocs;
MPI_Win sbuf_win = MPI_WIN_NULL, rbuf_win = MPI_WIN_NULL;
MPI_Comm comm_world = MPI_COMM_NULL;
int ITER = 2;

static int check_stat(MPI_Status stat, int peer, int tag)
{
    int errs = 0;

#if !defined(USE_ANYSRC_NOTAG)
    if (stat.MPI_TAG != tag) {
        fprintf(stderr, "[%d] stat.MPI_TAG %d != %d\n", rank, stat.MPI_TAG, tag);
        fflush(stderr);
        errs++;
    }
#endif
    if (stat.MPI_SOURCE != peer) {
        fprintf(stderr, "[%d] stat.MPI_SOURCE %d != %d\n", rank, stat.MPI_SOURCE, peer);
        fflush(stderr);
        errs++;
    }
    if (stat.MPI_ERROR != MPI_SUCCESS) {
        fprintf(stderr, "[%d] stat.MPI_ERROR 0x%x != MPI_SUCCESS 0x%x\n",
                rank, stat.MPI_ERROR, MPI_SUCCESS);
        fflush(stderr);
        errs++;
    }

    return errs;
}

static int run_test(void)
{
    int i, x, c, errs = 0, errs_total = 0;
    int peer;
    MPI_Request req;
    MPI_Status stat;

    if (rank % 2)       /* receive only */
        peer = (rank - 1 + nprocs) % nprocs;
    else        /* send only */
        peer = (rank + 1) % nprocs;

    for (x = 0; x < ITER; x++) {

        if (rank % 2) { /* receive only */
            for (i = 0; i < NUM_OPS; i++) {
#if defined(USE_ANYSRC) || defined(USE_ANYSRC_ANYTAG)
                MPI_Irecv(&rbuf[i * COUNT], COUNT, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG,
                          comm_world, &req);
#elif defined(USE_ANYSRC_NOTAG)
                /* tag is ignored */
                MPI_Irecv(&rbuf[i * COUNT], COUNT, MPI_DOUBLE, MPI_ANY_SOURCE, 0, comm_world, &req);
#else
                MPI_Irecv(&rbuf[i * COUNT], COUNT, MPI_DOUBLE, peer, i, comm_world, &req);
#endif
                stat.MPI_ERROR = MPI_SUCCESS;
                MPI_Wait(&req, &stat);

                /* check completed receive */
                for (c = 0; c < COUNT; c++) {
                    if (CTEST_double_diff(rbuf[i * COUNT + c], 1.0 * i * COUNT + c + peer)) {
                        fprintf(stderr,
                                "[%d] rbuf[%d] %.1lf != %.1lf\n",
                                rank, i * COUNT + c,
                                rbuf[i * COUNT + c], 1.0 * i * COUNT + c + peer);
                        fflush(stderr);
                        errs++;
                    }
                }
                errs += check_stat(stat, peer, i);
            }
        }
        else {  /* send only */
            for (i = 0; i < NUM_OPS; i++) {
#if defined(USE_ANYSRC_NOTAG)
                /* tag is ignored */
                MPI_Isend(&sbuf[i * COUNT], COUNT, MPI_DOUBLE, peer, 0, comm_world, &req);
#else
                MPI_Isend(&sbuf[i * COUNT], COUNT, MPI_DOUBLE, peer, i, comm_world, &req);
#endif
                MPI_Wait(&req, &stat);
            }
        }
    }

    MPI_Allreduce(&errs, &errs_total, 1, MPI_INT, MPI_SUM, comm_world);
    return errs_total;
}

int main(int argc, char *argv[])
{
    int i, errs = 0;
    MPI_Info info = MPI_INFO_NULL;
    MPI_Comm shm_comm = MPI_COMM_NULL;

    MPI_Init(&argc, &argv);

    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (nprocs < 2 || nprocs % 2) {
        fprintf(stderr, "Please run using power of two number of processes\n");
        goto exit;
    }

    MPI_Info_create(&info);

    /* Register as shared buffer in Casper. */
    MPI_Info_set(info, (char *) "shmbuf_regist", (char *) "true");
    MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, rank, info, &shm_comm);

    MPI_Win_allocate_shared(sizeof(double) * NUM_OPS * COUNT, sizeof(double),
                            MPI_INFO_NULL, shm_comm, &sbuf, &sbuf_win);
    MPI_Win_allocate_shared(sizeof(double) * NUM_OPS * COUNT, sizeof(double),
                            MPI_INFO_NULL, shm_comm, &rbuf, &rbuf_win);

    for (i = 0; i < NUM_OPS * COUNT; i++) {
        sbuf[i] = 1.0 * i + rank;
        rbuf[i] = sbuf[i] * -1;
    }

    MPI_Info_free(&info);
#ifdef USE_NOINFO
    /* Default any_src(may with specific tag), disable asynchronous progress */
    info = MPI_INFO_NULL;
#else
    MPI_Info_create(&info);
#   if defined(USE_ANYSRC)
    /* any_src(may with specific tag), disable asynchronous progress */
    MPI_Info_set(info, (char *) "wildcard_used", (char *) "anysrc");
#   elif defined(USE_ANYSRC_NOTAG) || defined(USE_ANYSRC_ANYTAG)
    /* Use communicator duplicating approach */
    MPI_Info_set(info, (char *) "wildcard_used", (char *) "anysrc|anytag_notag");
#   else
    /* Use tag encoding approach */
    MPI_Info_set(info, (char *) "wildcard_used", (char *) "none");
#   endif
#endif

#ifdef USE_OFFLOAD_MIN_MSGSZ
    if (info != MPI_INFO_NULL)
        MPI_Info_set(info, (char *) "offload_min_msgsz", (char *) "1");
#endif

#ifdef USE_DERIVED_DTYPE
    if (info != MPI_INFO_NULL)
        MPI_Info_set(info, (char *) "datatype_used", (char *) "derived");
#elif defined(USE_NODTYPE)
    /* Default datatype_used = predefined | derived. */
#else
    if (info != MPI_INFO_NULL)
        MPI_Info_set(info, (char *) "datatype_used", (char *) "predefined");
#endif
    MPI_Comm_dup_with_info(MPI_COMM_WORLD, info, &comm_world);

    MPI_Barrier(comm_world);
    errs = run_test();

  exit:
    if (rank == 0)
        CTEST_report_result(errs);

    if (info != MPI_INFO_NULL)
        MPI_Info_free(&info);
    if (sbuf_win != MPI_WIN_NULL)
        MPI_Win_free(&sbuf_win);
    if (rbuf_win != MPI_WIN_NULL)
        MPI_Win_free(&rbuf_win);
    if (shm_comm != MPI_COMM_NULL)
        MPI_Comm_free(&shm_comm);
    if (comm_world != MPI_COMM_NULL)
        MPI_Comm_free(&comm_world);

    MPI_Finalize();

    return 0;
}
