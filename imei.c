/******************************************************************************/
/* imei.c                                                                     */
/******************************************************************************/

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "imei.h"

//------------------------------------------------------------------------------
// imei_calc_check_digit()
//------------------------------------------------------------------------------
int imei_calc_check_digit(const char *imei)
{
	int res;
	size_t i;
	int sum;
	char dgt[14];
	char *ch;
	
	// check for present
	if (!imei)
		return -EIMEI_EMPTY;
	// check for length
	if (strlen(imei) < 14)
		return -EIMEI_SHORT;
	// check for valid symbols
	for (i = 0; i < strlen(imei); i++)
		if (!isdigit(imei[i]))
			return -EIMEI_ILLEG;

	ch = (char *)imei;

	sum = 0;
	for (i=0; i<14; i++)
	{
		// get value of digit
		dgt[i] = *ch++ - '0';
		// multiply by 2 every 2-th digit and sum
		if (i & 1) {
			dgt[i] <<= 1;
			sum += dgt[i]/10 + dgt[i]%10;
		} else
			sum += dgt[i];
	}
	// calc complementary to mod 10
	res = sum%10;
	res = res ? 10 - res : 0;
	res += '0';

	return (char)res;
}
//------------------------------------------------------------------------------
// end of imei_calc_check_digit()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// imei_is_valid()
//------------------------------------------------------------------------------
int imei_is_valid(const char *imei)
{
	int res;
	char cs;

	if ((res = imei_calc_check_digit(imei)) < 0)
		return -res;

	if (strlen(imei) > 14) {
		cs = (char)res;
		if (cs != imei[14])
			return -EIMEI_BADCS;
	}

	return -EIMEI_VALID;
}
//------------------------------------------------------------------------------
// end of imei_is_valid()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// imei_strerror()
//------------------------------------------------------------------------------
const char *imei_strerror(int errno)
{
	switch (errno)
	{
		case EIMEI_VALID: return "valid";
		case EIMEI_EMPTY: return "empty";
		case EIMEI_SHORT: return "to short";
		case EIMEI_ILLEG: return "has illegal symbol";
		case EIMEI_BADCS: return "bad check digit";
		default: return "unknown error";
	}
}
//------------------------------------------------------------------------------
// end of imei_strerror()
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of imei.c                                                              */
/******************************************************************************/
