/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "ucx_noinline.h"

int MPIDI_UCX_mpi_send_init(const void *buffer, MPI_Aint count,
                            MPI_Datatype datatype, int dest, int tag,
                            MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    return MPIDIG_mpi_send_init(buffer, count, datatype, dest, tag, comm, context_offset, request);
}

int MPIDI_UCX_mpi_ssend_init(const void *buffer, MPI_Aint count,
                             MPI_Datatype datatype, int dest, int tag,
                             MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    return MPIDIG_mpi_ssend_init(buffer, count, datatype, dest, tag, comm, context_offset, request);
}

int MPIDI_UCX_mpi_bsend_init(const void *buffer, MPI_Aint count,
                             MPI_Datatype datatype, int dest, int tag,
                             MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    return MPIDIG_mpi_bsend_init(buffer, count, datatype, dest, tag, comm, context_offset, request);
}

int MPIDI_UCX_mpi_rsend_init(const void *buffer, MPI_Aint count,
                             MPI_Datatype datatype, int dest, int tag,
                             MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    return MPIDIG_mpi_rsend_init(buffer, count, datatype, dest, tag, comm, context_offset, request);
}
