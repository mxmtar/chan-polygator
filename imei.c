/******************************************************************************/
/* imei.c                                                                     */
/******************************************************************************/

#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "imei.h"
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// imei_calc_check_digit()
//------------------------------------------------------------------------------
int imei_calc_check_digit(const char *imei)
{
	int res;
	int i;
	int sum;
	char dgt[14];
	char *ch;
	
	// check for imei present
	if (!imei)
		return -1;
	ch = (char *)imei;
	//
	sum = 0;
	for (i=0; i<14; i++)
	{
		// check input string for valid symbol
		if (*ch == '\0')
			return -2;
		if (!isdigit(*ch))
			return -3;
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

/******************************************************************************/
/* end of imei.c                                                              */
/******************************************************************************/
