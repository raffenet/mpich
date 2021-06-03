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
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_UCX_MPI_SEND_INIT);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_UCX_MPI_SEND_INIT);

    int vni_src = MPIDI_UCX_get_vni(SRC_VCI_FROM_SENDER, comm, comm->rank, rank, tag);
    int vni_dst = MPIDI_UCX_get_vni(DST_VCI_FROM_SENDER, comm, comm->rank, rank, tag);
    MPIDI_av_entry_t *addr = MPIDIU_comm_rank_to_av(comm, rank);

    MPID_THREAD_CS_ENTER(VCI, MPIDI_VCI(vni_src).lock);

    MPIR_Request *req = MPIR_Request_create_from_pool(MPIR_REQUEST_KIND__PREQUEST_SEND, vni_src, 1);
    req->comm = comm;
    MPIR_Comm_add_ref(comm);

    MPIDI_UCX_REQ(req).ep = MPIDI_UCX_AV_TO_EP(addr, vni_src, vni_dst);
    MPIDI_UCX_REQ(req).buffer = buffer;
    MPIDI_UCX_REQ(req).count = count;
    MPIDI_UCX_REQ(req).ucp_tag =
        MPIDI_UCX_init_tag(comm->context_id + context_offset, comm->rank, tag);

    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);
    if (dt_contig) {
        MPI_Aint dt_size;
        MPIR_Datatype_get_size_macro(datatype, dt_size);
        MPIDI_UCX_REQ(req).buffer = (char *) buffer + dt_true_lb;
        MPIDI_UCX_REQ(req).ucp_dt = ucp_dt_make_contig(dt_size);
    } else {
        MPIR_Datatype_ptr_add_ref(dt_ptr);
        MPIDI_UCX_REQ(req).ucp_dt = dt_ptr->dev.netmod.ucx.ucp_datatype;
    }
    *request = req;

    MPID_THREAD_CS_EXIT(VCI, MPIDI_VCI(vni_src).lock);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_UCX_SEND_INIT);

    return mpi_errno;
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
