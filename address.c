/******************************************************************************/
/* address.c                                                                  */
/******************************************************************************/

#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <locale.h>
#include <langinfo.h>

#include "address.h"

//------------------------------------------------------------------------------
// is_address_string()
//------------------------------------------------------------------------------
int is_address_string(const char *buf)
{
	if (!buf || !strlen(buf))
		return 0;
	// check first symbol
	if ((!isdigit(*buf)) && (*buf != '+') && (*buf != '*')
		&& (*buf != '#') && (*buf != 'p') && (*buf != 'w'))
		return 0;
	buf++;
	// check rest symbols
	while (*buf)
	{
		if (!isdigit(*buf) && (*buf != '*')
			&& (*buf != '#') && (*buf != 'p') && (*buf != 'w'))
			return 0;
		buf++;
	}
	return 1;
}
//------------------------------------------------------------------------------
// end of is_address_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// is_address_equal()
//------------------------------------------------------------------------------
int is_address_equal(struct address *lhs, struct address *rhs)
{
	if (!lhs || !rhs) return 0;

	if (lhs->type.full == rhs->type.full) {
		if (strcmp(lhs->value, rhs->value))
			return 0;
	} else
		return 0;

	return 1;
}
//------------------------------------------------------------------------------
// end of is_address_equal()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// address_classify()
//------------------------------------------------------------------------------
void address_classify(const char *input, struct address *addr)
{
	// copy number
	memset(addr, 0, sizeof(struct address));
	strcpy(addr->value, input);
	addr->length = strlen(addr->value);
	addr->type.bits.reserved = 1;
	// get numbering plan
	if ((is_address_string(addr->value)) && (addr->length > 7))
		addr->type.bits.numbplan = NUMBERING_PLAN_ISDN_E164;
	else
		addr->type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
	// get type of number
	if (((addr->value[0] == '0') && (addr->value[1] == '0')) ||
		((addr->value[0] == '0') && (addr->value[1] == '0') && (addr->value[2] == '0')) ||
			((addr->value[0] == '+'))){
		addr->type.bits.typenumb = TYPE_OF_NUMBER_INTERNATIONAL;
	} else if (addr->value[0] == '0') {
		addr->type.bits.typenumb = TYPE_OF_NUMBER_NATIONAL;
	} else {
		addr->type.bits.typenumb = TYPE_OF_NUMBER_UNKNOWN;
	}

	address_normalize(addr);
}
//------------------------------------------------------------------------------
// end of address_classify()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// address_normalize()
//------------------------------------------------------------------------------
void address_normalize(struct address *addr)
{
	char buf[MAX_ADDRESS_LENGTH];
	int len;

	iconv_t tc;

	char *ib;
	char *ob;
	size_t incnt;
	size_t outcnt;
	size_t outres;

	if ((addr->type.bits.typenumb == TYPE_OF_NUMBER_INTERNATIONAL) &&
			(addr->type.bits.numbplan == NUMBERING_PLAN_ISDN_E164)) {
		//
		memset(buf, 0, MAX_ADDRESS_LENGTH);
		//
		if ((addr->value[0] == '0') && (addr->value[1] == '0')) {
			len = sprintf(buf, "%.*s", addr->length-2, &addr->value[2]);
			addr->length = len;
		} else if ((addr->value[0] == '0') && (addr->value[1] == '0') && (addr->value[2] == '0')) {
			len = sprintf(buf, "%.*s", addr->length-3, &addr->value[3]);
			addr->length = len;
		} else if (addr->value[0] == '+') {
			len = sprintf(buf, "%.*s", addr->length-1, &addr->value[1]);
			addr->length = len;
		} else {
			len = sprintf(buf, "%.*s", addr->length, &addr->value[0]);
			addr->length = len;
		}
		strcpy(addr->value, buf);
	} else if (addr->type.bits.typenumb == TYPE_OF_NUMBER_ALPHANUMGSM7) {
		tc = iconv_open("UTF-8", "UCS-2BE");
		if (tc == (iconv_t)-1) {
			// converter not created
			len = sprintf(buf, "unknown");
			addr->length = len;
		} else {
			ib = addr->value;
			incnt = addr->length;
			ob = buf;
			outcnt = 256;
			outres = iconv(tc, &ib, &incnt, &ob, &outcnt);
			if (outres == (size_t)-1) {
				// convertation failed
				len = sprintf(buf, "unknown");
				addr->length = len;
			} else {
				len = (ob - buf);
				addr->length = len;
				*(buf+len) = '\0';
			}
			// close converter
			iconv_close(tc);
		}
		strcpy(addr->value, buf);
	}
}
//------------------------------------------------------------------------------
// end of address_normalize()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// address_show()
//------------------------------------------------------------------------------
char *address_show(char *buf, struct address *addr, int full)
{
	int len;

	if (!buf || !addr)
		return "unknown";

	*buf = '\0';
	len = 0;

	if ((addr->type.bits.typenumb == TYPE_OF_NUMBER_INTERNATIONAL) &&
				(addr->type.bits.numbplan == NUMBERING_PLAN_ISDN_E164)) {
		len += sprintf(buf+len, "+%.*s", addr->length, addr->value);
	} else
		len += sprintf(buf+len, "%.*s", addr->length, addr->value);

	if (full) {
		// type of number
		len += sprintf(buf+len, ", type=");
		switch (addr->type.bits.typenumb)
		{
			case TYPE_OF_NUMBER_UNKNOWN:
				len += sprintf(buf+len, "unknown");
				break;
			case TYPE_OF_NUMBER_INTERNATIONAL:
				len += sprintf(buf+len, "international");
				break;
			case TYPE_OF_NUMBER_NATIONAL:
				len += sprintf(buf+len, "national");
				break;
			case TYPE_OF_NUMBER_NETWORK:
				len += sprintf(buf+len, "network");
				break;
			case TYPE_OF_NUMBER_SUBSCRIBER:
				len += sprintf(buf+len, "subscriber");
				break;
			case TYPE_OF_NUMBER_ALPHANUMGSM7:
				len += sprintf(buf+len, "alphanumeric");
				break;
			case TYPE_OF_NUMBER_ABBREVIATED:
				len += sprintf(buf+len, "abbreviated");
				break;
			case TYPE_OF_NUMBER_RESERVED:
				len += sprintf(buf+len, "reserved");
				break;
			default:
				len += sprintf(buf+len, "unknown");
				break;
		}
		// numbering plan
		len += sprintf(buf+len, ", plan=");
		switch (addr->type.bits.numbplan)
		{
			case NUMBERING_PLAN_UNKNOWN:
				len += sprintf(buf+len, "unknown");
				break;
			case NUMBERING_PLAN_ISDN_E164:
				len += sprintf(buf+len, "isdn");
				break;
			case NUMBERING_PLAN_DATA_X121:
				len += sprintf(buf+len, "data");
				break;
			case NUMBERING_PLAN_TELEX:
				len += sprintf(buf+len, "telex");
				break;
			case NUMBERING_PLAN_NATIONAL:
				len += sprintf(buf+len, "national");
				break;
			case NUMBERING_PLAN_PRIVATE:
				len += sprintf(buf+len, "private");
				break;
			case NUMBERING_PLAN_ERMES:
				len += sprintf(buf+len, "ermes");
				break;
			case NUMBERING_PLAN_RESERVED:
				len += sprintf(buf+len, "reserved");
				break;
			default:
				len += sprintf(buf+len, "unknown=%d", addr->type.bits.typenumb);
				break;
		}
	} // full

	return buf;
}
//------------------------------------------------------------------------------
// end of address_show()
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of address.c                                                           */
/******************************************************************************/
