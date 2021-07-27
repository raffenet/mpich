/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef UCX_PART_UTILS_H_INCLUDED
#define UCX_PART_UTILS_H_INCLUDED

MPL_STATIC_INLINE_PREFIX uint64_t MPIDI_UCX_part_tag(MPIR_Request * request, int partition)
{
    uint64_t tag;

    /* recv handle is upper 32 bits */
    if (request->kind == MPIR_REQUEST_KIND__PART_SEND) {
        tag = (uint64_t) MPIDI_UCX_PART_REQ(request).peer_req << 32;
    } else {
        tag = (uint64_t) request->handle << 32;
    }

    tag |= MPIDI_UCX_PART_TAG;
    tag |= partition;

    return tag;
}

MPL_STATIC_INLINE_PREFIX void MPIDI_UCX_part_recv_cb(void *request, ucs_status_t status,
                                                     const ucp_tag_recv_info_t * tag_info,
                                                     void *user_data)
{
    MPIR_Request *req = user_data;
    MPIDIU_request_complete(req);
    ucp_request_release(request);
}

MPL_STATIC_INLINE_PREFIX void MPIDI_UCX_part_send_cb(void *request, ucs_status_t status,
                                                     void *user_data)
{
    MPIR_Request *req = user_data;
    MPIDIU_request_complete(req);
    ucp_request_release(request);
}

MPL_STATIC_INLINE_PREFIX void MPIDI_UCX_part_recv(MPIR_Request * request)
{
    ucs_status_ptr_t status;
    ucp_request_param_t param = {
        .op_attr_mask =
            UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA | UCP_OP_ATTR_FIELD_DATATYPE,
        .cb.recv = MPIDI_UCX_part_recv_cb,
        .datatype = MPIDI_UCX_PART_REQ(request).ucp_dt,
        .user_data = request,
    };

    if (unlikely(MPIDI_UCX_PART_REQ(request).is_first_iteration)) {
        status = ucp_tag_recv_nbx(MPIDI_UCX_global.ctx[0].worker,
                                  (char *) MPIDI_PART_REQUEST(request,
                                                              buffer),
                                  MPIDI_UCX_PART_REQ(request).first_count,
                                  MPIDI_UCX_part_tag(request, 0), 0xffffffffffffffff, &param);
        if (status == NULL) {
            MPIDIU_request_complete(request);
        }
        MPIDI_UCX_PART_REQ(request).is_first_iteration = false;

        return;
    }

    for (int i = 0; i < MPIDI_UCX_PART_REQ(request).use_partitions; i++) {
        status = ucp_tag_recv_nbx(MPIDI_UCX_global.ctx[0].worker,
                                  (char *) MPIDI_PART_REQUEST(request,
                                                              buffer) +
                                  i * MPIDI_UCX_PART_REQ(request).use_count,
                                  MPIDI_UCX_PART_REQ(request).use_count, MPIDI_UCX_part_tag(request,
                                                                                            i),
                                  0xffffffffffffffff, &param);
        if (status == NULL) {
            MPIDIU_request_complete(request);
        }
    }
}

MPL_STATIC_INLINE_PREFIX void MPIDI_UCX_part_send(MPIR_Request * request, int partition)
{
    ucs_status_ptr_t status;
    ucp_request_param_t param = {
        .op_attr_mask =
            UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA | UCP_OP_ATTR_FIELD_DATATYPE,
        .cb.send = MPIDI_UCX_part_send_cb,
        .datatype = MPIDI_UCX_PART_REQ(request).ucp_dt,
        .user_data = request,
    };

    if (unlikely(MPIDI_UCX_PART_REQ(request).is_first_iteration)) {
        status = ucp_tag_send_nbx(MPIDI_UCX_PART_REQ(request).ep,
                                  (char *) MPIDI_PART_REQUEST(request,
                                                              buffer),
                                  MPIDI_UCX_PART_REQ(request).first_count,
                                  MPIDI_UCX_part_tag(request, 0), &param);
        if (status == NULL) {
            MPIDIU_request_complete(request);
        }
        MPIDI_UCX_PART_REQ(request).is_first_iteration = false;

        return;
    }

    status = ucp_tag_send_nbx(MPIDI_UCX_PART_REQ(request).ep,
                              (char *) MPIDI_PART_REQUEST(request,
                                                          buffer) +
                              MPIDI_UCX_PART_REQ(request).use_count * partition,
                              MPIDI_UCX_PART_REQ(request).use_count,
                              MPIDI_UCX_part_tag(request, partition), &param);
    if (status == NULL) {
        MPIDIU_request_complete(request);
    }
}

#endif /* UCX_PART_UTILS_H_INCLUDED */
