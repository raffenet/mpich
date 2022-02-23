##
## Copyright (C) by Argonne National Laboratory
##     See COPYRIGHT in top-level directory
##

if BUILD_PMI_SLURM

noinst_HEADERS += src/pmi/slurm/pmi.h

AM_CPPFLAGS += -I$(top_srcdir)/src/pmi/slurm

endif BUILD_PMI_SLURM

if BUILD_PMI2_SLURM

noinst_HEADERS += src/pmi/slurm/pmi2.h

AM_CPPFLAGS += -I$(top_srcdir)/src/pmi/slurm

endif BUILD_PMI2_SLURM
