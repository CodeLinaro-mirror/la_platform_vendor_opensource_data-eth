#                                               -*- Autoconf -*-

# configure.ac -- Autoconf script for thermal-engine
#

# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([thermal-eth-netlink],
	1.0.0)
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG

# Checks for libraries.
#PKG_CHECK_MODULES([THERMAL], [libthermalclient])
#AC_SUBST(THERMAL_CFLAGS)
#AC_SUBST(THERMAL_LIBS)

AC_ARG_WITH([sanitized-headers],
	    [AS_HELP_STRING([--with-sanitized-headers=DIR],[location of the sanitized Linux kernel headers])],
	    [CFLAGS="${CFLAGS} -I$withval"])


has_libnl_ver=0
PKG_CHECK_MODULES([LIBNL], [libnl-3.0 >= 3.0 libnl-genl-3.0 >= 3.0], [has_libnl_ver=3], [
		   PKG_CHECK_MODULES([LIBNL], [libnl-2.0 >= 2.0], [has_libnl_ver=2], [
				      PKG_CHECK_MODULES([LIBNL], [libnl-1], [has_libnl_ver=1], [has_libnl_ver=0])])])

if (test "$has_libnl_ver" -eq 0); then
	AC_MSG_ERROR(libnl and libnl-genl are required but were not found)
fi

if (test "$has_libnl_ver" -gt 1); then
	AC_DEFINE([HAVE_LIBNL20], [1], [Define if you have libnl-2.0 or higher])
fi

if (test "$has_libnl_ver" -gt 2); then
	AC_DEFINE([HAVE_LIBNL30], [1], [Define if you have libnl-3.0 or higher])
fi

AC_SUBST([LIBNL_CFLAGS])
AC_SUBST([LIBNL_LIBS])

#AC_ARG_WITH([glib],
#            AC_HELP_STRING([--with-glib],
#            [enable glib, building HLOS systems which use glib]))

#if (test "x${with_glib}" = "xyes"); then
#    AC_DEFINE(ENABLE_USEGLIB, 1, [Define if HLOS systems uses glib])
#    PKG_CHECK_MODULES(GTHREAD, gthread-2.0 >= 2.16, dummy=yes,
#            AC_MSG_ERROR(GThread >= 2.16 is required))
#    PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.16, dummy=yes,
#            AC_MSG_ERROR(GLib >= 2.16 is required))
#    GLIB_CFLAGS="$GLIB_CFLAGS $GTHREAD_CFLAGS"
#    GLIB_LIBS="$GLIB_LIBS $GTHREAD_LIBS"

#    AC_SUBST(GLIB_CFLAGS)
#    AC_SUBST(GLIB_LIBS)
#fi

#AM_CONDITIONAL(USE_GLIB, test "x${with_glib}" = "xyes")

AC_SUBST([CFLAGS])
AC_SUBST([CC])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
