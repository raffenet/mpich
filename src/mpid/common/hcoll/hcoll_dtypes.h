/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2014 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef HCOLL_DTYPES_H_INCLUDED
#define HCOLL_DTYPES_H_INCLUDED
#include "hcoll/api/hcoll_dte.h"
#include "hcoll.h"

enum {
    TRY_FIND_DERIVED,
    NO_DERIVED
};

int hcoll_type_commit_hook(MPIR_Datatype * dtype_p);
int hcoll_type_free_hook(MPIR_Datatype * dtype_p);

static dte_data_representation_t datatype_2_dte_data_rep(MPI_Datatype datatype)
{
    switch (datatype) {
        case MPI_CHAR:
        case MPI_SIGNED_CHAR:
            return DTE_BYTE;
        case MPI_SHORT:
            return DTE_INT16;
        case MPI_INT:
            return DTE_INT32;
        case MPI_LONG:
        case MPI_LONG_LONG:
            return DTE_INT64;
            /* return DTE_INT128; */
        case MPI_BYTE:
        case MPI_UNSIGNED_CHAR:
            return DTE_UBYTE;
        case MPI_UNSIGNED_SHORT:
            return DTE_UINT16;
        case MPI_UNSIGNED:
            return DTE_UINT32;
        case MPI_UNSIGNED_LONG:
        case MPI_UNSIGNED_LONG_LONG:
            return DTE_UINT64;
            /* return DTE_UINT128; */
        case MPI_FLOAT:
            return DTE_FLOAT32;
        case MPI_DOUBLE:
            return DTE_FLOAT64;
        case MPI_LONG_DOUBLE:
            return DTE_FLOAT128;
        default:
            return DTE_ZERO;
    }
}

static inline dte_data_representation_t mpi_predefined_derived_2_hcoll(MPI_Datatype datatype)
{
    MPI_Aint size;

    switch (datatype) {
        case MPI_FLOAT_INT:
            return DTE_FLOAT_INT;
        case MPI_DOUBLE_INT:
            return DTE_DOUBLE_INT;
        case MPI_LONG_INT:
            return DTE_LONG_INT;
        case MPI_SHORT_INT:
            return DTE_SHORT_INT;
        case MPI_LONG_DOUBLE_INT:
            return DTE_LONG_DOUBLE_INT;
        case MPI_2INT:
            return DTE_2INT;
#ifdef HAVE_FORTRAN_BINDING
#if HCOLL_API >= HCOLL_VERSION(3,7)
        case MPI_2INTEGER:
            MPIR_Datatype_get_size_macro(datatype, size);
            switch (size) {
                case 4:
                    return DTE_2INT;
                case 8:
                    return DTE_2INT64;
                default:
                    return DTE_ZERO;
            }
        case MPI_2REAL:
            MPIR_Datatype_get_size_macro(datatype, size);
            switch (size) {
                case 4:
                    return DTE_2FLOAT32;
                case 8:
                    return DTE_2FLOAT64;
                default:
                    return DTE_ZERO;
            }
        case MPI_2DOUBLE_PRECISION:
            MPIR_Datatype_get_size_macro(datatype, size);
            switch (size) {
                case 4:
                    return DTE_2FLOAT32;
                case 8:
                    return DTE_2FLOAT64;
                default:
                    return DTE_ZERO;
            }
#endif
#endif
        default:
            break;
    }
    return DTE_ZERO;
}

// #if HCOLL_API => HCOLL_VERSION(3,6)
// static inline
// int hcoll_map_derived_type(MPI_Datatype *dtype, dte_data_representation *new_dte)
// {
//     int rc;
//     if (NULL == dtype) {
//         return MPI_SUCCESS;
//     }
//     rc = hcoll_create_mpi_type((void *)dtype, new_dte);
//     return rc == HCOLL_SUCCESS ? MPI_SUCCESS : MPI_ERROR;
// }
//
// static dte_data_representation_t find_derived_mapping(MPI_Datatype) {
//     dte_data_representation_t dte = DTE_ZERO;
//     mca_coll_hcoll_dtype_t *hcoll_dtype;
//     if (mca_coll_hcoll_component.derived_types_support_enabled) {
//     }
//
//     return dte;
// }
// #endif

static dte_data_representation_t
mpi_dtype_2_hcoll_dtype(MPI_Datatype datatype, int count, const int mode)
{
    MPIR_Datatype *dt_ptr;
    dte_data_representation_t dte_data_rep = DTE_ZERO;

    if (HANDLE_GET_KIND((datatype)) == HANDLE_KIND_BUILTIN) {
        /* Built-in type */
        dte_data_rep = datatype_2_dte_data_rep(datatype);
    }
#if HCOLL_API >= HCOLL_VERSION(3,6)
    else if (TRY_FIND_DERIVED == mode) {

        /* Check for predefined derived types */
        dte_data_rep = mpi_predefined_derived_2_hcoll(datatype);
        if (HCOL_DTE_IS_ZERO(dte_data_rep)) {
            /* Must be a non-predefined derived mapping, get it */
            MPIR_Datatype_get_ptr(datatype, dt_ptr);
            dte_data_rep = (dte_data_representation_t) dt_ptr->dev.hcoll_datatype;
        }
    }
#endif

    /* We always fall back, don't even think about forcing it! */
    /* XXX Fix me
     * if (HCOL_DTE_IS_ZERO(dte_data_rep) && TRY_FIND_DERIVED == mode
     * && !mca_coll_hcoll_component.hcoll_datatype_fallback) {
     * dte_data_rep = DTE_ZERO;
     * dte_data_rep.rep.in_line_rep.data_handle.in_line.in_line = 0;
     * dte_data_rep.rep.in_line_rep.data_handle.pointer_to_handle = (uint64_t) &datatype;
     * }
     */
    return dte_data_rep;
}

static hcoll_dte_op_t *mpi_op_2_dte_op(MPI_Op op)
{
    switch (op) {
        case MPI_MAX:
            return &hcoll_dte_op_max;
        case MPI_MIN:
            return &hcoll_dte_op_min;
        case MPI_SUM:
            return &hcoll_dte_op_sum;
        case MPI_PROD:
            return &hcoll_dte_op_prod;
        case MPI_LAND:
            return &hcoll_dte_op_land;
        case MPI_BAND:
            return &hcoll_dte_op_band;
        case MPI_LOR:
            return &hcoll_dte_op_lor;
        case MPI_BOR:
            return &hcoll_dte_op_bor;
        case MPI_LXOR:
            return &hcoll_dte_op_lxor;
        case MPI_BXOR:
            return &hcoll_dte_op_bxor;
        default:
            return &hcoll_dte_op_null;
    }
}

#endif /* HCOLL_DTYPES_H_INCLUDED */
