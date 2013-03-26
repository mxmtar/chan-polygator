#
# get astersik defaults
#
# PG_ASTERISK_GET_DEFAULTS([variable], [value])

AC_DEFUN([PG_ASTERISK_GET_DEFAULTS],
[

AC_MSG_CHECKING([for default value $1])

PG_DEFAULTS_RESULT=$2
PG_DEFAULTS_FILE=defaults

if test -f ${PG_DEFAULTS_FILE} ; then
	[PG_DEFAULTS_RESULT=`cat ${PG_DEFAULTS_FILE} | ${AWK} -F '=' '{ k=1; v=2; if($k=="$1") print $v; }'`]
fi

AC_MSG_RESULT(${PG_DEFAULTS_RESULT})

if test "$1" = "gsm.enable" ; then
	if test "${PG_DEFAULTS_RESULT}" = "yes" ; then
		AC_DEFINE([PG_GSM_ENABLE_DEFAULT], [1], [Define default GSM channel power state.])
	else
		AC_DEFINE([PG_GSM_ENABLE_DEFAULT], [0], [Define default GSM channel power state.])
	fi
elif test "$1" = "gsm.context" ; then
	AC_DEFINE_UNQUOTED([PG_GSM_CONTEXT_DEFAULT], ["${PG_DEFAULTS_RESULT}"], [Define default context value.])
elif test "$1" = "gsm.incoming" ; then
	if test "${PG_DEFAULTS_RESULT}" = "spec" ; then
		AC_DEFINE([PG_GSM_INCOMING_DEFAULT], [1], [Define default GSM incoming call type.])
	elif test "${PG_DEFAULTS_RESULT}" = "dyn" ; then
		AC_DEFINE([PG_GSM_INCOMING_DEFAULT], [2], [Define default GSM incoming call type.])
	else
		AC_DEFINE([PG_GSM_INCOMING_DEFAULT], [0], [Define default GSM incoming call type.])
	fi
elif test "$1" = "gsm.incomingto" ; then
	AC_DEFINE_UNQUOTED([PG_GSM_INCOMINGTO_DEFAULT], ["${PG_DEFAULTS_RESULT}"], [Define default incomingto value.])
elif test "$1" = "gsm.outgoing" ; then
	if test "${PG_DEFAULTS_RESULT}" = "allow" ; then
		AC_DEFINE([PG_GSM_OUT_CALL_PERMISSION_DEFAULT], [1], [Define default GSM outgoing call permission.])
	else
		AC_DEFINE([PG_GSM_OUT_CALL_PERMISSION_DEFAULT], [0], [Define default GSM outgoing call permission.])
	fi
fi

])
