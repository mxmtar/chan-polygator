#
# Process this file with autoconf to produce a configure script.
#
AC_PREREQ([2.60])

# set project attribute
AC_INIT([chan-polygator], [1.6.0])

# init automake
AM_INIT_AUTOMAKE([-Wall -Werror])

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

# generating version string
if test -f version ; then
	PACKAGE_VERSION=`cat version`
fi

# construct package string
PACKAGE_STRING=${PACKAGE_NAME}-${PACKAGE_VERSION}

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR([chan_polygator.c])

AC_COPYRIGHT("Polygator")
AC_REVISION(1.6.0)

# prepare autoconfig header data
AC_CONFIG_HEADER([autoconfig.h])

# manage platform values
BUILD_PLATFORM=${build}
BUILD_CPU=${build_cpu}
BUILD_VENDOR=${build_vendor}
BUILD_OS=${build_os}

AC_SUBST(BUILD_PLATFORM)
AC_SUBST(BUILD_CPU)
AC_SUBST(BUILD_VENDOR)
AC_SUBST(BUILD_OS)

HOST_PLATFORM=${host}
HOST_CPU=${host_cpu}
HOST_VENDOR=${host_vendor}
HOST_OS=${host_os}

AC_SUBST(HOST_PLATFORM)
AC_SUBST(HOST_CPU)
AC_SUBST(HOST_VENDOR)
AC_SUBST(HOST_OS)

WORK_DIR=`pwd`
AC_SUBST(WORK_DIR)

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CXX
AC_PROG_GREP
AC_PROG_INSTALL
AC_PROG_SED
AC_PROG_LIBTOOL

m4_ifdef([AM_PROG_CC_C_O], [AM_PROG_CC_C_O])

CPPFLAGS="${CPPFLAGS} -I$lt_sysroot$prefix/include"
if test -d "$lt_sysroot$prefix/lib64" ; then
	LIBS="${LIBS} -L$lt_sysroot$prefix/lib64"
	LIBDIR=$lt_sysroot$prefix/lib64
	AC_SUBST(LIBDIR)
else
	LIBS="${LIBS} -L$lt_sysroot$prefix/lib"
	LIBDIR=$lt_sysroot$prefix/lib
	AC_SUBST(LIBDIR)
fi

# Get asterisk source path
AC_ARG_WITH([asterisk], AC_HELP_STRING([--with-asterisk=PATH],[use asterisk files in PATH]), [ASTERISK_SRC_PATH=${withval}; echo "used asterisk files in ${withval}"])

# Checks for packages.
# Checks for required polygator-linux version.
PG_POLYGATOR_LINUX_VERSION_PREREQ([1.1.1])

# Checks for required libvinetic version.
PG_LIBVINETIC_VERSION_PREREQ([1.1.1])

# Checks for required asterisk version.
PG_ASTERISK_VERSION_PREREQ([1.6.0], [${ASTERISK_SRC_PATH}])
AC_DEFINE_UNQUOTED([ASTERISK_VERSION_NUMBER], [${ASTERISK_VERSION_NUMBER}], [Define recognized asterisk version number.])

# Checks for required sqlite3 version
PG_SQLITE3_VERSION_PREREQ([3.0.0])

# Checks for header files.
AC_CHECK_HEADER([polygator/version.h], [], [echo "Polygator kernel module not installed" ; exit 1])
AC_CHECK_HEADER([polygator/polygator-base.h], [], [echo "Polygator kernel module not installed" ; exit 1])
AC_CHECK_HEADER([polygator/vinetic-def.h], [], [echo "Polygator kernel module not installed" ; exit 1])
AC_CHECK_HEADER([polygator/vinetic-ioctl.h], [], [echo "Polygator kernel module not installed" ; exit 1])
AC_CHECK_HEADER([libvinetic/libvinetic.h], [], [echo "libvinetic not installed - please install libvinetic or libvinetic development package" ; exit 1])
AC_CHECK_HEADER([asterisk.h], [], [echo "asterisk not installed - please install asterisk or asterisk development package" ; exit 1])
AC_CHECK_HEADER([sqlite3.h], [], [echo "sqlite3 not installed - please install sqlite3 or sqlite3 development package" ; exit 1])

AC_MSG_CHECKING(asterisk/select.h presence)
AC_LANG_C
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM(
[
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <asterisk.h>
#include <asterisk/select.h>
],
[[;]])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_ASTERISK_SELECT_H], [1], [Define to 1 if you have the <asterisk/select.h> header file.]),
AC_MSG_RESULT(no)
)

TMPCPPFLAGS=${CPPFLAGS}
CPPFLAGS="${CPPFLAGS} -Wall -Werror"
AC_MSG_CHECKING(ast_queue_hangup_with_cause)
AC_LANG_C
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM(
[
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <asterisk.h>
#include <asterisk/channel.h>
],
[[ast_queue_hangup_with_cause(NULL, 0);]])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_ASTERISK_QUEUE_HANGUP_WITH_CAUSE], [1], [Define to 1 if you have the ast_queue_hangup_with_cause() function.]),
AC_MSG_RESULT(no)
)
CPPFLAGS=${TMPCPPFLAGS}

TMPCPPFLAGS=${CPPFLAGS}
CPPFLAGS="${CPPFLAGS} -Wall -Werror"
AC_MSG_CHECKING(ast_strptime)
AC_LANG_C
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM(
[
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <asterisk.h>
#include <asterisk/localtime.h>
],
[[ast_strptime(NULL, NULL, NULL);]])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_ASTERISK_STRPTIME], [1], [Define to 1 if you have the ast_strptime() function.]),
AC_MSG_RESULT(no)
)
CPPFLAGS=${TMPCPPFLAGS}

# Get asterisk directories
PG_ASTERISK_GET_DIRECTORY([astetcdir])
PG_ASTERISK_GET_DIRECTORY([astmoddir])
PG_ASTERISK_GET_DIRECTORY([astdatadir])

AC_DEFINE_UNQUOTED([ASTERISK_DATA_DEFAULT_PATH], ["${astdatadir}"], [Define default path to asterisk data directory.])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT

AC_MSG_NOTICE(Package "$PACKAGE_STRING")
AC_MSG_NOTICE( Build on:)
AC_MSG_NOTICE(  OS: $build_os)
AC_MSG_NOTICE(  Vendor: $build_vendor)
AC_MSG_NOTICE(  CPU: $build_cpu)
AC_MSG_NOTICE( Build for: OS:)
AC_MSG_NOTICE(  OS: $host_os)
AC_MSG_NOTICE(  Vendor: $host_vendor)
AC_MSG_NOTICE(  CPU: $host_cpu)
if test "${cross_compiling}" = "yes"; then
	AC_MSG_NOTICE(Cross compilation needed)
fi
