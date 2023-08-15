/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef MPIDIG_PERSIST_H_INCLUDED
#define MPIDIG_PERSIST_H_INCLUDED

int MPIDIG_mpi_send_init(const void *buffer, int count, MPI_Datatype datatype, int dest, int tag,
                         MPIR_Comm * comm, int attr, MPIR_Request ** request);
int MPIDIG_mpi_ssend_init(const void *buffer, int count, MPI_Datatype datatype, int dest, int tag,
                          MPIR_Comm * comm, int attr, MPIR_Request ** request);
int MPIDIG_mpi_bsend_init(const void *buffer, int count, MPI_Datatype datatype, int dest, int tag,
                          MPIR_Comm * comm, int attr, MPIR_Request ** request);
int MPIDIG_mpi_rsend_init(const void *buffer, int count, MPI_Datatype datatype, int dest, int tag,
                          MPIR_Comm * comm, int attr, MPIR_Request ** request);

#endif /* MPIDIG_PERSIST_H_INCLUDED */
