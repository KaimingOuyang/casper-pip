/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * (C) 2014 by Argonne National Laboratory.
 *     See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include "cspg.h"

static int finalize_cnt = 0;

/* Destroy global ghost process object */
int CSPG_destroy_proc(void)
{
    int mpi_errno = MPI_SUCCESS;

    /* common objects */
    if (CSP_PROC.local_comm != MPI_COMM_NULL) {
        CSPG_DBG_PRINT(" free CSP_PROC.local_comm\n");
        PMPI_Comm_free(&CSP_PROC.local_comm);
    }

    if (CSP_PROC.wgroup != MPI_GROUP_NULL)
        PMPI_Group_free(&CSP_PROC.wgroup);

    CSP_PROC.local_comm = MPI_COMM_NULL;
    CSP_PROC.wgroup = MPI_GROUP_NULL;

    /* ghost-specific objects */
    if (CSP_PROC.ghost.g_local_comm != MPI_COMM_NULL) {
        CSPG_DBG_PRINT(" free CSP_PROC.ghost.g_local_comm\n");
        PMPI_Comm_free(&CSP_PROC.ghost.g_local_comm);
    }

    CSP_PROC.ghost.g_local_comm = MPI_COMM_NULL;

    return mpi_errno;
}

int CSPG_finalize(CSP_cmd_fnc_pkt_t * pkt CSP_ATTRIBUTE((unused)), int *exit_flag)
{
    int mpi_errno = MPI_SUCCESS;
    int local_nprocs, local_user_nprocs;

    finalize_cnt++;
    PMPI_Comm_size(CSP_PROC.local_comm, &local_nprocs);
    local_user_nprocs = local_nprocs - CSP_ENV.num_g;

    CSPG_DBG_PRINT(" %d/%d processes already arrived finalize...\n",
                   finalize_cnt, local_user_nprocs);

    /* wait till all local processes arrive finalize.
     * Because every ghost is shared by multiple local user processes.*/
    if (finalize_cnt < local_user_nprocs) {
        (*exit_flag) = 0;
        goto fn_exit;
    }

    CSPG_DBG_PRINT(" All processes arrived finalize.\n");
    (*exit_flag) = 1;

    CSPG_destroy_proc();

    CSPG_DBG_PRINT(" PMPI_Finalize\n");
    mpi_errno = PMPI_Finalize();
    if (mpi_errno != MPI_SUCCESS)
        goto fn_fail;

  fn_exit:
    return mpi_errno;

  fn_fail:
    CSPG_ERR_PRINT("error happened in %s, abort\n", __FUNCTION__);
    PMPI_Abort(MPI_COMM_WORLD, 0);
    goto fn_exit;
}
