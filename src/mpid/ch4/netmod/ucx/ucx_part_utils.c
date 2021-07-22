/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

typedef struct MPIDI_UCX_part_send_init_msg_t {
    int src_rank;
    int tag;
    MPIR_Context_id_t context_id;
    MPI_Request sreq;
    MPI_Aint data_sz;           /* size of entire send data */
} MPIDI_UCX_part_send_init_msg_t;

typedef struct MPIDI_UCX_part_recv_init_msg_t {
    MPI_Request sreq;
    MPI_Request rreq;
} MPIDI_UCX_part_cts_msg_t;

/* Callback used on receiver, triggered when received the send_init AM.
 * It tries to match with a local posted part_rreq or store as unexpected. */
int MPIDI_UCX_part_send_init_target_msg_cb(int handler_id, void *am_hdr, void *data,
                                           MPI_Aint in_data_sz, int is_local, int is_async,
                                           MPIR_Request ** req)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_UCX_PART_SEND_INIT_TARGET_MSG_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_UCX_PART_SEND_INIT_TARGET_MSG_CB);

    MPIDI_UCX_part_send_init_msg_t *msg_hdr = am_hdr;
    MPIR_Request *posted_req =
        MPIDIG_rreq_dequeue(msg_hdr->src_rank, msg_hdr->tag, msg_hdr->context_id,
                            &MPIDI_global.part_posted_list, MPIDIG_PART);
    if (posted_req) {
        MPIDI_UCX_PART_REQ(posted_req).peer_req = msg_hdr->sreq;
        MPIDI_UCX_PART_REQ(posted_req).data_sz = msg_hdr->data_sz;

        if (MPIR_Part_request_is_active(posted_req)) {
            abort();
        }
    } else {
        MPIR_Request *unexp_req;

        /* Create temporary unexpected request, freed when matched with a precv_init. */
        MPIDI_CH4_REQUEST_CREATE(unexp_req, MPIR_REQUEST_KIND__PART_RECV, 0, 1);
        MPIR_ERR_CHKANDSTMT(unexp_req == NULL, mpi_errno, MPIX_ERR_NOREQ, goto fn_fail,
                            "**nomemreq");

        MPIDI_PART_REQUEST(unexp_req, rank) = msg_hdr->src_rank;
        MPIDI_PART_REQUEST(unexp_req, tag) = msg_hdr->tag;
        MPIDI_PART_REQUEST(unexp_req, context_id) = msg_hdr->context_id;
        MPIDI_UCX_PART_REQ(unexp_req).peer_req = msg_hdr->sreq;
        MPIDI_UCX_PART_REQ(unexp_req).data_sz = msg_hdr->data_sz;

        MPIDIG_enqueue_request(unexp_req, &MPIDI_global.part_unexp_list, MPIDIG_PART);
    }

    if (is_async)
        *req = NULL;

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_UCX_PART_SEND_INIT_TARGET_MSG_CB);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* Callback used on sender to set receiver information */
int MPIDI_UCX_part_recv_init_target_msg_cb(int handler_id, void *am_hdr, void *data,
                                           MPI_Aint in_data_sz, int is_local, int is_async,
                                           MPIR_Request ** req)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_UCX_PART_RECV_INIT_TARGET_MSG_CB);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_UCX_PART_RECV_INIT_TARGET_MSG_CB);

    MPIDIG_part_cts_msg_t *msg_hdr = am_hdr;
    MPIR_Request *part_sreq;
    MPIR_Request_get_ptr(msg_hdr->sreq, part_sreq);
    MPIR_Assert(part_sreq);

    MPIDI_UCX_PART_REQ(part_sreq, peer_req) = msg_hdr->rreq;
    if (MPIR_Part_request_is_active(part_sreq)) {
        abort();
    }

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_UCX_PART_RECV_INIT_TARGET_MSG_CB);
    return mpi_errno;
}

void MPIDI_UCX_part_am_init(void)
{
    MPIDIG_am_reg_cb(MPIDI_UCX_PART_SEND_INIT, NULL, &MPIDI_UCX_part_send_init_target_msg_cb);
    MPIDIG_am_reg_cb(MPIDI_UCX_PART_RECV_INIT, NULL, &MPIDI_UCX_part_recv_init_target_msg_cb);
}
