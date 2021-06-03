/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef UCX_PERSIST_H_INCLUDED
#define UCX_PERSIST_H_INCLUDED

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_prequest_start(MPIR_Request * request)
{
    return MPIDIG_prequest_start(request);
}

#endif /* UCX_PERSIST_H_INCLUDED */
