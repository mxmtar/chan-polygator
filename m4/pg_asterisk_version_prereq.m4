#
# check astersik version
#
# PG_ASTERISK_VERSION_PREREQ([version])

AC_DEFUN([PG_ASTERISK_VERSION_PREREQ],
[

AC_MSG_CHECKING([for asterisk version >= $1])

ASTERISK_VERSION=""

test "$prefix" = NONE && prefix=/usr/local

REQ_ASTERISK_VERSION=$1
[REQ_ASTERISK_VERSION_TRIM=`LANG=C printf "$REQ_ASTERISK_VERSION" | ${GREP} -e '^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}' | ${SED} -e '{s:\(^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}\)\(.*\):\1:}'`]
if test -z "$REQ_ASTERISK_VERSION_TRIM"; then
	AC_MSG_RESULT(fail)
	AC_MSG_RESULT([bad version string \"$1\"])
	exit 1
fi
REQ_ASTERISK_VERSION_PARTS=`LANG=C printf "$REQ_ASTERISK_VERSION_TRIM" | ${SED} -e 'y:\.: :'`
COUNT="0"
REQ_ASTERISK_VERSION_BIN=`
for PARTS in $REQ_ASTERISK_VERSION_PARTS ; do
	if ((COUNT)) ; then
		printf "%02d" $PARTS
	else
		printf "%d" $PARTS
	fi
	let COUNT++;
done`

if test -d "$lt_sysroot$2" ; then
	[TST_ASTERISK_VERSION=`cat $lt_sysroot$2/.version | ${GREP} -e '[[:space:]]*\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}[^[:space:]]*.*' | ${SED} -e '{s:[[:space:]]*\(\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}[^[:space:]]*\).*:\1:}'`]
elif test -f "$lt_sysroot$prefix/sbin/asterisk" ; then
	[TST_ASTERISK_VERSION=`LD_LIBRARY_PATH=$LIBDIR $lt_sysroot$prefix/sbin/asterisk -V | ${GREP} -e '[[:space:]]*Asterisk[[:space:]]*\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}[^[:space:]]*.*' | ${SED} -e '{s:[[:space:]]*Asterisk[[:space:]]*\(\([0-9]\{1,2\}\.\)\{2,3\}[0-9]\{1,2\}[^[:space:]]*\).*:\1:}'`]
else
	AC_MSG_RESULT(fail)
	AC_MSG_RESULT([asterisk executable not found])
	exit 1
fi

if test "$TST_ASTERISK_VERSION" = ""; then
	AC_MSG_RESULT(fail)
	if test -d "$lt_sysroot$2" ; then
		AC_MSG_RESULT([can't get asterisk version from $lt_sysroot$2/.version])
	else
		AC_MSG_RESULT([asterisk executable don't return version by -V option])
	fi
	exit 1
fi

ASTERISK_VERSION=TST_ASTERISK_VERSION

[TST_ASTERISK_VERSION_TRIM=`LANG=C printf "$TST_ASTERISK_VERSION" | ${GREP} -e '^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}' | ${SED} -e '{s:\(^[0-9]\{1,2\}\.[0-9]\{1,2\}\.[0-9]\{1,2\}\)\(.*\):\1:}'`]
if test -z "$TST_ASTERISK_VERSION_TRIM"; then
	AC_MSG_RESULT(fail)
	AC_MSG_RESULT([bad version string \"$TST_ASTERISK_VERSION_TRIM\"])
	exit 1
fi
TST_ASTERISK_VERSION_PARTS=`LANG=C printf "$TST_ASTERISK_VERSION_TRIM" | ${SED} -e 'y:\.: :'`
COUNT="0"
TST_ASTERISK_VERSION_BIN=`
for PARTS in $TST_ASTERISK_VERSION_PARTS ; do
	if ((COUNT)) ; then
		printf "%02d" $PARTS
	else
		printf "%d" $PARTS
	fi
	let COUNT++;
done`

if test $TST_ASTERISK_VERSION_BIN -ge $REQ_ASTERISK_VERSION_BIN ; then
	AC_SUBST(ASTERISK_VERSION)
	ASTERISK_VERSION_NUMBER=${TST_ASTERISK_VERSION_BIN}
	AC_SUBST(ASTERISK_VERSION_NUMBER)
	AC_MSG_RESULT([$TST_ASTERISK_VERSION])
else
	AC_MSG_RESULT(fail)
	AC_MSG_RESULT([asterisk version \"$TST_ASTERISK_VERSION\" is early then required \"$REQ_ASTERISK_VERSION\"])
	exit 1
fi

])
