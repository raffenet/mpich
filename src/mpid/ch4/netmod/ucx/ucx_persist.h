/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef UCX_PERSIST_H_INCLUDED
#define UCX_PERSIST_H_INCLUDED

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_prequest_start(MPIR_Request * request)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_Request_add_ref(request);      /* for the user */

    MPIDI_UCX_ucp_request_t *ucp_request =
        (MPIDI_UCX_ucp_request_t *) ucp_tag_send_nbx(MPIDI_UCX_REQ(request).psend.ep,
                                                     MPIDI_UCX_REQ(request).psend.buffer,
                                                     MPIDI_UCX_REQ(request).psend.count,
                                                     MPIDI_UCX_REQ(request).psend.ucp_tag,
                                                     &MPIDI_UCX_REQ(request).psend.param);
    MPIDI_UCX_CHK_REQUEST(ucp_request);

    if (ucp_request) {
        MPIR_Request_add_ref(request);  /* for the progress engine */
        MPIR_cc_inc(request->cc_ptr);
        ucp_request->req = request;
        MPIDI_UCX_REQ(request).ucp_request = ucp_request;
    }

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#endif /* UCX_PERSIST_H_INCLUDED */
