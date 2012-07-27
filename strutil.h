/******************************************************************************/
/* strutil.h                                                                  */
/******************************************************************************/

#ifndef __STRUTIL_H__
#define __STRUTIL_H__

#include <sys/types.h>

//------------------------------------------------------------------------------
// symbol
#define SP 0x20
#define TAB 0x09

//------------------------------------------------------------------------------
#if 0
// SMS
struct sms_dcs{
	unsigned char res:4;
	unsigned char cbg:4;
	} __attribute__((packed));
// SMS Coding Group Bits
#define SMS_GENERAL_DCS_UNCOMP_NOCL	0x0
#define SMS_GENERAL_DCS_UNCOMP_WTCL	0x1
#define SMS_GENERAL_DCS_COMPRS_NOCL	0x2
#define SMS_GENERAL_DCS_COMPRS_WTCL	0x3

#define SMS_AUTODEL_GRP_UNCOMP_NOCL	0x4
#define SMS_AUTODEL_GRP_UNCOMP_WTCL	0x5
#define SMS_AUTODEL_GRP_COMPRS_NOCL	0x6
#define SMS_AUTODEL_GRP_COMPRS_WTCL	0x7

#define SMS_CGB_RESERVED0			0x8
#define SMS_CGB_RESERVED1			0x9
#define SMS_CGB_RESERVED2			0xA
#define SMS_CGB_RESERVED3			0xB

#define SMS_MSGWAIT_DISCARD			0xC
#define SMS_MSGWAIT_STORE_GSM7		0xD
#define SMS_MSGWAIT_STORE_UCS2		0xE
#define SMS_CGB_OTHER				0xF

// CBS
struct cbs_dcs{
	unsigned char res:4;
	unsigned char cbg:4;
	} __attribute__((packed));
// CBS Coding Group Bits
#define CBS_CGB_GSM7_DFLT_LANGFIX0	0x0
#define CBS_CGB_GSM7_DFLT_LANGSPEC	0x1
#define CBS_CGB_GSM7_DFLT_LANGFIX1	0x2
#define CBS_CGB_GSM7_DFLT_LANGFIX2	0x3
#define CBS_CGB_UNCOMPRESSED_NOCLASS	0x4
#define CBS_CGB_UNCOMPRESSED_CLASS	0x5
#define CBS_CGB_COMPRESSED_NOCLASS	0x6
#define CBS_CGB_COMPRESSED_CLASS		0x7
#define CBS_CGB_RESERVED0			0x8
#define CBS_CGB_USER_DATA_HEADER		0x9
#define CBS_CBS_CGB_RESERVED1		0xA
#define CBS_CGB_RESERVED2			0xB
#define CBS_CGB_RESERVED3			0xC
#define CBS_CGB_L1_PROTOCOL			0xD
#define CBS_CGB_WAP_FORUM			0xE
#define CBS_CGB_OTHER				0xF

// LANGFIX0
#define CBS_LANGFIX0_GERMAN			0x0
#define CBS_LANGFIX0_ENGLISH			0x1
#define CBS_LANGFIX0_ITALIAN			0x2
#define CBS_LANGFIX0_FRENCH			0x3
#define CBS_LANGFIX0_SPANICH			0x4
#define CBS_LANGFIX0_DUTCH			0x5
#define CBS_LANGFIX0_SWEDISH			0x6
#define CBS_LANGFIX0_DANISH			0x7
#define CBS_LANGFIX0_PORTUGUESE		0x8
#define CBS_LANGFIX0_FINNISH			0x9
#define CBS_LANGFIX0_NORWEGIAN		0xA
#define CBS_LANGFIX0_GREEK			0xB
#define CBS_LANGFIX0_TURKISH			0xC
#define CBS_LANGFIX0_HUNGARIAN		0xD
#define CBS_LANGFIX0_POLISH			0xE
#define CBS_LANGFIX0_UNSPECIFIED		0xF

// LANGFIX1
#define CBS_LANGFIX0_CZECH			0x0
#define CBS_LANGFIX0_HEBREW			0x1
#define CBS_LANGFIX0_ARABIC			0x2
#define CBS_LANGFIX0_RUSSIAN			0x3
#define CBS_LANGFIX0_ICELANDIC		0x4

// character set
#define CS_GSM7_DEFAULT			0
#define CS_8BIT_DATA				1
#define CS_UCS2					2
#define CS_RESERVED				3
//------------------------------------------------------------------------------

#define MTI_SMS_DELIVER			0
#define MTI_SMS_SUBMIT			1
#define MTI_SMS_STATUS_REPORT	2

//
union sms_pdu_fb{
	struct{
		unsigned char mti:2;		// message type indicator
		unsigned char reserved:6;
		} __attribute__((packed)) general;
	struct{
		unsigned char mti:2;		// message type indicator - bit: 0,1
		unsigned char mms:1;		// more message to send - bit 2
		unsigned char lp:1;		// loop prevention - bit 3
		unsigned char notused:1;	// bit 4
		unsigned char sri:1;		// status report indication - bit 5
		unsigned char udhi:1;	// user data header indication - bit 6
		unsigned char rp:1;		// reply path - bit 7
		} __attribute__((packed)) deliver;
	struct{
		unsigned char mti:2;		// message type indicator - bit: 0,1
		unsigned char rd:1;		// reject duplicates - bit 2
		unsigned char vpf:2;		// validity period format - bit 3,4
		unsigned char sri:1;		// status report indication - bit 5
		unsigned char udhi:1;	// user data header indication - bit 6
		unsigned char rp:1;		// reply path - bit 7
		} __attribute__((packed)) submit;
	struct{
		unsigned char mti:2;		// message type indicator - bit: 0,1
		unsigned char mms:1;		// more message to send - bit 2
		unsigned char lp:1;		// loop prevention - bit 3
		unsigned char notused:1;	// bit 4
		unsigned char srq:1;		// status report indication - bit 5
		unsigned char udhi:1;	// user data header indication - bit 6
		unsigned char rp:1;		// reply path - bit 7
		} __attribute__((packed)) status_report;
	unsigned char full;
	} __attribute__((packed));


struct sms{
	// name
	char name[256];
	// owner
	char owner[256];
	// channel
	char channel[32];
	// SMS Center Address
	struct address smsca;
	// Remote Address
	struct address remaddr;
	//  sent time
	time_t sent;
	// receiving time
	time_t delivered;
	// sms type
	int type;
	// first byte
	union sms_pdu_fb fb;
	// message refernce
	unsigned char mr;
	// protocol identifier
	unsigned char pid;
	// data coding scheme
	unsigned char dcs;
	// concateneted message
	unsigned char cmsgref;
	unsigned char cmsgall;
	unsigned char cmsgcur;
	// symbol count
	int symcnt;
	// sms char buffer ucs2 big-endian
	int ucs2_datalen;
	char ucs2_databuf[1024];
	// sms char buffer utf8
	int utf8_datalen;
	char utf8_databuf[1024];

	int atpdulen;
	int hexpdulen;
	char hexpdubuf[1024];
	};
#endif
//------------------------------------------------------------------------------
// extern
extern const char char_cr;
extern const char char_lf;
extern const char char_ctrlz;
// prototypes
extern int is_str_digit(const char *buf);
extern int is_str_xdigit(const char *buf);
extern int is_str_printable(const char *buf);
extern void str_digit_to_bcd(const char *instr, int inlen, char *out);
extern int str_bin_to_hex(char **instr, int *inlen, char **outstr, int *outlen);
extern int str_hex_to_bin(char **instr, int *inlen, char **outstr, int *outlen);
extern int from_ucs2_to_specset(char *specset, char **instr, int *inlen, char **outstr, int *outlen);

#endif //__STRUTIL_H__

/******************************************************************************/
/* end of strutil.h                                                           */
/******************************************************************************/
