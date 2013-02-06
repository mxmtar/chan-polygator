#
# check libvinetic version
#
# PG_LIBVINETIC_VERSION_PREREQ([version])

AC_DEFUN([PG_LIBVINETIC_VERSION_PREREQ],
[

AC_MSG_CHECKING([for libvinetic version >= $1])

LIBVINETIC_VERSION=""

if test -f "$lt_sysroot/usr/include/libvinetic/version.h" ; then
	REQ_LIBVINETIC_VERSION=$1
	[REQ_LIBVINETIC_VERSION_TRIM=`LANG=C printf "$REQ_LIBVINETIC_VERSION" | ${GREP} -e '^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}' | ${SED} -e '{s:\(^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}\)\(.*\):\1:}'`]
	if test -z "$REQ_LIBVINETIC_VERSION_TRIM"; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(bad version string \"$1\")
		exit 1
	fi
	REQ_LIBVINETIC_VERSION_PARTS=`LANG=C printf "$REQ_LIBVINETIC_VERSION_TRIM" | ${SED} -e 'y:\.: :'`
	COUNT="0"
	REQ_LIBVINETIC_VERSION_BIN=`
	for PARTS in $REQ_LIBVINETIC_VERSION_PARTS ; do
		if ((COUNT)) ; then
			printf "%02d" $PARTS
		else
			printf "%d" $PARTS
		fi
		let COUNT++;
	done`

	[TST_LIBVINETIC_VERSION=`cat $lt_sysroot/usr/include/libvinetic/version.h | ${GREP} -e '[[:space:]]*#define[[:space:]]*libvinetic_VERSION[[:space:]]*\"\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}\".*' | ${SED} -e '{s:[[:space:]]*#define[[:space:]]*libvinetic_VERSION[[:space:]]*\"\(\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}\)\".*:\1:}'`]
	if test "$TST_LIBVINETIC_VERSION" = ""; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT([Cannot find libvinetic_VERSION in libvinetic/version.h header to retrieve libvinetic version!])
		exit 1
	fi
	[TST_LIBVINETIC_VERSION_TRIM=`LANG=C printf "$TST_LIBVINETIC_VERSION" | ${GREP} -e '^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}' | ${SED} -e '{s:\(^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}\)\(.*\):\1:}'`]
	if test -z "$TST_LIBVINETIC_VERSION_TRIM"; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(bad version string \"$TST_LIBVINETIC_VERSION_TRIM\")
		exit 1
	fi
	TST_LIBVINETIC_VERSION_PARTS=`LANG=C printf "$TST_LIBVINETIC_VERSION_TRIM" | ${SED} -e 'y:\.: :'`
	COUNT="0"
	TST_LIBVINETIC_VERSION_BIN=`
	for PARTS in $TST_LIBVINETIC_VERSION_PARTS ; do
		if ((COUNT)) ; then
			printf "%02d" $PARTS
		else
			printf "%d" $PARTS
		fi
		let COUNT++;
	done`

	LIBVINETIC_VERSION=$TST_LIBVINETIC_VERSION

	if test $TST_LIBVINETIC_VERSION_BIN -ge $REQ_LIBVINETIC_VERSION_BIN ; then
		AC_SUBST(LIBVINETIC_VERSION)
		AC_MSG_RESULT($TST_LIBVINETIC_VERSION)
	else
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(libvinetic version \"$TST_LIBVINETIC_VERSION\" is early then required \"$REQ_LIBVINETIC_VERSION\")
		exit 1
	fi
else
	AC_MSG_RESULT(fail)
	AC_MSG_RESULT(libvinetic/version.h not found")
	exit 1
fi

])
