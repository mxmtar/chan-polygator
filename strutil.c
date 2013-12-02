/******************************************************************************/
/* strutil.c                                                                  */
/******************************************************************************/

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "strutil.h"

const char char_lf = 0x0a;
const char char_cr = 0x0d;
const char char_ctrlz = 0x1a;

//------------------------------------------------------------------------------
#define UCS2_UNKNOWN_SYMBOL 0x3F00
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_true()
//------------------------------------------------------------------------------
int str_true(const char *s)
{
	if (!strlen(s)) {
		return 0;
	}

	if (!strcasecmp(s, "yes") ||
		!strcasecmp(s, "true") ||
		!strcasecmp(s, "y") ||
		!strcasecmp(s, "t") ||
		!strcasecmp(s, "1") ||
		!strcasecmp(s, "on") ||
		!strcasecmp(s, "run") ||
		!strcasecmp(s, "active")) {
		return -1;
	}

	return 0;
}
//------------------------------------------------------------------------------
// end of str_true()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_xchg()
//------------------------------------------------------------------------------
int str_xchg(const char *str, char p, char r)
{
	int cnt = -1;
	char *c;

	if (str) {
		c = (char *)str;
		cnt = 0;
		while (*c) {
			if (*c == p) {
				*c = r;
				cnt++;
			}
			c++;
		}
	}

	return cnt;
}
//------------------------------------------------------------------------------
// end of str_xchg()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// is_str_digit()
//------------------------------------------------------------------------------
int is_str_digit(const char *buf)
{
	int len;
	char *test;

	if (!(test = (char *)buf)) {
		return 0;
	}
	if (!(len = strlen(test))) {
		return 0;
	}

	while (len--) {
		if (!isdigit(*test++)) {
			return 0;
		}
	}
	return 1;
}
//------------------------------------------------------------------------------
// end of is_str_digit()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// is_str_xdigit()
//------------------------------------------------------------------------------
int is_str_xdigit(const char *buf)
{
	int len;
	char *test;

	if (!(test = (char *)buf)) {
		return 0;
	}
	if (!(len = strlen(test))) {
		return 0;
	}

	while (len--) {
		if (!isxdigit(*test++)) {
			return 0;
		}
	}
	return 1;
}
//------------------------------------------------------------------------------
// end of is_str_xdigit()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// is_str_printable()
//------------------------------------------------------------------------------
int is_str_printable(const char *buf)
{
	int len;
	char *test;

	if (!(test = (char *)buf)) {
		return 0;
	}
	if (!(len = strlen(test))) {
		return 0;
	}

	while (len--) {
		if (!isprint(*test++)) {
			return 0;
		}
	}
	return 1;
}
//------------------------------------------------------------------------------
// end of is_str_printable()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_digit_to_bcd()
//------------------------------------------------------------------------------
void str_digit_to_bcd(const char *instr, int inlen, char *out)
{
	if (inlen % 2) inlen--;

	do {
		if (inlen % 2) {
			*out ^= 0x0f;
			*out++ |= ((*instr) - 0x30);
		} else {
			*out = (((*instr) - 0x30) << 4) + 0x0f;
		}
		inlen--;
		instr++;
	} while (inlen > 0);
}
//------------------------------------------------------------------------------
// end of str_digit_to_bcd()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_from_bin_to_hex()
// * convert byte by byte data into string HEX presentation *
// * outlen shall be twice bigger than inlen *
// char **instr - input binary buffer
// int *inlen - actual length in input buffer
// char **outstr - output string HEX buffer
// int *outlen - max size of output buffer
//
// * return
//		on error	- (size_t)-1
//		on success	- count of processed bytes
//
//------------------------------------------------------------------------------
size_t str_from_bin_to_hex(char **ibuf, size_t *ilen, char **obuf, size_t *olen)
{
	size_t res;

	size_t len;
	size_t rest;
	char *rdpos;
	char *wrpos;
	char chr;

	// check params
	if ((!ibuf) || (!*ibuf) || (!ilen) || (!obuf) || (!*obuf) || (!olen)) {
		return (size_t)-1;
	}

	res = *ilen;

	rdpos = *ibuf;
	len = *ilen;
	wrpos = *obuf;
	rest = *olen;

	memset(wrpos, 0, rest);

	while (len) {
		if (rest) {
			chr = (*rdpos >> 4) & 0xf;
			if (chr <= 9) {
				*wrpos = chr + '0';
			} else {
				*wrpos = chr - 10 + 'A';
			}
			rest--;
		} else {
			*ibuf = rdpos;
			*ilen = len;
			*obuf = wrpos;
			*olen = rest;
			return (size_t)-1;
		}
		if (rest) {
			chr = (*rdpos) & 0xf;
			if (chr <= 9) {
				*(wrpos+1) = chr + '0';
			} else {
				*(wrpos+1) = chr - 10 + 'A';
			}
			wrpos += 2;
			rest--;
		} else {
			*ibuf = rdpos;
			*ilen = len;
			*obuf = wrpos;
			*olen = rest;
			return (size_t)-1;
		}
		len--;
		rdpos++;
	}

	*ibuf = rdpos;
	*ilen = len;
	*obuf = wrpos;
	*olen = rest;

	return res;
}
//------------------------------------------------------------------------------
// end of str_from_bin_to_hex()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_from_hex_to_bin()
// * convert string in ASCII-HEX presentation to binary data*
// * inlen shall be twice bigger than outlen *
// char **instr - input ASCII-HEX buffer
// size_t *inlen - actual length in input buffer
// char **outstr - output binary data buffer
// sizet *outlen - max size of output buffer
//
// * return
//		on error	- (size_t)-1
//		on success	- count of processed bytes
//
//------------------------------------------------------------------------------
size_t str_from_hex_to_bin(char **ibuf, size_t *ilen, char **obuf, size_t *olen)
{
	size_t res;
	size_t len;
	size_t rest;
	char *rdpos;
	char *wrpos;

	u_int8_t high_nibble;
	u_int8_t low_nibble;

	// check params
	if ((!ibuf) || (!*ibuf) || (!ilen) || (!obuf) || (!*obuf) || (!olen)) {
		return (size_t)-1;
	}

	res = *ilen;

	rdpos = *ibuf;
	len = *ilen;
	wrpos = *obuf;
	rest = *olen;

	// check for inbuf has even length
	if (len % 2) {
		return (size_t)-1;
	}

	memset(wrpos, 0, rest);

	while (len) {
		// check for free space in outbuf
		if (rest) {
			// check for valid input symbols
			if (isxdigit(*rdpos) && isxdigit(*(rdpos + 1))) {
				//
				high_nibble = toupper((int)*rdpos);
				low_nibble = toupper((int)*(rdpos + 1));
				//
				if (isdigit(high_nibble)) {
					high_nibble = high_nibble - '0';
				} else {
					high_nibble = high_nibble - 'A' + 10;
				}
				if (isdigit(low_nibble)) {
					low_nibble = low_nibble - '0';
				} else {
					low_nibble = low_nibble - 'A' + 10;
				}
				*wrpos = (char)((high_nibble << 4) + low_nibble);
			} else {
				*ibuf = rdpos;
				*ilen = len;
				*obuf = wrpos;
				*olen = rest;
				return (size_t)-1;
			}
			rdpos += 2;
			len -= 2;
			wrpos++;
			rest--;
		} else {
			*ibuf = rdpos;
			*ilen = len;
			*obuf = wrpos;
			*olen = rest;
			return (size_t)-1;
		}
	}

	*ibuf = rdpos;
	*ilen = len;
	*obuf = wrpos;
	*olen = rest;

	return res;
}
//------------------------------------------------------------------------------
// end of str_from_hex_to_bin()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_from_ucs2_to_set()
//------------------------------------------------------------------------------
size_t str_from_ucs2_to_set(const char *set, char **ibuf, size_t *ilen, char **obuf, size_t *olen)
{
	size_t len;
	size_t rest;
	char *rdpos;
	char *wrpos;
	char *locbuf;

	char *iptr;
	size_t icnt;
	char *optr;
	size_t ocnt;

	size_t res;
	iconv_t tc;

	u_int16_t unksym;

	// check params
	if ((!set) || (!ibuf) || (!*ibuf) || (!ilen) || (!obuf) || (!*obuf) || (!olen)) {
		return (size_t)-1;
	}

	len = *ilen;
	locbuf = malloc(len + 2);
	if (!locbuf) {
		return (size_t)-1;
	}

	memcpy(locbuf, *ibuf, len);
	rdpos = locbuf;
	rest = *olen;
	wrpos = *obuf;

	memset(wrpos, 0, rest);

	// prepare converter
	tc = iconv_open(set, "UCS-2BE");
	if (tc == (iconv_t)-1) {
		// converter not created
		free(locbuf);
		return (size_t)-1;
	}

	while (len > 2) {
		iptr = rdpos;
		icnt = len;
		optr = wrpos;
		ocnt = rest;
		res = iconv(tc, &iptr, &icnt, &optr, &ocnt);
		if (res == (size_t)-1) {
			if (errno == EILSEQ) {
				unksym = UCS2_UNKNOWN_SYMBOL;
				memcpy(iptr, &unksym, sizeof(u_int16_t));
			} else if(errno == EINVAL) {
				break;
			}
		}
		rdpos = iptr;
		len = icnt;
		wrpos = optr;
		rest = ocnt;
	}

	// close converter
	iconv_close(tc);

	*ibuf = rdpos;
	*ilen = len;
	*obuf = wrpos;
	*olen = rest;

	free(locbuf);
	return 0;
}
//------------------------------------------------------------------------------
// end of str_from_ucs2_to_set()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_from_set_to_ucs2()
//------------------------------------------------------------------------------
size_t str_from_set_to_ucs2(const char *set, char **ibuf, size_t *ilen, char **obuf, size_t *olen)
{
	size_t res;
	iconv_t tc;

	if ((!set) || (!ibuf) || (!*ibuf) || (!ilen) || (!obuf) || (!*obuf) || (!olen)) {
		return (size_t)-1;
	}

	tc = iconv_open("UCS-2BE", set);
	if (tc == (iconv_t)-1) {
		return (size_t)-1;
	}

	res = iconv(tc, ibuf, ilen, obuf, olen);
	if (res == (size_t)-1) {
		iconv_close(tc);
		return (size_t)-1;
	}

	iconv_close(tc);

	return 0;
}
//------------------------------------------------------------------------------
// end of str_from_specset_to_ucs2()
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of strutil.c                                                           */
/******************************************************************************/
