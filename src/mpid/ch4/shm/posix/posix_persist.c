/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "posix_noinline.h"

int MPIDI_POSIX_mpi_send_init(const void *buffer, MPI_Aint count,
                              MPI_Datatype datatype, int dest, int tag, MPIR_Comm * comm,
                              int attr, MPIR_Request ** request)
{
    return MPIDIG_mpi_send_init(buffer, count, datatype, dest, tag, comm, attr, request);
}
