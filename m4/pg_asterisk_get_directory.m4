#
# get astersik directories
#
# PG_ASTERISK_GET_DIRECTORY([directory])

AC_DEFUN([PG_ASTERISK_GET_DIRECTORY],
[
AC_MSG_CHECKING([for asterisk directory \"$1\"])

test "$prefix" = NONE && prefix=/usr/local

astprefix=$prefix

if test "$prefix" = /usr ; then
	astsysconfdir=/etc
	astlocalstatedir=/var
else
	astsysconfdir=$astprefix/etc
	astlocalstatedir=$astprefix/var
fi

if test "$1" = "astetcdir" ; then
	if test -d "$lt_sysroot$astsysconfdir/asterisk" ; then
		$1_res=$astsysconfdir/asterisk
	else 
		$1_res=unknown
	fi
elif test "$1" = "astdatadir" ; then
	if test -d "$lt_sysroot$astlocalstatedir/lib/asterisk" ; then
		$1_res=$astlocalstatedir/lib/asterisk
	else
		$1_res=unknown
	fi
elif test "$1" = "astmoddir" ; then
	if test -d "$lt_sysroot$astprefix/lib64/asterisk/modules" ; then
		$1_res=$astprefix/lib64/asterisk/modules
	elif test -d "$lt_sysroot$astprefix/lib/asterisk/modules" ; then
		$1_res=$astprefix/lib/asterisk/modules
	else
		$1_res=unknown
	fi
else
	$1_res=unknown
fi

# check result
if test "${$1_res}" = "unknown" ; then
	AC_MSG_RESULT(fail)
	exit 1
fi

AST_CONF_FILE=$lt_sysroot$astsysconfdir/asterisk/asterisk.conf

[exclamation_mark=`cat ${AST_CONF_FILE} | ${GREP} -e '\[directories\]' | ${SED} -e 's:^[[:space:]]*\[directories\][[:space:]]*(\(!\)).*:\1:'`]
if test "${exclamation_mark}" != "!" ; then
[strip_prefix_dir=`LANG=C printf "$1" | ${SED} -e 's:ast::'`]
[$1_res=`cat ${AST_CONF_FILE} | ${GREP} ${strip_prefix_dir} | ${SED} -e 's:^.*[[:space:]]*=>[[:space:]]*\(/[^;[:space:]]\+\).*:\1:'`]
fi

# check result
if test "${$1_res}" != "unknown" ; then
	$1=${$1_res}
	AC_SUBST([$1])
	AC_MSG_RESULT("${$1_res}")
else
	AC_MSG_RESULT(fail)
	exit 1
fi
])
