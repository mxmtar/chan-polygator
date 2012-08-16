#
# check sqlite3 version
#
# PG_SQLITE3_VERSION_PREREQ([version])

AC_DEFUN([PG_SQLITE3_VERSION_PREREQ],
[

AC_MSG_CHECKING([for sqlite3 version >= $1])

SQLITE3_VERSION=""

if test -f "$lt_sysroot/usr/include/sqlite3.h" ; then
	REQ_SQLITE_VERSION=$1
	[REQ_SQLITE_VERSION_TRIM=`LANG=C printf "$REQ_SQLITE_VERSION" | ${GREP} -e '^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}' | ${SED} -e '{s:\(^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}\)\(.*\):\1:}'`]
	if test -z "$REQ_SQLITE_VERSION_TRIM"; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(bad version string \"$1\")
		exit 1
	fi
	REQ_SQLITE_VERSION_PARTS=`LANG=C printf "$REQ_SQLITE_VERSION_TRIM" | ${SED} -e 'y:\.: :'`
	REQ_SQLITE_VERSION_BIN=`
	for PARTS in $REQ_SQLITE_VERSION_PARTS ; do
	printf "%03d" $PARTS
	done`

	[TST_SQLITE_VERSION=`cat $lt_sysroot/usr/include/sqlite3.h | ${GREP} -e '[[:space:]]*#define[[:space:]]*SQLITE_VERSION[[:space:]]*\"\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}\".*' | ${SED} -e '{s:[[:space:]]*#define[[:space:]]*SQLITE_VERSION[[:space:]]*\"\(\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}\)\".*:\1:}'`]
	if test "$TST_SQLITE_VERSION" = ""; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT([Cannot find SQLITE_VERSION in sqlite3.h header to retrieve sqlite3 version!])
		exit 1
	fi
	SQLITE3_VERSION=TST_SQLITE_VERSION

	[TST_SQLITE_VERSION_TRIM=`LANG=C printf "$TST_SQLITE_VERSION" | ${GREP} -e '^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}' | ${SED} -e '{s:\(^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}\)\(.*\):\1:}'`]
	if test -z "$TST_SQLITE_VERSION_TRIM"; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(bad version string \"$TST_SQLITE_VERSION_TRIM\")
		exit 1
	fi
	TST_SQLITE_VERSION_PARTS=`LANG=C printf "$TST_SQLITE_VERSION_TRIM" | ${SED} -e 'y:\.: :'`
	TST_SQLITE_VERSION_BIN=`
	for PARTS in $TST_SQLITE_VERSION_PARTS ; do
	printf "%03d" $PARTS
	done`

	if test $TST_SQLITE_VERSION_BIN -ge $REQ_SQLITE_VERSION_BIN ; then
		AC_SUBST(SQLITE3_VERSION)
		AC_MSG_RESULT($TST_SQLITE_VERSION)
	else
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(sqlite3 version \"$TST_SQLITE_VERSION\" is early then required \"$REQ_SQLITE_VERSION\")
		exit 1
	fi
else
	AC_MSG_RESULT(fail)
	AC_MSG_RESULT(sqlite3.h not found")
	exit 1
fi

])
