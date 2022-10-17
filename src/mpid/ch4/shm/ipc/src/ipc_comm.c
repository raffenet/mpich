/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "ipc_noinline.h"
#include "ipc_types.h"

int MPIDI_IPC_mpi_comm_commit_pre_hook(MPIR_Comm * comm)
{
    return MPI_SUCCESS;
}
