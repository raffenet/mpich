/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef UCX_PART_H_INCLUDED
#define UCX_PART_H_INCLUDED

#include "ucx_impl.h"
#include "ucx_part_utils.h"

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_part_start(MPIR_Request * request)
{
    MPIR_Request_add_ref(request);

    /* FIXME: to avoid this we need to modify the noncontig type handling
     * to not free the datatype for persistent/partitioned communication */
    int is_contig;
    MPIR_Datatype_is_contig(MPIDI_PART_REQUEST(request, datatype), &is_contig);
    if (!is_contig) {
        MPIR_Datatype *dt_ptr;

        MPIR_Datatype_get_ptr(MPIDI_PART_REQUEST(request, datatype), dt_ptr);
        MPIR_Assert(dt_ptr != NULL);
        MPIR_Datatype_ptr_add_ref(dt_ptr);
    }

    if (unlikely(MPIDI_UCX_PART_REQ(request).is_first_iteration)) {
        MPIR_cc_set(&request->cc, 1);
        if (request->kind == MPIR_REQUEST_KIND__PART_SEND) {
            MPIR_cc_set(&MPIDI_UCX_PART_REQ(request).parts_left, request->u.part.partitions);
        } else {
            MPIDI_UCX_part_recv(request);
        }

        return MPI_SUCCESS;
    }

    MPIR_cc_set(&request->cc, MPIDI_UCX_PART_REQ(request).use_partitions);
    if (request->kind == MPIR_REQUEST_KIND__PART_RECV) {
        MPIDI_UCX_part_recv(request);
    } else {    /* send request */
        if (MPIDI_UCX_PART_REQ(request).use_partitions == 1) {
            MPIR_cc_set(&MPIDI_UCX_PART_REQ(request).parts_left, request->u.part.partitions);
        }
    }

    return MPI_SUCCESS;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_mpi_pready_range(int partition_low,
                                                       int partition_high, MPIR_Request * request)
{
    MPID_THREAD_CS_ENTER(VCI, MPIDI_VCI(0).lock);

    if (unlikely(MPIDI_UCX_PART_REQ(request).is_first_iteration)) {
        for (int i = partition_low; i <= partition_high; i++) {
            MPIR_cc_dec(&MPIDI_UCX_PART_REQ(request).parts_left);
        }

        if (MPIR_cc_get(MPIDI_UCX_PART_REQ(request).parts_left) == 0 &&
            MPIDI_UCX_PART_REQ(request).peer_req != MPI_REQUEST_NULL) {
            MPIDI_UCX_part_send(request, 0);
        }

        goto fn_exit;
    }

    for (int i = partition_low; i <= partition_high; i++) {
        if (MPIDI_UCX_PART_REQ(request).use_partitions == 1) {
            MPIR_cc_dec(&MPIDI_UCX_PART_REQ(request).parts_left);
        } else {
            MPIDI_UCX_part_send(request, i);
        }
    }

    if (MPIDI_UCX_PART_REQ(request).use_partitions == 1 &&
        MPIR_cc_get(MPIDI_UCX_PART_REQ(request).parts_left) == 0) {
        MPIDI_UCX_part_send(request, 0);
    }

  fn_exit:
    MPID_THREAD_CS_EXIT(VCI, MPIDI_VCI(0).lock);
    return MPI_SUCCESS;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_mpi_pready_list(int length, int array_of_partitions[],
                                                      MPIR_Request * request)
{
    MPID_THREAD_CS_ENTER(VCI, MPIDI_VCI(0).lock);

    if (unlikely(MPIDI_UCX_PART_REQ(request).is_first_iteration)) {
        for (int i = 0; i < length; i++) {
            MPIR_cc_dec(&MPIDI_UCX_PART_REQ(request).parts_left);
        }

        if (MPIR_cc_get(MPIDI_UCX_PART_REQ(request).parts_left) == 0 &&
            MPIDI_UCX_PART_REQ(request).peer_req != MPI_REQUEST_NULL) {
            MPIDI_UCX_part_send(request, 0);
        }

        goto fn_exit;
    }

    for (int i = 0; i < length; i++) {
        if (MPIDI_UCX_PART_REQ(request).use_partitions == 1) {
            MPIR_cc_dec(&MPIDI_UCX_PART_REQ(request).parts_left);
        } else {
            MPIDI_UCX_part_send(request, array_of_partitions[i]);
        }
    }

    if (MPIDI_UCX_PART_REQ(request).use_partitions == 1 &&
        MPIR_cc_get(MPIDI_UCX_PART_REQ(request).parts_left) == 0) {
        MPIDI_UCX_part_send(request, 0);
    }

  fn_exit:
    MPID_THREAD_CS_EXIT(VCI, MPIDI_VCI(0).lock);
    return MPI_SUCCESS;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_mpi_parrived(MPIR_Request * request, int partition, int *flag)
{
    return MPIDIG_mpi_parrived(request, partition, flag);
}

#endif /* UCX_PART_H_INCLUDED */
