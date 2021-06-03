/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpidimpl.h"
#include "ucx_noinline.h"

void MPIDI_UCX_psend_cmpl_cb(void *request, ucs_status_t status, void *user_data)
{
    MPIR_Request *req = user_data;

    MPIR_FUNC_ENTER;

    if (unlikely(status == UCS_ERR_CANCELED))
        MPIR_STATUS_SET_CANCEL_BIT(req->status, TRUE);
    MPIDI_Request_complete_fast(req);
    ucp_request_release(request);

    MPIR_FUNC_EXIT;
}

int MPIDI_UCX_mpi_send_init(const void *buffer, MPI_Aint count,
                            MPI_Datatype datatype, int rank, int tag,
                            MPIR_Comm * comm, int attr, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_FUNC_ENTER;

    int context_offset = MPIR_PT2PT_ATTR_CONTEXT_OFFSET(attr);
    MPIDI_av_entry_t *addr = MPIDIU_comm_rank_to_av(comm, rank);
    int vci_src, vci_dst;
    MPIDI_UCX_SEND_VNIS(vci_src, vci_dst);

    MPID_THREAD_CS_ENTER(VCI, MPIDI_VCI(vci_src).lock);

    MPIR_Request *req = MPIR_Request_create_from_pool(MPIR_REQUEST_KIND__PREQUEST_SEND, vci_src, 1);
    req->comm = comm;
    MPIR_Comm_add_ref(comm);

    MPIDI_UCX_REQ(req).psend.ep = MPIDI_UCX_AV_TO_EP(addr, vci_src, vci_dst);
    MPIDI_UCX_REQ(req).psend.buffer = buffer;
    MPIDI_UCX_REQ(req).psend.count = count;
    MPIDI_UCX_REQ(req).psend.ucp_tag =
        MPIDI_UCX_init_tag(comm->context_id + context_offset, comm->rank, tag);
    MPIDI_UCX_REQ(req).psend.param.op_attr_mask =
        UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
    MPIDI_UCX_REQ(req).psend.param.cb.send = MPIDI_UCX_psend_cmpl_cb;
    MPIDI_UCX_REQ(req).psend.param.user_data = req;

    int dt_contig;
    MPIR_Datatype *dt_ptr;
    MPI_Aint dt_true_lb, data_sz;
    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);
    if (dt_contig) {
        MPIDI_UCX_REQ(req).psend.buffer = (char *) buffer + dt_true_lb;
        MPIDI_UCX_REQ(req).psend.count = data_sz;
    } else {
        MPIR_Datatype_ptr_add_ref(dt_ptr);
        MPIDI_UCX_REQ(req).psend.param.op_attr_mask |= UCP_OP_ATTR_FIELD_DATATYPE;
        MPIDI_UCX_REQ(req).psend.param.datatype = dt_ptr->dev.netmod.ucx.ucp_datatype;
    }
    *request = req;

    MPID_THREAD_CS_EXIT(VCI, MPIDI_VCI(vci_src).lock);

    MPIR_FUNC_EXIT;

    return mpi_errno;
}
