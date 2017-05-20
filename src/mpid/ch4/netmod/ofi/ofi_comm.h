/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2006 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 *
 *  Portions of this code were written by Intel Corporation.
 *  Copyright (C) 2011-2016 Intel Corporation.  Intel provides this material
 *  to Argonne National Laboratory subject to Software Grant and Corporate
 *  Contributor License Agreement dated February 8, 2012.
 */
#ifndef OFI_COMM_H_INCLUDED
#define OFI_COMM_H_INCLUDED

#include "ofi_impl.h"
#include "mpl_utlist.h"
#include "mpidu_shm.h"

#undef FUNCNAME
#define FUNCNAME MPIDI_NM_mpi_comm_create_hook
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
static inline int MPIDI_NM_mpi_comm_create_hook(MPIR_Comm * comm)
{
    int mpi_errno = MPI_SUCCESS;
    static int comm_world_initialized = 0;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_NM_MPI_COMM_CREATE_HOOK);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_NM_MPI_COMM_CREATE_HOOK);

    MPIDI_OFI_map_create(&MPIDI_OFI_COMM(comm).huge_send_counters);
    MPIDI_OFI_map_create(&MPIDI_OFI_COMM(comm).huge_recv_counters);
    MPIDI_OFI_index_allocator_create(&MPIDI_OFI_COMM(comm).win_id_allocator, 0);
    MPIDI_OFI_index_allocator_create(&MPIDI_OFI_COMM(comm).rma_id_allocator, 1);

    mpi_errno = MPIDI_CH4U_init_comm(comm);

    /* no connection for non-dynamic or non-root-rank of intercomm */
    MPIDI_OFI_COMM(comm).conn_id = -1;

    /* Do not handle intercomms */
    if (comm->comm_kind == MPIR_COMM_KIND__INTERCOMM)
        goto fn_exit;

    /* if this is MPI_COMM_WORLD, finish bc exchange */
    if (comm == MPIR_Process.comm_world && !comm_world_initialized) {
        struct MPIDI_OFI_rank_addr {
            char addrname[MPIDI_Global.addrnamelen];
        };
        MPIDU_shm_seg_t memory;
        MPIDU_shm_barrier_t *barrier;
        struct MPIDI_OFI_rank_addr *table, *local_table;
        int i, local_rank, local_rank_0 = -1, num_local = 0;
        fi_addr_t *mapped_table;
        int rank = MPIR_Comm_rank(comm);
        int size = MPIR_Comm_size(comm);
        int num_nodes = MPIDI_CH4_Global.max_node_id + 1;
        int procs_per_node[num_nodes];

        if (size == num_nodes)
            goto out;

        /* locality information for shm segment */
        for (i = 0; i < size; i++) {
            if (MPIDI_CH4_rank_is_local(i, comm)) {
                if (i == rank)
                    local_rank = num_local;

                if (local_rank_0 == -1)
                    local_rank_0 = i;

                num_local++;
            }
        }

        /* start at -1 because we want to exclude the node root */
        for (i = 0; i < num_nodes; i++)
            procs_per_node[i] = -1;
        for (i = 0; i < size; i++)
            procs_per_node[MPIDI_CH4_Global.node_map[0][i]] += 1;

        /* determine if we have uniform ppn */
        int uniform = 1;
        for (i = 0; i < MPIDI_CH4_Global.max_node_id; i++) {
            if (procs_per_node[i] != procs_per_node[i + 1]) {
                uniform = 0;
                break;
            }
        }
        int offset = 0;
        if (uniform) {
            offset = MPIDI_CH4_Global.node_map[0][rank] * (num_local - 1);
        } else {
            for (i = 0; i < MPIDI_CH4_Global.node_map[0][rank]; i++)
                offset += procs_per_node[i];
        }

        MPIDU_shm_seg_alloc((size - num_nodes) * sizeof(struct MPIDI_OFI_rank_addr), (void **)&table);
        MPIDU_shm_seg_commit(&memory, &barrier, num_local, local_rank, local_rank_0, rank);

        /* copy info */
        if (rank != local_rank_0) {
            local_table = &table[offset];
            memcpy(local_table[local_rank - 1].addrname, MPIDI_Global.addrname, MPIDI_Global.addrnamelen);
        }

        MPIDU_shm_barrier(barrier, num_local);
        if (rank == local_rank_0) {
            MPIR_Errflag_t errflag = MPIR_ERR_NONE;
            MPIR_Comm *allgather_comm = comm->node_roots_comm ? comm->node_roots_comm : comm;
            if (uniform) {
                MPIR_Allgather_impl(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, table, (num_local - 1) * sizeof(struct MPIDI_OFI_rank_addr),
                                    MPI_BYTE, allgather_comm, &errflag);
            } else {
                /* convert procs_per_node to recvcounts */
                for (i = 0; i < num_nodes; i++)
                    procs_per_node[i] *= MPIDI_Global.addrnamelen;
                int displs[num_nodes];
                for (i = 0; i < num_nodes; i++)
                    displs[i] = 0;
                for (i = 1; i < num_nodes; i++)
                    displs[i] = displs[i - 1] + procs_per_node[i - 1];
                MPIR_Allgatherv_impl(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, table, procs_per_node, displs,
                                     MPI_BYTE, allgather_comm, &errflag);
            }
        }
        MPIDU_shm_barrier(barrier, num_local);

        mapped_table = MPL_malloc((size - num_nodes) * sizeof(fi_addr_t));
        double start = MPI_Wtime();
        fi_av_insert(MPIDI_Global.av, table, size - num_nodes, mapped_table, 0ULL, NULL);
        int curr;
        if (MPII_Comm_is_node_consecutive(comm)) {
            curr = 0;
            for (i = 0; i < size; i++) {
                if (MPIDI_OFI_AV(&MPIDIU_get_av(0, i)).dest != FI_ADDR_UNSPEC)
                    continue;
                else {
                    MPIDI_OFI_AV(&MPIDIU_get_av(0, i)).dest = mapped_table[curr++];
                }
            }
        } else {
            int ranks[size];
            int j;
            curr = 0;
            for (i = 0; i < num_nodes; i++)
                for (j = 0; j < size; j++)
                    if (MPIDI_CH4_Global.node_map[0][j] == i)
                        ranks[curr++] = j;
            for (i = 0; i < size; i++) {
                MPIDI_OFI_AV(&MPIDIU_get_av(0, ranks[i])).dest = mapped_table[i];
            }
        }

        MPIDU_shm_barrier(barrier, num_local);
        MPIDU_shm_seg_destroy(&memory, num_local);
        MPL_free(mapped_table);
        PMI_Barrier();
        /* now everyone can communicate */
    out:
        comm_world_initialized = 1;
    }

    MPIR_Assert(comm->coll_fns != NULL);
  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_NM_MPI_COMM_CREATE_HOOK);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_NM_mpi_comm_free_hook
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
static inline int MPIDI_NM_mpi_comm_free_hook(MPIR_Comm * comm)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_NM_MPI_COMM_FREE_HOOK);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_NM_MPI_COMM_FREE_HOOK);

    mpi_errno = MPIDI_CH4U_destroy_comm(comm);
    MPIDI_OFI_map_destroy(MPIDI_OFI_COMM(comm).huge_send_counters);
    MPIDI_OFI_map_destroy(MPIDI_OFI_COMM(comm).huge_recv_counters);
    MPIDI_OFI_index_allocator_destroy(MPIDI_OFI_COMM(comm).win_id_allocator);
    MPIDI_OFI_index_allocator_destroy(MPIDI_OFI_COMM(comm).rma_id_allocator);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_NM_MPI_COMM_FREE_HOOK);
    return mpi_errno;
}


#endif /* OFI_COMM_H_INCLUDED */
