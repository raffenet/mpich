AC_PREREQ([2.60])
#
# (C) 2006 by Argonne National Laboratory.
#     See COPYRIGHT in top-level directory.
#

AC_INIT([PMI], [0.1])

AC_CONFIG_AUX_DIR(confdb)
AC_CONFIG_MACRO_DIR(confdb)
AM_INIT_AUTOMAKE([subdir-objects] [-Wall -Werror foreign 1.12.3])

dnl must come before LT_INIT, which AC_REQUIREs AC_PROG_CC
PAC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AR

LT_PREREQ([2.2.6])

# Bug in libtool adds -O2 and -g by default
PAC_PUSH_FLAG([CFLAGS])
LT_INIT()
PAC_POP_FLAG([CFLAGS])

if test "$enable_pmiport" != "no" ; then
   enable_pmiport=yes
fi
AC_CHECK_HEADERS(unistd.h string.h stdlib.h sys/socket.h strings.h assert.h arpa/inet.h)
dnl Use snprintf if possible when creating messages
AC_CHECK_FUNCS(snprintf)
if test "$ac_cv_func_snprintf" = "yes" ; then
    PAC_FUNC_NEEDS_DECL([#include <stdio.h>],snprintf)
fi

#
# PM's that need support for a port can set the environment variable
# NEED_PMIPORT in their setup_pm script.
if test "$NEED_PMIPORT" = "yes" -a "$enable_pmiport" != "yes" ; then
    AC_MSG_WARN([The process manager requires the pmiport capability.  Do not specify --disable-pmiport.])
    enable_pmiport=yes
fi
#
if test "$enable_pmiport" = "yes" ; then
    # Check for the necessary includes and functions
    missing_headers=no
    AC_CHECK_HEADERS([	\
	sys/types.h	\
	sys/param.h	\
	sys/socket.h	\
	netinet/in.h	\
	netinet/tcp.h	\
	sys/un.h	\
	netdb.h		\
	],,missing_headers=yes )
    AC_SEARCH_LIBS(socket,socket)
    AC_SEARCH_LIBS(gethostbyname,nsl)
    missing_functions=no
    AC_CHECK_FUNCS(socket setsockopt gethostbyname,,missing_functions=yes)
    
    if test "$missing_functions" = "no" ; then
        AC_DEFINE(USE_PMI_PORT,1,[Define if access to PMI information through a port rather than just an fd is allowed])
    else
        AC_MSG_ERROR([Cannot build simple PMI with support for an IP port because of missing functions])
    fi
fi

# Check for socklen_t .  If undefined, define it as int
# (note the conditional inclusion of sys/socket.h)
AC_CACHE_CHECK([whether socklen_t is defined (in sys/socket.h if present)],
pac_cv_have_socklen_t,[
AC_TRY_COMPILE([
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
]
typedef struct { double a; int b; } socklen_t;,[socklen_t a;a.a=1.0;],pac_cv_have_socklen_t=no,pac_cv_have_socklen_t=yes)])
if test "$pac_cv_have_socklen_t" = no ; then
    AC_DEFINE(socklen_t,int,[Define if socklen_t is not defined])
fi
# Check for h_addr or h_addr_list
AC_CACHE_CHECK([whether struct hostent contains h_addr_list],
pac_cv_have_haddr_list,[
AC_TRY_COMPILE([
#include <netdb.h>],[struct hostent hp;hp.h_addr_list[0]=0;],
pac_cv_have_haddr_list=yes,pac_cv_have_haddr_list=no)])
if test "$pac_cv_have_haddr_list" = "yes" ; then
    AC_DEFINE(HAVE_H_ADDR_LIST,1,[Define if struct hostent contains h_addr_list])
fi
PAC_C_GNU_ATTRIBUTE

dnl Final output
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
