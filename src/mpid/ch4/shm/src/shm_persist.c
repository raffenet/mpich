/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "shm_noinline.h"
#include "../posix/posix_noinline.h"

int MPIDI_SHM_mpi_send_init(const void *buffer, MPI_Aint count,
                            MPI_Datatype datatype, int dest, int tag,
                            MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    return MPIDI_POSIX_mpi_send_init(buffer, count, datatype, dest, tag, comm, context_offset,
                                     request);
}
