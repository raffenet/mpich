/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "ucx_impl.h"
#include "ucx_noinline.h"

int MPIDI_UCX_mpi_psend_init(void *buf, int partitions, MPI_Aint count,
                             MPI_Datatype datatype, int dest, int tag,
                             MPIR_Comm * comm, MPIR_Info * info,
                             MPIDI_av_entry_t * av, MPIR_Request ** request)
{
    return MPIDIG_mpi_psend_init(buf, partitions, count, datatype, dest, tag, comm,
                                 info, 0 /*is_local */ , request);
}

int MPIDI_UCX_mpi_precv_init(void *buf, int partitions, MPI_Aint count,
                             MPI_Datatype datatype, int source, int tag,
                             MPIR_Comm * comm, MPIR_Info * info,
                             MPIDI_av_entry_t * av, MPIR_Request ** request)
{
    return MPIDIG_mpi_precv_init(buf, partitions, count, datatype, source, tag, comm,
                                 info, 0 /*is_local */ , request);
}

int MPIDI_UCX_precv_matched_hook(MPIR_Request * part_req)
{
    return MPI_SUCCESS;
}
