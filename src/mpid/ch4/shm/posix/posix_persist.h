/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef POSIX_PERSIST_H_INCLUDED
#define POSIX_PERSIST_H_INCLUDED

#include "posix_impl.h"

MPL_STATIC_INLINE_PREFIX int MPIDI_POSIX_prequest_start(MPIR_Request * request)
{
    return MPIDIG_prequest_start(request);
}

#endif /* POSIX_PERSIST_H_INCLUDED */
