#
# check astersik version
#
# PG_ASTERISK_VERSION_PREREQ([version])

AC_DEFUN([PG_ASTERISK_VERSION_PREREQ],
[

AC_MSG_CHECKING([for asterisk version >= $1])

ASTERISK_VERSION=""

test "$prefix" = NONE && prefix=/usr/local

if test -f "$lt_sysroot$prefix/include/asterisk/version.h" ; then
	REQ_ASTERISK_VERSION=$1
	[REQ_ASTERISK_VERSION_TRIM=`LANG=C printf "$REQ_ASTERISK_VERSION" | ${GREP} -e '^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}' | ${SED} -e '{s:\(^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}\)\(.*\):\1:}'`]
	if test -z "$REQ_ASTERISK_VERSION_TRIM"; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(bad version string \"$1\")
		exit 1
	fi
	REQ_ASTERISK_VERSION_PARTS=`LANG=C printf "$REQ_ASTERISK_VERSION_TRIM" | ${SED} -e 'y:\.: :'`
	REQ_ASTERISK_VERSION_BIN=`
	for PARTS in $REQ_ASTERISK_VERSION_PARTS ; do
	printf "%03d" $PARTS
	done`

	[TST_ASTERISK_VERSION=`cat $lt_sysroot$prefix/include/asterisk/version.h | ${GREP} -e '[[:space:]]*#define[[:space:]]*ASTERISK_VERSION[[:space:]]*\"\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}[^[:space:]]*\".*' | ${SED} -e '{s:[[:space:]]*#define[[:space:]]*ASTERISK_VERSION[[:space:]]*\"\(\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}[^[:space:]]*\)\".*:\1:}'`]
	if test "$TST_ASTERISK_VERSION" = ""; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT([Cannot find ASTERISK_VERSION in asterisk/version.h header to retrieve asterisk version!])
		exit 1
	fi
	ASTERISK_VERSION=TST_ASTERISK_VERSION

	[TST_ASTERISK_VERSION_TRIM=`LANG=C printf "$TST_ASTERISK_VERSION" | ${GREP} -e '^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}' | ${SED} -e '{s:\(^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}\)\(.*\):\1:}'`]
	if test -z "$TST_ASTERISK_VERSION_TRIM"; then
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(bad version string \"$TST_ASTERISK_VERSION_TRIM\")
		exit 1
	fi
	TST_ASTERISK_VERSION_PARTS=`LANG=C printf "$TST_ASTERISK_VERSION_TRIM" | ${SED} -e 'y:\.: :'`
	TST_ASTERISK_VERSION_BIN=`
	for PARTS in $TST_ASTERISK_VERSION_PARTS ; do
	printf "%03d" $PARTS
	done`

	if test $TST_ASTERISK_VERSION_BIN -ge $REQ_ASTERISK_VERSION_BIN ; then
		AC_SUBST(ASTERISK_VERSION)
		AC_MSG_RESULT($TST_ASTERISK_VERSION)
	else
		AC_MSG_RESULT(fail)
		AC_MSG_RESULT(asterisk version \"$TST_ASTERISK_VERSION\" is early then required \"$REQ_ASTERISK_VERSION\")
		exit 1
	fi
else
	AC_MSG_RESULT(fail)
	AC_MSG_RESULT(asterisk/version.h not found")
	exit 1
fi

])
