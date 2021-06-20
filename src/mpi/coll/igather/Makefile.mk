##
## Copyright (C) by Argonne National Laboratory
##     See COPYRIGHT in top-level directory
##

# mpi_sources includes only the routines that are MPI function entry points
# The code for the MPI operations (e.g., MPI_SUM) is not included in
# mpi_sources

mpi_core_sources +=                          \
    src/mpi/coll/igather/igather_intra_sched_binomial.c  \
    src/mpi/coll/igather/igather_inter_sched_short.c \
    src/mpi/coll/igather/igather_inter_sched_long.c \
    src/mpi/coll/igather/igather_gentran_algos.c
