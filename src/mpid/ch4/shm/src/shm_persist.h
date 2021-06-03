/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef SHM_PERSIST_H_INCLUDED
#define SHM_PERSIST_H_INCLUDED

#include <shm.h>
#include "../posix/shm_inline.h"

MPL_STATIC_INLINE_PREFIX int MPIDI_SHM_prequest_start(MPIR_Request * request)
{
    return MPIDI_POSIX_prequest_start(request);
}

#endif /* SHM_PERSIST_H_INCLUDED */
