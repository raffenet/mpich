#include "hcoll/api/hcoll_dte.h"
#include "mpiimpl.h"
#include "hcoll_dtypes.h"

extern int hcoll_initialized;
extern int hcoll_enable;

/* This will only get called once */
int hcoll_type_commit_hook(MPIR_Datatype * dtype_p)
{
    int mpi_errno, ret;

    if (0 == hcoll_initialized) {
        mpi_errno = hcoll_initialize();
        if (mpi_errno)
            return MPI_ERR_OTHER;
    }

    if (0 == hcoll_enable) {
        return MPI_SUCCESS;
    }

    dtype_p->dev.hcoll_datatype = mpi_predefined_derived_2_hcoll(dtype_p->handle);
    if (!HCOL_DTE_IS_ZERO(dtype_p->dev.hcoll_datatype)) {
        return MPI_SUCCESS;
    }

    dtype_p->dev.hcoll_datatype = DTE_ZERO;

    ret = hcoll_create_mpi_type((void *)(intptr_t)dtype_p->handle, &dtype_p->dev.hcoll_datatype);
    if (HCOLL_SUCCESS != ret) {
        return MPI_ERR_OTHER;
    }

    return MPI_SUCCESS;
}

int hcoll_type_free_hook(MPIR_Datatype * dtype_p)
{
    if (0 == hcoll_enable) {
        return MPI_SUCCESS;
    }

    int rc = hcoll_dt_destroy(dtype_p->dev.hcoll_datatype);
    if (HCOLL_SUCCESS != rc) {
        return MPI_ERR_OTHER;
    }

    dtype_p->dev.hcoll_datatype = DTE_ZERO;

    return MPI_SUCCESS;
}
