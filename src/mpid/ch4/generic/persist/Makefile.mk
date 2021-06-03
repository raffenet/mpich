##
## Copyright (C) by Argonne National Laboratory
##     See COPYRIGHT in top-level directory
##

AM_CPPFLAGS += -I$(top_srcdir)/src/mpid/ch4/generic/persist

noinst_HEADERS += src/mpid/ch4/generic/persist/mpidig_persist.h

mpi_core_sources += src/mpid/ch4/generic/persist/mpidig_persist.c
