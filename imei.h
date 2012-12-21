/******************************************************************************/
/* imei.h                                                                     */
/******************************************************************************/

#ifndef __IMEI_H__
#define __IMEI_H__

#include <sys/types.h>

enum {
	EIMEI_VALID = 0,
	EIMEI_EMPTY = 1,
	EIMEI_SHORT = 2,
	EIMEI_ILLEG = 3,
	EIMEI_BADCS = 4,
};

extern int imei_calc_check_digit(const char *imei);
extern int imei_is_valid(const char *imei);
extern const char *imei_strerror(int err);

#endif //__IMEI_H__

/******************************************************************************/
/* end of imei.h                                                              */
/******************************************************************************/
