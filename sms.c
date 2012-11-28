/******************************************************************************/
/* sms.c                                                                      */
/******************************************************************************/

#include <sys/types.h>
#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <iconv.h>

#include "strutil.h"

#include "sms.h"

//------------------------------------------------------------------------------
static const unsigned short gsm_to_unicode_le[128] = {
	// 000xxxx
	0x0040,		// @ - 00
	0x00A3,		// £ - 01
	0x0024,		// $ - 02
	0x00A5,		// ¥ - 03
	0x00E8,		// è - 04
	0x00E9,		// é - 05
	0x00F9,		// ù - 06
	0x00EC,		// ì - 07
	0x00F2,		// ò - 08
	0x00E7,		// ç - 09
	0x000A,		// LF- 0A
	0x00D8,		// Ø - 0B
	0x00F8,		// ø - 0C
	0x000D,		// CR- 0D
	0x00C5,		// Å - 0E
	0x00E5,		// å - 0F
	// 001xxxx
	0x0394,		// Δ - 10
	0x005F,		// _ - 11
	0x03A6,		// Φ - 12
	0x0393,		// Γ - 13
	0x039B,		// Λ - 14
	0x03A9,		// Ω - 15
	0x03A0,		// Π - 16
	0x03A8,		// Ψ - 17
	0x03A3,		// Σ - 18
	0x0398,		// Θ - 19
	0x039E,		// Ξ - 1A
	0x001B,		//ESC- 1B
	0x00C6,		// Æ - 1C
	0x00E6,		// æ - 1D
	0x00DF,		// ß - 1E
	0x00C9,		// É - 1F
	// 010xxxx
	0x0020,		// SP- 20
	0x0021,		// ! - 21
	0x0022,		// " - 22
	0x0023,		// # - 23
	0x00A4,		// ¤ - 24
	0x0025,		// % - 25
	0x0026,		// & - 26
	0x0027,		// ' - 27
	0x0028,		// ( - 28
	0x0029,		// ) - 29
	0x002A,		// * - 2A
	0x002B,		// + - 2B
	0x002C,		// , - 2C
	0x002D,		// - - 2D
	0x002E,		// . - 2E
	0x002F,		// / - 2F
	// 011xxxx
	0x0030,		// 0 - 30
	0x0031,		// 1 - 31
	0x0032,		// 2 - 32
	0x0033,		// 3 - 33
	0x0034,		// 4 - 34
	0x0035,		// 5 - 35
	0x0036,		// 6 - 36
	0x0037,		// 7 - 37
	0x0038,		// 8 - 38
	0x0039,		// 9 - 39
	0x003A,		// : - 3A
	0x003B,		// ; - 3B
	0x003C,		// < - 3C
	0x003D,		// = - 3D
	0x003E,		// > - 3E
	0x003F,		// ? - 3F
	// 100xxxx
	0x00A1,		// ¡ - 40
	0x0041,		// A - 41
	0x0042,		// B - 42
	0x0043,		// C - 43
	0x0044,		// D - 44
	0x0045,		// E - 45
	0x0046,		// F - 46
	0x0047,		// G - 47
	0x0048,		// H - 48
	0x0049,		// I - 49
	0x004A,		// J - 4A
	0x004B,		// K - 4B
	0x004C,		// L - 4C
	0x004D,		// M - 4D
	0x004E,		// N - 4E
	0x004F,		// O - 4F
	// 101xxxx
	0x0050,		// P - 50
	0x0051,		// Q - 51
	0x0052,		// R - 52
	0x0053,		// S - 53
	0x0054,		// T - 54
	0x0055,		// U - 55
	0x0056,		// V - 56
	0x0057,		// W - 57
	0x0058,		// X - 58
	0x0059,		// Y - 59
	0x005A,		// Z - 5A
	0x00C4,		// Ä - 5B
	0x00D6,		// Ö - 5C
	0x00D1,		// Ñ - 5D
	0x00DC,		// Ü - 5E
	0x00A7,		// § - 5F
	// 110xxxx
	0x00BF,		// ¿ - 60
	0x0061,		// a - 61
	0x0062,		// b - 62
	0x0063,		// c - 63
	0x0064,		// d - 64
	0x0065,		// e - 65
	0x0066,		// f - 66
	0x0067,		// g - 67
	0x0068,		// h - 68
	0x0069,		// i - 69
	0x006A,		// j - 6A
	0x006B,		// k - 6B
	0x006C,		// l - 6C
	0x006D,		// m - 6D
	0x006E,		// n - 6E
	0x006F,		// o - 6F
	// 111xxxx
	0x0070,		// p - 70
	0x0071,		// q - 71
	0x0072,		// r - 72
	0x0073,		// s - 73
	0x0074,		// t - 74
	0x0075,		// u - 75
	0x0076,		// v - 76
	0x0077,		// w - 77
	0x0078,		// x - 78
	0x0079,		// y - 79
	0x007A,		// z - 7A
	0x00E4,		// ä - 7B
	0x00F6,		// ö - 7C
	0x00F1,		// ñ - 7D
	0x00FC,		// ü - 7E
	0x00E0,		// à - 7F
};
//------------------------------------------------------------------------------
static const unsigned short gsm_to_unicode_be[128] = {
	// 000xxxx
	0x4000,		// @ - 00
	0xA300,		// £ - 01
	0x2400,		// $ - 02
	0xA500,		// ¥ - 03
	0xE800,		// è - 04
	0xE900,		// é - 05
	0xF900,		// ù - 06
	0xEC00,		// ì - 07
	0xF200,		// ò - 08
	0xE700,		// ç - 09
	0x0A00,		// LF- 0A
	0xD800,		// Ø - 0B
	0xF800,		// ø - 0C
	0x0D00,		// CR- 0D
	0xC500,		// Å - 0E
	0xE500,		// å - 0F
	// 001xxxx
	0x9403,		// Δ - 10
	0x5F00,		// _ - 11
	0xA603,		// Φ - 12
	0x9303,		// Γ - 13
	0x9B03,		// Λ - 14
	0xA903,		// Ω - 15
	0xA003,		// Π - 16
	0xA803,		// Ψ - 17
	0xA303,		// Σ - 18
	0x9803,		// Θ - 19
	0x9E03,		// Ξ - 1A
	0x1B00,		//ESC- 1B
	0xC600,		// Æ - 1C
	0xE600,		// æ - 1D
	0xDF00,		// ß - 1E
	0xC900,		// É - 1F
	// 010xxxx
	0x2000,		// SP- 20
	0x2100,		// ! - 21
	0x2200,		// " - 22
	0x2300,		// # - 23
	0xA400,		// ¤ - 24
	0x2500,		// % - 25
	0x2600,		// & - 26
	0x2700,		// ' - 27
	0x2800,		// ( - 28
	0x2900,		// ) - 29
	0x2A00,		// * - 2A
	0x2B00,		// + - 2B
	0x2C00,		// , - 2C
	0x2D00,		// - - 2D
	0x2E00,		// . - 2E
	0x2F00,		// / - 2F
	// 011xxxx
	0x3000,		// 0 - 30
	0x3100,		// 1 - 31
	0x3200,		// 2 - 32
	0x3300,		// 3 - 33
	0x3400,		// 4 - 34
	0x3500,		// 5 - 35
	0x3600,		// 6 - 36
	0x3700,		// 7 - 37
	0x3800,		// 8 - 38
	0x3900,		// 9 - 39
	0x3A00,		// : - 3A
	0x3B00,		// ; - 3B
	0x3C00,		// < - 3C
	0x3D00,		// = - 3D
	0x3E00,		// > - 3E
	0x3F00,		// ? - 3F
	// 100xxxx
	0xA100,		// ¡ - 40
	0x4100,		// A - 41
	0x4200,		// B - 42
	0x4300,		// C - 43
	0x4400,		// D - 44
	0x4500,		// E - 45
	0x4600,		// F - 46
	0x4700,		// G - 47
	0x4800,		// H - 48
	0x4900,		// I - 49
	0x4A00,		// J - 4A
	0x4B00,		// K - 4B
	0x4C00,		// L - 4C
	0x4D00,		// M - 4D
	0x4E00,		// N - 4E
	0x4F00,		// O - 4F
	// 101xxxx
	0x5000,		// P - 50
	0x5100,		// Q - 51
	0x5200,		// R - 52
	0x5300,		// S - 53
	0x5400,		// T - 54
	0x5500,		// U - 55
	0x5600,		// V - 56
	0x5700,		// W - 57
	0x5800,		// X - 58
	0x5900,		// Y - 59
	0x5A00,		// Z - 5A
	0xC400,		// Ä - 5B
	0xD600,		// Ö - 5C
	0xD100,		// Ñ - 5D
	0xDC00,		// Ü - 5E
	0xA700,		// § - 5F
	// 110xxxx
	0xBF00,		// ¿ - 60
	0x6100,		// a - 61
	0x6200,		// b - 62
	0x6300,		// c - 63
	0x6400,		// d - 64
	0x6500,		// e - 65
	0x6600,		// f - 66
	0x6700,		// g - 67
	0x6800,		// h - 68
	0x6900,		// i - 69
	0x6A00,		// j - 6A
	0x6B00,		// k - 6B
	0x6C00,		// l - 6C
	0x6D00,		// m - 6D
	0x6E00,		// n - 6E
	0x6F00,		// o - 6F
	// 111xxxx
	0x7000,		// p - 70
	0x7100,		// q - 71
	0x7200,		// r - 72
	0x7300,		// s - 73
	0x7400,		// t - 74
	0x7500,		// u - 75
	0x7600,		// v - 76
	0x7700,		// w - 77
	0x7800,		// x - 78
	0x7900,		// y - 79
	0x7A00,		// z - 7A
	0xE400,		// ä - 7B
	0xF600,		// ö - 7C
	0xF100,		// ñ - 7D
	0xFC00,		// ü - 7E
	0xE000,		// à - 7F
};
#define UCS2_UNKNOWN_SYMBOL 0x3F00
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pdu_parser()
//------------------------------------------------------------------------------
struct pdu *pdu_parser(const char *pduhex, int pduhexlen, int pdulen, time_t ltime, int *err)
{
	char *ip, *op;
	int ilen, olen;
	char *cp;
	char *tp;
	int tlen;

	struct pdu *pdu = NULL;

	char *lpdu = NULL;

	char ucs2data[320];

	struct timeval tv;
	struct tm tmc, *tml;

	// check for valid input
	if (!pduhex || !pduhexlen || !pdulen) {
		if (err) *err = __LINE__;
		goto pdu_parser_error;
	}
	lpdu = strdup(pduhex);
	// create storage
	if (!(pdu = malloc(sizeof(struct pdu)))){
		if (err) *err = __LINE__;
		goto pdu_parser_error;
	}
	memset(pdu, 0, sizeof(struct pdu));

	// set PDU length
	pdu->len = pdulen;
	// convert PDU from hex to bin
	ip = lpdu;
	ilen = pduhexlen;
	op = pdu->buf;
	olen = MAX_PDU_BIN_SIZE;
	if (str_hex_to_bin(&ip, &ilen, &op, &olen)) {
		if (err) *err = __LINE__;
		goto pdu_parser_error;
	}
	pdu->full_len = MAX_PDU_BIN_SIZE - olen;

	cp = pdu->buf;

	// check for SCA present in PDU
	if (pdu->len > pdu->full_len) {
		// is abnormally - sanity check
		if (err) *err = __LINE__;
		goto pdu_parser_error;
	}
/*
	else if (pdu->len == pdu->full_len) {
		// SCA not present in PDU
		sprintf(pdu->scaddr.value, "unknown");
		pdu->scaddr.length = strlen(pdu->scaddr.value);
		pdu->scaddr.type.bits.reserved = 1;
		pdu->scaddr.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
		pdu->scaddr.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
	}
*/
	else {
		// SCA present in PDU
		tlen = *cp++ & 0xff;
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		if (tlen) {
			pdu->scaddr.type.full = *cp++; // get type of sms center address
			pdu->scaddr.length = 0;
			tlen--;
			tp = pdu->scaddr.value;
			while (tlen > 0)
			{
				// low nibble
				if (((*cp & 0x0f) !=  0x0f)) {
					*tp++ = (*cp & 0x0f) + '0';
					pdu->scaddr.length++;
				}
				// high nibble
				if ((((*cp >> 4) & 0x0f) !=  0x0f)) {
					*tp++ = ((*cp >> 4) & 0x0f) + '0';
					pdu->scaddr.length++;
				}
				tlen--;
				cp++;
				if ((cp - pdu->buf) > pdu->full_len) {
					if (err) *err = __LINE__;
					goto pdu_parser_error;
				}
			}
		} else {
			sprintf(pdu->scaddr.value, "unknown");
			pdu->scaddr.length = strlen(pdu->scaddr.value);
			pdu->scaddr.type.bits.reserved = 1;
			pdu->scaddr.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
			pdu->scaddr.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
		}
		address_normalize(&pdu->scaddr);
	}
	// check PDU length
	if ((cp - pdu->buf) > pdu->full_len) {
		if (err) *err = __LINE__;
		goto pdu_parser_error;
	}
	// get first byte of PDU
	pdu->fb.full = *cp++;
	// check PDU length
	if ((cp - pdu->buf) > pdu->full_len) {
		if (err) *err = __LINE__;
		goto pdu_parser_error;
	}
	// select PDU type
	if (pdu->fb.general.mti == MTI_SMS_DELIVER) {
		// originating address
		tlen = pdu->raddr.length = (int)(*cp++ & 0xff); // get length
		if (tlen > 20) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}		
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		if (tlen) {
			pdu->raddr.type.full = *cp++; // get type
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			tp = pdu->raddr.value;
			if (pdu->raddr.type.bits.typenumb == TYPE_OF_NUMBER_ALPHANUMGSM7) {
				//
				ip = cp;
				ilen = (tlen * 4)/7;
				op = pdu->raddr.value;
				olen = MAX_ADDRESS_LENGTH;
				//
				if (gsm7_to_ucs2(&ip, &ilen, 0, &op, &olen)) {
					sprintf(pdu->raddr.value, "unknown");
					pdu->raddr.length = strlen(pdu->raddr.value);
					pdu->raddr.type.bits.reserved = 1;
					pdu->raddr.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
					pdu->raddr.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
				} else
					pdu->raddr.length = MAX_ADDRESS_LENGTH - olen;
				//
				cp += tlen/2;
				if (tlen%2) cp++;
				if ((cp - pdu->buf) > pdu->full_len) {
					if (err) *err = __LINE__;
					goto pdu_parser_error;
				}
			} else {
				while (tlen > 0)
				{
					// low nibble
					if (((*cp & 0x0f) !=  0x0f))
						*tp++ = (*cp & 0x0f) + '0';
					// high nibble
					if ((((*cp >> 4) & 0x0f) !=  0x0f))
						*tp++ = ((*cp >> 4) & 0x0f) + '0';
					tlen -= 2;
					cp++;
					if ((cp - pdu->buf) > pdu->full_len) {
						if (err) *err = __LINE__;
						goto pdu_parser_error;
					}
				}
			}
		} else { // length = 0
			sprintf(pdu->raddr.value, "unknown");
			pdu->raddr.length = strlen(pdu->raddr.value);
			pdu->raddr.type.bits.reserved = 1;
			pdu->raddr.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
			pdu->raddr.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
			cp++; // skip type position
		}
		address_normalize(&pdu->raddr);
		// check PDU length
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		// protocol identifier
		pdu->pid = (unsigned char)(*cp++ & 0xff);
		// check PDU length
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		// data coding scheme
		pdu->dacosc = (unsigned char)(*cp++ & 0xff);
		// check PDU length
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		if (dcs_parser(pdu->dacosc, &pdu->dcs)) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		// service centre time stamp
		gettimeofday(&tv, NULL);
		if ((tml = localtime(&tv.tv_sec))) {
			// year
			tmc.tm_year = (tml->tm_year/100)*100 + (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			// check PDU length
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// month
			tmc.tm_mon = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f)) - 1;
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// day of the month
			tmc.tm_mday = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// hours
			tmc.tm_hour = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// minutes
			tmc.tm_min = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// seconds
			tmc.tm_sec = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// timezone - daylight savings
// 			tmc.tm_isdst = 0;
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// make time_t data
			pdu->sent = mktime(&tmc);
		} else {
			pdu->sent = tv.tv_sec;
			cp += 7;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
		}
		// user data length
		pdu->udl = (int)(*cp++ & 0xff);
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		//----------------------------------------------------------------------
		//
		pdu->delivered = ltime;
		//
	} // end of sms-deliver
/*
	else if(pdu->fb.general.mti == MTI_SMS_SUBMIT){
		;
		}
*/
	else if (pdu->fb.general.mti == MTI_SMS_STATUS_REPORT) {
		// message reference
		pdu->mr = (int)(*cp++ & 0xff);
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		// recipient address
		tlen = pdu->raddr.length = (int)(*cp++ & 0xff); // get length
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		if (tlen) {
			pdu->raddr.type.full = *cp++; // get type
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			tp = pdu->raddr.value;
			if (pdu->raddr.type.bits.typenumb == TYPE_OF_NUMBER_ALPHANUMGSM7) {
				//
				ip = cp;
				ilen = (tlen * 4)/7;
				op = pdu->raddr.value;
				olen = MAX_ADDRESS_LENGTH;
				//
				if (gsm7_to_ucs2(&ip, &ilen, 0, &op, &olen)) {
					sprintf(pdu->raddr.value, "unknown");
					pdu->raddr.length = strlen(pdu->raddr.value);
					pdu->raddr.type.bits.reserved = 1;
					pdu->raddr.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
					pdu->raddr.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
				} else
					pdu->raddr.length = MAX_ADDRESS_LENGTH - olen;
				//
				cp += tlen/2;
				if (tlen%2) cp++;
				if ((cp - pdu->buf) > pdu->full_len) {
					if (err) *err = __LINE__;
					goto pdu_parser_error;
				}
			} else {
				while (tlen > 0)
				{
					// low nibble
					if (((*cp & 0x0f) !=  0x0f))
						*tp++ = (*cp & 0x0f) + '0';
					// high nibble
					if ((((*cp >> 4) & 0x0f) !=  0x0f))
						*tp++ = ((*cp >> 4) & 0x0f) + '0';
					tlen -= 2;
					cp++;
					if ((cp - pdu->buf) > pdu->full_len) {
						if (err) *err = __LINE__;
						goto pdu_parser_error;
					}
				}
			}
		} else{ // length = 0
			sprintf(pdu->raddr.value, "unknown");
			pdu->raddr.length = strlen(pdu->raddr.value);
			pdu->raddr.type.bits.reserved = 1;
			pdu->raddr.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
			pdu->raddr.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
			cp++; // skip type position
		}
		address_normalize(&pdu->raddr);
		// check PDU length
		if ((cp - pdu->buf) > pdu->full_len) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		// service centre time stamp
		gettimeofday(&tv, NULL);
		if ((tml = localtime(&tv.tv_sec))) {
			// year
			tmc.tm_year = (tml->tm_year/100)*100 + (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			// check PDU length
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// month
			tmc.tm_mon = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f)) - 1;
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// day of the month
			tmc.tm_mday = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// hours
			tmc.tm_hour = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// minutes
			tmc.tm_min = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// seconds
			tmc.tm_sec = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// timezone - daylight savings
// 			tmc.tm_isdst = 0;
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// make time_t data
			pdu->sent = mktime(&tmc);
		} else {
			pdu->sent = tv.tv_sec;
			cp += 7;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
		}
		// discharge time
		if ((tml = localtime(&tv.tv_sec))) {
			// year
			tmc.tm_year = (tml->tm_year/100)*100 + (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			// check PDU length
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// month
			tmc.tm_mon = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f)) - 1;
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// day of the month
			tmc.tm_mday = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// hours
			tmc.tm_hour = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// minutes
			tmc.tm_min = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// seconds
			tmc.tm_sec = (((*cp) & 0x0f)*10 + ((*cp >> 4) & 0x0f));
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// timezone - daylight savings
// 			tmc.tm_isdst = 0;
			cp++;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			// make time_t data
			pdu->delivered = mktime(&tmc);
		} else {
			pdu->sent = tv.tv_sec;
			cp += 7;
			if ((cp - pdu->buf) > pdu->full_len) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
		}
		// status
		pdu->status = (unsigned char)(*cp++ & 0xff);
		// optional
		// parameter indicator (optional)
		if ((cp - pdu->buf) <= pdu->full_len)
			pdu->paramind.full = (unsigned char)(*cp++ & 0xff);
		// protocol identifier (optional)
		if (pdu->paramind.bits.pid) {
			if ((cp - pdu->buf) <= pdu->full_len)
				pdu->pid = (unsigned char)(*cp++ & 0xff);
		}
		// data coding  scheme (optional)
		if (pdu->paramind.bits.dcs) {
			if ((cp - pdu->buf) <= pdu->full_len)
				pdu->dacosc = (unsigned char)(*cp++ & 0xff);
		}
		if (dcs_parser(pdu->dacosc, &pdu->dcs)) {
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
		// user data length (optional)
		if (pdu->paramind.bits.udl) {
			if((cp - pdu->buf) <= pdu->full_len)
				pdu->udl = (int)(*cp++ & 0xff);
		}
	} else {
		if (err) *err = __LINE__;
		goto pdu_parser_error;
	}
	// processing user data
	pdu->concat_ref = 0;
	pdu->concat_cnt = 1;
	pdu->concat_num = 1;
	if (pdu->udl) {
		// processing user data header
		if (pdu->fb.general.udhi) {
			tlen = (int)(*cp & 0xff);
			tp = cp+1;
			while (tlen > 0)
			{
				switch ((int)(*tp & 0xff))
				{
					//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
					case 0x00:
						pdu->concat_ref = *(tp+2);
						pdu->concat_cnt = *(tp+3);
						pdu->concat_num = *(tp+4);
						break;
					//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
					default:
						break;
				}
				tlen -= (*(tp+1) + 2);
				tp += (*(tp+1) + 2);
			}
		}
		// get user data
		if (pdu->dcs.charset == DCS_CS_GSM7) {
			// gsm7
			// check for user data header
			tlen = 0;
			if (pdu->fb.general.udhi) {
				ilen = (int)(*cp & 0xff);
				tlen = ((ilen+1)/7)*8;
				if ((ilen+1)%7)
					tlen += ((ilen+1)%7) + 1;
				pdu->udl -= tlen;
			} // end udhi
			//
			ip = cp;
			ilen = pdu->udl;
			op = ucs2data;
			olen = 320;
			if (gsm7_to_ucs2(&ip, &ilen, tlen, &op, &olen)) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			tlen = 320 - olen;
			// convert to utf8
			ip = ucs2data;
			ilen = tlen;
			op = pdu->ud;
			olen = 640;
			if (from_ucs2_to_specset("UTF-8", &ip, &ilen, &op, &olen)) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			pdu->udl = 640 - olen;
		} else if (pdu->dcs.charset == DCS_CS_8BIT) {
			// 8-bit
			// check for user data header
			if (pdu->fb.general.udhi) {
				tlen = (int)(*cp++ & 0xff);
				pdu->udl -= (tlen + 1);
				cp += tlen;
			}
			//
// 			memcpy(pdu->ud, cp, pdu->udl);
			olen = 0;
			for (tlen=0; tlen<pdu->udl; tlen++)
				olen += sprintf(cp+olen,"%02x ", (unsigned char)*(cp+tlen));
		} else if (pdu->dcs.charset == DCS_CS_UCS2) {
			// ucs2
			// check for user data header
			if (pdu->fb.general.udhi) {
				tlen = (int)(*cp++ & 0xff);
				pdu->udl -= (tlen + 1);
				cp += tlen;
			}
			// convert to utf8
			ip = cp;
			ilen = pdu->udl;
			op = pdu->ud;
			olen = 640;
			if (from_ucs2_to_specset("UTF-8", &ip, &ilen, &op, &olen)) {
				if (err) *err = __LINE__;
				goto pdu_parser_error;
			}
			pdu->udl = 640 - olen;
		} else {
			// reserved
			if (err) *err = __LINE__;
			goto pdu_parser_error;
		}
	}

	// return on success
	if (err) *err = 0;
	if (lpdu) free(lpdu);
	return pdu;

pdu_parser_error:
	if (lpdu) free(lpdu);
	if (pdu) free(pdu);
	return NULL;
}
//------------------------------------------------------------------------------
// end of pdu_parser()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// dcs_parser()
//------------------------------------------------------------------------------
int dcs_parser(unsigned char inp, struct dcs *dcs)
{
	if(!dcs) return -1;

	if (inp == 0x00) { // 0000 0000
		// Special Case
		dcs->group = DCS_GROUP_GENERAL;
		dcs->charset = DCS_CS_GSM7;
		dcs->isclass = 0;
		dcs->compres = 0;
	} else if ((inp & 0xC0) == 0x00) { // 00xx xxxx
		// General Data Coding indication
		dcs->group = DCS_GROUP_GENERAL;
		dcs->charset = (inp >> 2) & 0x03;
		dcs->isclass = (inp >> 4) & 0x01;
		dcs->classid = inp & 0x03;
		dcs->compres = (inp >> 5) & 0x01;
	} else if ((inp & 0xC0) == 0x40) { // 01xx xxxx
		// Automatic Deletion Group
		dcs->group = DCS_GROUP_AUTODEL;
		dcs->charset = (inp >> 2) & 0x03;
		dcs->isclass = (inp >> 4) & 0x01;
		dcs->classid = inp & 0x03;
		dcs->compres = (inp >> 5) & 0x01;
	} else if ((inp & 0xF0) == 0xC0) { // 1100 xxxx
		// Message Waiting Indication: Discard Message
		dcs->group = DCS_GROUP_MWI;
		dcs->charset = DCS_CS_GSM7;
		dcs->isclass = 0;
		dcs->compres = 0;
		dcs->mwistore = 0;
		dcs->mwiind = (inp >> 3) & 0x01;
		dcs->mwitype = inp & 0x03;
	} else if ((inp & 0xF0) == 0xD0) { // 1101 xxxx
		// Message Waiting Indication: Store Message
		dcs->group = DCS_GROUP_MWI;
		dcs->charset = DCS_CS_GSM7;
		dcs->isclass = 0;
		dcs->compres = 0;
		dcs->mwistore = 1;
		dcs->mwiind = (inp >> 3) & 0x01;
		dcs->mwitype = inp & 0x03;
	} else if ((inp & 0xF0) == 0xE0) { // 1110 xxxx
		// Message Waiting Indication: Store Message
		dcs->group = DCS_GROUP_MWI;
		dcs->charset = DCS_CS_UCS2;
		dcs->isclass = 0;
		dcs->compres = 0;
		dcs->mwistore = 1;
		dcs->mwiind = (inp >> 3) & 0x01;
		dcs->mwitype = inp & 0x03;
	} else if ((inp & 0xF0) == 0xF0) { // 1111 xxxx
		// Data coding/message class
		dcs->group = DCS_GROUP_DCMC;
		dcs->charset = (inp >> 2) & 0x01;
		dcs->isclass = 1;
		dcs->classid = inp & 0x03;
		dcs->compres = 0;
	} else
		return -1;

	return 0;
}
//------------------------------------------------------------------------------
// end of dcs_parser()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// gsm7_to_ucs2()
//------------------------------------------------------------------------------
int gsm7_to_ucs2(char **instr, int *inlen, int start, char **outstr, int *outlen)
{
	int len;
	int rest;
	char *rdpos;
	unsigned short *wrpos;

	int i;
	unsigned char chridx;
	unsigned int sym4grp;

	// check input
	if (!instr || !*instr || !inlen || !outstr || !*outstr || !outlen)
		return -1;

	len = *inlen;
	rdpos = *instr;
	rest = *outlen;
	wrpos = (unsigned short *)*outstr;

	memset(wrpos, 0, rest);

	i = start;
	while (len > 0)
	{
		if (i%8 < 4) {
			memcpy(&sym4grp, (rdpos+((i/8)*7)), 4);
			chridx = (sym4grp >> ((i%4)*7)) & 0x7f;
		} else {
			memcpy(&sym4grp, (rdpos+((i/8)*7)+3), 4);
			sym4grp >>= 4;
			chridx = (sym4grp >> ((i%4)*7)) & 0x7f;
		}
		//
		*wrpos = gsm_to_unicode_be[chridx];
		//
		rest -= 2;
		wrpos++;
		i++;
		len--;
	}

	*instr = rdpos;
	*inlen = len;
	*outstr = (char *)wrpos;
	*outlen = rest;

	return 0;
}
//------------------------------------------------------------------------------
// end of gsm7_to_ucs2()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// ucs2_to_gsm7()
//------------------------------------------------------------------------------
int ucs2_to_gsm7(char **instr, int *inlen, int start, char **outstr, int *outlen)
{
	int len;
	int rest;
	unsigned short *rdpos;
	char *wrpos;

	int i, j;
	unsigned int sym4grp;

	char *tbuf;

	// check input
	if (!instr || !*instr || !inlen || !outstr || !*outstr || !outlen)
		return -1;
	//
	len = *inlen;
	rdpos = (unsigned short *)*instr;
	rest = *outlen;
	wrpos = *outstr;

	// create temp buffer
	if (!(tbuf = malloc(len/2)))
		return -1;
	memset(tbuf, 0, len/2);
	// get gsm7 alphabet symbols
	for (i=0; i<(len/2); i++)
	{
		for (j=0; j<128; j++)
		{
			if (*(rdpos+i) == gsm_to_unicode_be[j]) {
				*(tbuf+i) = (char)j;
				break;
			}
		}
	}
	// pack into output buffer
	len /= 2;
	i=start;
	while (len)
	{
		//
		if (i%8 < 4) {
			memcpy(&sym4grp, (wrpos+((i/8)*7)), 4);
			sym4grp |= ((*(tbuf+(i-start)) & 0x7f) << ((i%4)*7));
			memcpy((wrpos+((i/8)*7)), &sym4grp, 4);
		} else {
			memcpy(&sym4grp, (wrpos+((i/8)*7)+3), 4);
			sym4grp |= ((*(tbuf+(i-start)) & 0x7f) << (((i%4)*7) + 4));
			memcpy((wrpos+((i/8)*7)+3), &sym4grp, 4);
		}
		len--;
		i++;
	}
	// insert stuff CR symbol
	if ((i&7) == 7) {
		memcpy(&sym4grp, (wrpos+((i/8)*7)+3), 4);
		sym4grp |= ( 0x0d << (((i%4)*7) + 4));
		memcpy((wrpos+((i/8)*7)+3), &sym4grp, 4);
	}
	//
	*instr = (char *)rdpos;
	*inlen = len;
	*outstr = wrpos;
	*outlen = rest;

	free(tbuf);
	return 0;
}
//------------------------------------------------------------------------------
// end of ucs2_to_gsm7()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// is_gsm7_string()
//------------------------------------------------------------------------------
int is_gsm7_string (char *buf)
{
	int res;

	iconv_t tc;
	char *ibuf;
	size_t ilen;
	char *obuf;
	size_t olen;

	unsigned short *ucs2buf;
	int ucs2len;
	int i, j;

	//
	ibuf = buf;
	ilen = strlen(ibuf);
	ucs2len = olen = ilen * 2;
	if (!(obuf = malloc(olen)))
		return -1;
	ucs2buf = (unsigned short *)obuf;
	// convert from utf-8 to ucs-2be - prepare converter
	tc = iconv_open("UCS-2BE", "UTF-8");
	if (tc == (iconv_t)-1) {
		// converter not created
		free(obuf);
		return -1;
	}
	res = iconv(tc, &ibuf, &ilen, &obuf, &olen);
	if (res == (size_t)-1) {
		free(obuf);
		return -1;
	}
	ucs2len -= olen;
	// close converter
	iconv_close(tc);

	res = 1;
	// test for data in gsm7 default alphabet
	for (i=0; i<ucs2len; i++)
	{
		for (j=0; j<128; j++)
		{
			if (*(ucs2buf+i) == gsm_to_unicode_be[j])
				break;
		}
		if (j >= 128) {
			res = 0;
			break;
		}
		if (!res) break;
	}

	free(obuf);
	return res;
}
//------------------------------------------------------------------------------
// end of is_gsm7_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// get_parts_count()
//------------------------------------------------------------------------------
int get_parts_count(char *buf)
{
	int isgsm7;

	iconv_t tc;
	char *ibuf;
	size_t ilen;
	char *obuf;
	size_t olen;

	unsigned short *ucs2buf;
	int ucs2len;
	int i, j;

	//
	ibuf = buf;
	ilen = strlen(ibuf);
	ucs2len = olen = ilen * 2;
	if (!(obuf = malloc(olen)))
		return -1;
	ucs2buf = (unsigned short *)obuf;
	// convert from utf-8 to ucs-2be - prepare converter
	tc = iconv_open("UCS-2BE", "UTF-8");
	if (tc == (iconv_t)-1) {
		// converter not created
		free(obuf);
		return -1;
	}
	isgsm7 = iconv(tc, &ibuf, &ilen, &obuf, &olen);
	if (isgsm7 == (size_t)-1) {
		free(obuf);
		return -1;
	}
	ucs2len -= olen;
	// close converter
	iconv_close(tc);

	isgsm7 = 1;
	// test for data in gsm7 default alphabet
	for (i=0; i<(ucs2len/2); i++)
	{
		for (j=0; j<128; j++)
		{
			if (*(ucs2buf+i) == gsm_to_unicode_be[j])
				break;
		}
		if (j >= 128) {
			isgsm7 = 0;
			break;
		}
		if (!isgsm7) break;
	}

	ucs2len /= 2;

	if (isgsm7) {
		if (ucs2len <= 160)
			i = 1;
		else {
			i = ucs2len / 153;
			if (ucs2len % 153) i++;
		}
	} else {
		if (ucs2len <= 70)
			i = 1;
		else {
			i = ucs2len / 67;
			if (ucs2len % 67) i++;
		}
	}

	free(obuf);
	return i;
}
//------------------------------------------------------------------------------
// end of get_parts_count()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// calc_submit_pdu()
//------------------------------------------------------------------------------
struct pdu *calc_submit_pdu(char *content, char *destination, int flash, struct address *sca, int id)
{
	int isgsm7;

	iconv_t tc;
	char *ibuf;
	size_t ilen;
	char *obuf;
	size_t olen;

	char *bldp;

	unsigned short *ucs2buf;
	int ucs2len;
	int symcnt;
	int i, j;

	int part;
	int part_count;

	struct pdu *pdu;
	struct pdu *curr;
	struct pdu *prev;

	ibuf = content;
	ilen = strlen(ibuf);
	ucs2len = olen = ilen * 2;
	if (!(ucs2buf = malloc(olen)))
		return NULL;
	obuf = (char *)ucs2buf;
	// convert from utf-8 to ucs-2be - prepare converter
	tc = iconv_open("UCS-2BE", "UTF-8");
	if (tc == (iconv_t)-1) {
		// converter not created
		free(ucs2buf);
		return NULL;
	}
	isgsm7 = iconv(tc, &ibuf, &ilen, &obuf, &olen);
	if (isgsm7 == (size_t)-1) {
		free(ucs2buf);
		return NULL;
	}
	ucs2len -= olen;
	// close converter
	iconv_close(tc);

	isgsm7 = 1;
	// test for data in gsm7 default alphabet
	for (i=0; i<(ucs2len/2); i++)
	{
		for (j=0; j<128; j++) {
			if (*(ucs2buf+i) == gsm_to_unicode_be[j])
				break;
		}
		if (j >= 128) {
			isgsm7 = 0;
			break;
		}
		if (!isgsm7) break;
	}

	symcnt = olen = ucs2len / 2;

	if (isgsm7) {
		if (olen <= 160)
			part_count = 1;
		else {
			part_count = olen / 153;
			if(olen % 153) part_count++;
		}
	} else {
		if (olen <= 70)
			part_count = 1;
		else {
			part_count = olen / 67;
			if (olen % 67) part_count++;
		}
	}

	pdu = NULL;
	prev = NULL;
	curr = NULL;
	for (part=0; part<part_count; part++)
	{
		// create pdu storage
		if (!(curr = malloc(sizeof(struct pdu)))) {
			free(ucs2buf);
			pdu_free(pdu);
			return NULL;
		}
		memset(curr, 0, sizeof(struct pdu));
		if (!pdu)
			pdu = curr;
		if (prev)
			prev->next = curr;
		prev = curr;
		// build pdu
		bldp = curr->buf;
		// sms center address
		if (sca) {
			memcpy(&curr->scaddr, sca, sizeof(struct address));
			*bldp++ = (unsigned char)((curr->scaddr.length/2) + (curr->scaddr.length%2) + 1);
			*bldp++ = (unsigned char)curr->scaddr.type.full;
			for (i=0; i<curr->scaddr.length; i++)
			{
				if (i%2)
					*bldp++ |= (((curr->scaddr.value[i] - '0') << 4) & 0xf0);
				else
					*bldp = ((curr->scaddr.value[i] - '0') & 0x0f);
			}
			if (curr->scaddr.length%2)
				*bldp++ |= 0xf0;
		}
		// first byte
		curr->fb.submit.mti = MTI_SMS_SUBMIT; // message type indicator - bit: 0,1
		curr->fb.submit.rd = 1; // reject duplicates - bit 2
		curr->fb.submit.vpf = 0; // validity period format - bit 3,4
		curr->fb.submit.sri = 1; // status report indication - bit 5
		curr->fb.submit.udhi = (part_count > 1)?1:0; // user data header indication - bit 6
		curr->fb.submit.rp = 0; // reply path - bit 7
		*bldp++ = curr->fb.full;
		// message refernce
		*bldp++ = curr->mr = 0;
		// destination address
		address_classify(destination, &curr->raddr);
		*bldp++ = (unsigned char)curr->raddr.length;
		*bldp++ = (unsigned char)curr->raddr.type.full;
		for (i=0; i<curr->raddr.length; i++)
		{
			if (i%2)
				*bldp++ |= (((curr->raddr.value[i] - '0') << 4) & 0xf0);
			else
				*bldp = ((curr->raddr.value[i] - '0') & 0x0f);
		}
		if (curr->raddr.length%2)
			*bldp++ |= 0xf0;
		// protocol id
		*bldp++ = curr->pid = 0;
		// data coding scheme
		if (isgsm7 && !flash)
			curr->dacosc = 0x00;
		else if (isgsm7 && flash)
			curr->dacosc = 0x10;
		else if (!isgsm7 && !flash)
			curr->dacosc = 0x08;
		else
			curr->dacosc = 0x18;
		*bldp++ = curr->dacosc;
		// validity period
#if 0
		if (curr->fb.submit.vpf) {
			;
		}
#endif
		// user data length
		if (isgsm7) {
			// gsm7 default alphabet
			if (part_count > 1) {
				// set user data length
				curr->udl = ((symcnt/153)?(153):(symcnt%153)) + 7;
				*bldp++ = curr->udl;
				// set user data header
				*(bldp + 0) = 5; // udhi length
				*(bldp + 1) = 0; // ie id - concatenated message
				*(bldp + 2) = 3; // ie length - concatenated message
				*(bldp + 3) = (unsigned char)((id & 0xff)?(id & 0xff):(0x5a)); // concatenated message - message reference
				*(bldp + 4) = part_count; // concatenated message - parts count
				*(bldp + 5) = part+1; // concatenated message - current part
				*(bldp + 6) = 0x00; // fill bits
				// set user data
				ibuf = (char *)(ucs2buf + part*153);
				ilen = ((symcnt/153)?(153):(symcnt%153)) * 2;
				obuf = curr->ud;
				olen = 640;
				if (from_ucs2_to_specset("UTF-8", &ibuf, (int *)&ilen, &obuf, (int *)&olen)) {
					free(ucs2buf);
					pdu_free(pdu);
					return NULL;
				}
				//
				ibuf = (char *)(ucs2buf + part*153);
				ilen = ((symcnt/153)?(153):(symcnt%153)) * 2;
				obuf = bldp;
				olen = 153;
				if (ucs2_to_gsm7(&ibuf, (int *)&ilen, 7, &obuf, (int *)&olen)) {
					free(ucs2buf);
					pdu_free(pdu);
					return NULL;
				}
				curr->len = (int)(bldp - curr->buf) + ((curr->udl * 7) / 8);
				if ((curr->udl * 7) % 8) curr->len++;
				symcnt -= 153;
			} else {
				// set user data length
				curr->udl = symcnt;
				*bldp++ = curr->udl;
				// set user data
				ibuf = (char *)ucs2buf;
				ilen = ucs2len;
				obuf = curr->ud;
				olen = 640;
				if (from_ucs2_to_specset("UTF-8", &ibuf, (int *)&ilen, &obuf, (int *)&olen)) {
					free(ucs2buf);
					pdu_free(pdu);
					return NULL;
				}
				//
				ibuf = (char *)ucs2buf;
				ilen = ucs2len;
				obuf = bldp;
				olen = 140;
				if (ucs2_to_gsm7(&ibuf, (int *)&ilen, 0, &obuf, (int *)&olen)) {
					free(ucs2buf);
					pdu_free(pdu);
					return NULL;
				}
				curr->len = (int)(bldp - curr->buf) + ((curr->udl * 7) / 8);
				if ((curr->udl * 7) % 8) curr->len++;
			}
		} else {
			// ucs2
			if (part_count > 1) {
				// set user data length
				curr->udl = ((ucs2len/134)?(134):(ucs2len%134)) + 6;
				*bldp++ = curr->udl;
				// set user data header
				*(bldp + 0) = 5; // udhi length
				*(bldp + 1) = 0; // ie id - concatenated message
				*(bldp + 2) = 3; // ie length - concatenated message
				*(bldp + 3) = (unsigned char)((id & 0xff)?(id & 0xff):(0x5a)); // concatenated message - message reference
				*(bldp + 4) = part_count; // concatenated message - parts count
				*(bldp + 5) = part+1; // concatenated message - current part
				// set user data
				ibuf = (char *)(ucs2buf + part*67);
				ilen = (ucs2len/134)?(134):(ucs2len%134);
				obuf = curr->ud;
				olen = 640;
				if (from_ucs2_to_specset("UTF-8", &ibuf, (int *)&ilen, &obuf, (int *)&olen)) {
					free(ucs2buf);
					pdu_free(pdu);
					return NULL;
				}
				//
				memcpy(bldp+6, (ucs2buf + part*67), (ucs2len/134)?(134):(ucs2len%134));
				ucs2len -= 134;
				curr->len = (int)(bldp - curr->buf) + curr->udl;
			} else {
				// set user data length
				curr->udl = ucs2len;
				*bldp++ = curr->udl;
				// set user data
				ibuf = (char *)ucs2buf;
				ilen = ucs2len;
				obuf = curr->ud;
				olen = 640;
				if (from_ucs2_to_specset("UTF-8", &ibuf, (int *)&ilen, &obuf, (int *)&olen)) {
					free(ucs2buf);
					pdu_free(pdu);
					return NULL;
				}
				//
				memcpy(bldp, ucs2buf, ucs2len);
				curr->len = (int)(bldp - curr->buf) + curr->udl;
			}
		}
		// final adjust
		if (part_count > 1) {
			curr->concat_ref = (unsigned char)((id & 0xff)?(id & 0xff):(0x5a));
			curr->concat_cnt = part_count;
			curr->concat_num = part+1;
		} else {
			curr->concat_ref = 0;
			curr->concat_cnt = 1;
			curr->concat_num = 1;
		}
		// set full length
		curr->full_len = curr->len;
		if (curr->scaddr.length)
			curr->len -= ((curr->scaddr.length/2) + (curr->scaddr.length%2) + 2);
	}

	free(ucs2buf);
	return pdu;
}
//------------------------------------------------------------------------------
// end of calc_submit_pdu()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pdu_free()
//------------------------------------------------------------------------------
void pdu_free(struct pdu *pdu)
{
	struct pdu *next;

	while (pdu)
	{
		next = pdu->next;
		free(pdu);
		pdu = next;
	}
}
//------------------------------------------------------------------------------
// end of pdu_free()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// ussd_decode()
//------------------------------------------------------------------------------
char *get_ussd_decoded(char *ussdhex, int ussdhexlen, int dcs)
{
	char *res;

	char ussdbin[256];
	int ussdbinlen;

	char *ip, *op;
	int ilen, olen;

	// get buffer for result
	if (!(res = malloc(512)))
		return NULL;
	memset(res, 0, 512);

	// convert from hex
	ip = ussdhex;
	ilen = ussdhexlen;
	op = ussdbin;
	olen = 256;
	if (str_hex_to_bin(&ip, &ilen, &op, &olen))
		return NULL;
	ussdbinlen = 256 - olen;

	// sim300 ucs2 or gsm7
	if ((dcs == 0x11) ||
		(((dcs & 0xc0) == 0x40) && ((dcs & 0x0c) == 0x08)) ||
		(((dcs & 0xf0) == 0x90) && ((dcs & 0x0c) == 0x08))) {
		// ucs2
		ip = ussdbin;
		ilen = ussdbinlen;
		op = res;
		olen = 512;
		if (from_ucs2_to_specset("UTF-8", &ip, &ilen, &op, &olen)) {
			free(res);
			return NULL;
		}
	} else {
		// gsm7
		memcpy(res, ussdbin, ussdbinlen);
	}

	return res;
}
//------------------------------------------------------------------------------
// end of get_ussd_decoded()
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of sms.c                                                               */
/******************************************************************************/
