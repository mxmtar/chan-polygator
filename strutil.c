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
	if (!strlen(s))
		return 0;

	if (!strcasecmp(s, "yes") ||
		!strcasecmp(s, "true") ||
		!strcasecmp(s, "y") ||
		!strcasecmp(s, "t") ||
		!strcasecmp(s, "1") ||
		!strcasecmp(s, "on") ||
		!strcasecmp(s, "run") ||
		!strcasecmp(s, "active"))
		return -1;

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
		while (*c)
		{
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

	if (!(test = (char *)buf))
		return 0;
	if (!(len = strlen(test)))
		return 0;

	while (len--)
	{
		if (!isdigit(*test++))
			return 0;
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

	if (!(test = (char *)buf))
		return 0;
	if (!(len = strlen(test)))
		return 0;

	while (len--)
	{
		if (!isxdigit(*test++))
			return 0;
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

	if (!(test = (char *)buf))
		return 0;
	if (!(len = strlen(test)))
		return 0;

	while (len--)
	{
		if (!isprint(*test++))
			return 0;
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
	if (inlen%2) inlen--;
	do {
		if (inlen%2) {
			*out ^= 0x0f;
			*out++ |= ((*instr) - 0x30);
		} else
			*out = (((*instr) - 0x30) << 4) + 0x0f;
		inlen--;
		instr++;
	} while(inlen > 0);
}
//------------------------------------------------------------------------------
// end of str_digit_to_bcd()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_bin_to_hex()
// * convert byte by byte data into string HEX presentation *
// * outlen shall be twice bigger than inlen *
// char **instr - input binary buffer
// int *inlen - actual length in input buffer
// char **outstr - output string HEX buffer
// int *outlen - max size of output buffer
//
// * return 0 - success
// * return -1 - on fail
//------------------------------------------------------------------------------
int str_bin_to_hex(char **instr, int *inlen, char **outstr, int *outlen)
{
	int len;
	int rest;
	char *rdpos;
	char *wrpos;
	char chr;

	// check params
	if ((!instr) || (!*instr) || (!inlen) || (!outstr) || (!*outstr) || (!outlen))
		return -1;

	len = *inlen;
	rdpos = *instr;
	rest = *outlen;
	wrpos = *outstr;

	memset(wrpos, 0, rest);

	while (len > 0)
	{
		if (rest > 0) {
			chr = (*rdpos >> 4) & 0xf;
			if (chr <= 9)
				*wrpos = chr + '0';
			else
				*wrpos = chr - 10 + 'A';
			rest--;
		} else {
			*instr = rdpos;
			*inlen = len;
			*outstr = wrpos;
			*outlen = rest;
			return -1;
		}
		if (rest > 0) {
			chr = (*rdpos) & 0xf;
			if (chr <= 9)
				*(wrpos+1) = chr + '0';
			else
				*(wrpos+1) = chr - 10 + 'A';
			wrpos += 2;
			rest--;
		} else {
			*instr = rdpos;
			*inlen = len;
			*outstr = wrpos;
			*outlen = rest;
			return -1;
		}
		len--;
		rdpos++;
	}

	*instr = rdpos;
	*inlen = len;
	*outstr = wrpos;
	*outlen = rest;

	return 0;
}
//------------------------------------------------------------------------------
// end of str_bin_to_hex()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// str_hex_to_bin()
// * convert string in ASCII-HEX presentation to binary data*
// * inlen shall be twice bigger than outlen *
// char **instr - input ASCII-HEX buffer
// int *inlen - actual length in input buffer
// char **outstr - output binary data buffer
// int *outlen - max size of output buffer
//
// * return 0 - success
// * return -1 - on fail
//------------------------------------------------------------------------------
int str_hex_to_bin(char **instr, int *inlen, char **outstr, int *outlen)
{
	int len;
	int rest;
	char *rdpos;
	char *wrpos;

	char high_nibble;
	char low_nibble;

	// check params
	if ((!instr) || (!*instr) || (!inlen) || (!outstr) || (!*outstr) || (!outlen))
		return -1;

	len = *inlen;
	rdpos = *instr;
	rest = *outlen;
	wrpos = *outstr;

	// check for inbuf has even length
	if (len%2)
		return -1;

	memset(wrpos, 0, rest);

	while (len > 0)
	{
		// check for free space in outbuf
		if (rest > 0) {
			// check for valid input symbols
			if (isxdigit(*rdpos) && isxdigit(*(rdpos+1))) {
				//
				*rdpos = (char)toupper((int)*rdpos);
				*(rdpos+1) = (char)toupper((int)*(rdpos+1));
				//
				if (isdigit(*rdpos))
					high_nibble = *rdpos - '0';
				else
					high_nibble = *rdpos - 'A' + 10;
				//
				if (isdigit(*(rdpos+1)))
					low_nibble = *(rdpos+1) - '0';
				else
					low_nibble = *(rdpos+1) - 'A' + 10;
				//
				*wrpos = (char)((high_nibble<<4) + (low_nibble));
			} else {
				*instr = rdpos;
				*inlen = len;
				*outstr = wrpos;
				*outlen = rest;
				return -1;
			}
		}
		len -= 2;
		rdpos += 2;
		wrpos++;
		rest--;
	}

	*instr = rdpos;
	*inlen = len;
	*outstr = wrpos;
	*outlen = rest;

	return 0;
}
//------------------------------------------------------------------------------
// end of str_hex_to_bin()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// from_ucs2_to_specset()
//------------------------------------------------------------------------------
int from_ucs2_to_specset(char *specset, char **instr, int *inlen, char **outstr, int *outlen)
{
	int len;
	int rest;
	char *rdpos;
	char *wrpos;
	char *locbuf;

	char *ib;
	size_t incnt;
	char *ob;
	size_t outcnt;
	size_t outres;

	iconv_t tc;

	unsigned short *symptr;

	// check params
	if ((!specset) || (!instr) || (!*instr) || (!inlen) || (!outstr) || (!*outstr) || (!outlen))
		return -1;

	len = *inlen;
	locbuf = malloc(len+2);
	if (!locbuf)
		return -1;

	memcpy(locbuf, *instr, len);
	rdpos = locbuf;
	rest = *outlen;
	wrpos = *outstr;

	memset(wrpos, 0, rest);

	// prepare converter
	tc = iconv_open(specset, "UCS-2BE");
	if (tc == (iconv_t)-1) {
		// converter not created
		free(locbuf);
		return -1;
	}

	while (len > 2)
	{
		ib = rdpos;
		incnt = (size_t)len;
		ob = wrpos;
		outcnt = (size_t)rest;
		outres = iconv(tc, &ib, &incnt, &ob, &outcnt);
		if (outres == (size_t)-1) {
			if (errno == EILSEQ) {
				symptr = (unsigned short *)ib;
				*symptr = UCS2_UNKNOWN_SYMBOL;
			} else if(errno == EINVAL)
				break;
		}
		rdpos = ib;
		len = (int)incnt;
		wrpos = ob;
		rest = (int)outcnt;
	}

	// close converter
	iconv_close(tc);

	*instr = rdpos;
	*inlen = len;
	*outstr = wrpos;
	*outlen = rest;

	free(locbuf);
	return 0;
}
//------------------------------------------------------------------------------
// end of from_ucs2_to_specset()
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of strutil.c                                                           */
/******************************************************************************/
