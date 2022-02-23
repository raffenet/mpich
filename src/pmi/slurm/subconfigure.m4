[#] start of __file__

AC_DEFUN([PAC_SUBCFG_PREREQ_]PAC_SUBCFG_AUTO_SUFFIX,[
])

AC_DEFUN([PAC_SUBCFG_BODY_]PAC_SUBCFG_AUTO_SUFFIX,[

AM_CONDITIONAL([BUILD_PMI_SLURM],[test "x$pmi_name" = "xslurm"])
AM_COND_IF([BUILD_PMI_SLURM],[

# sets CPPFLAGS and LDFLAGS
PAC_SET_HEADER_LIB_PATH([slurm])

AC_CHECK_HEADER([slurm/pmi.h], [], [AC_MSG_ERROR([could not find slurm/pmi.h.  Configure aborted])])
AC_CHECK_LIB([pmi], [PMI_Init],
             [PAC_PREPEND_FLAG([-lpmi],[LIBS])
              PAC_PREPEND_FLAG([-lpmi], [WRAPPER_LIBS])],
             [AC_MSG_ERROR([could not find the Slurm libpmi.  Configure aborted])])

])dnl end COND_IF

AM_CONDITIONAL([BUILD_PMI2_SLURM],[test "x$pmi_name" = "xslurm/pmi2"])
AM_COND_IF([BUILD_PMI2_SLURM],[

# sets CPPFLAGS and LDFLAGS
PAC_SET_HEADER_LIB_PATH([slurm])

AC_CHECK_HEADER([slurm/pmi2.h], [], [AC_MSG_ERROR([could not find slurm/pmi2.h.  Configure aborted])])
AC_CHECK_LIB([pmi2], [PMI2_Init],
             [PAC_PREPEND_FLAG([-lpmi2],[LIBS])
              PAC_PREPEND_FLAG([-lpmi2], [WRAPPER_LIBS])],
             [AC_MSG_ERROR([could not find the Slurm libpmi2.  Configure aborted])])

AC_DEFINE(USE_PMI2_API, 1, [Define if PMI2 API must be used])

])dnl end COND_IF

])dnl end BODY macro

[#] end of __file__
