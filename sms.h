/******************************************************************************/
/* sms.h                                                                      */
/******************************************************************************/

#ifndef __SMS_H__
#define __SMS_H__

#include <sys/types.h>

#include "address.h"
//------------------------------------------------------------------------------
#define MAX_PDU_BIN_SIZE 192

//------------------------------------------------------------------------------
// PDU
// MTI
#define MTI_SMS_DELIVER 0
#define MTI_SMS_SUBMIT 1
#define MTI_SMS_STATUS_REPORT 2
// DCS
// group
#define DCS_GROUP_GENERAL 0
#define DCS_GROUP_AUTODEL 1
#define DCS_GROUP_MWI 2
#define DCS_GROUP_DCMC 3
// charset
#define DCS_CS_GSM7 0
#define DCS_CS_8BIT 1
#define DCS_CS_UCS2 2
#define DCS_CS_MASK 3
// mwi
#define DCS_MWI_VOICEMAIL 0
#define DCS_MWI_FAX 1
#define DCS_MWI_EMAIL 2
#define DCS_MWI_OTHER 3
//------------------------------------------------------------------------------

union pdu_fb {
	struct {
		unsigned char mti:2;		// message type indicator - bit: 0,1
		unsigned char nu0:4;		// bit: 2,3,4,5
		unsigned char udhi:1;	// user data header indication - bit 6
		unsigned char nu1:1;		// bit 7
	} __attribute__((packed)) general;
	struct {
		unsigned char mti:2;		// message type indicator - bit: 0,1
		unsigned char mms:1;		// more message to send - bit 2
		unsigned char lp:1;		// loop prevention - bit 3
		unsigned char nu0:1;		// bit 4
		unsigned char sri:1;		// status report indication - bit 5
		unsigned char udhi:1;	// user data header indication - bit 6
		unsigned char rp:1;		// reply path - bit 7
	} __attribute__((packed)) deliver;
	struct {
		unsigned char mti:2;		// message type indicator - bit: 0,1
		unsigned char rd:1;		// reject duplicates - bit 2
		unsigned char vpf:2;		// validity period format - bit 3,4
		unsigned char sri:1;		// status report indication - bit 5
		unsigned char udhi:1;	// user data header indication - bit 6
		unsigned char rp:1;		// reply path - bit 7
	} __attribute__((packed)) submit;
	struct {
		unsigned char mti:2;		// message type indicator - bit: 0,1
		unsigned char mms:1;		// more message to send - bit 2
		unsigned char lp:1;		// loop prevention - bit 3
		unsigned char nu0:1;		// bit 4
		unsigned char srq:1;		// status report indication - bit 5
		unsigned char udhi:1;	// user data header indication - bit 6
		unsigned char rp:1;		// reply path - bit 7
	} __attribute__((packed)) status_report;
	unsigned char full;
} __attribute__((packed));

union param_ind {
	struct {
		unsigned char pid:1;
		unsigned char dcs:1;
		unsigned char udl:1;
		unsigned char reserved:5;
	} __attribute__((packed)) bits;
	unsigned char full;
} __attribute__((packed));

struct dcs {
	unsigned int group:2; // general, autodel, mwi, dcmc
	unsigned int charset:2; // gsm7, 8bit, ucs2
	unsigned int isclass:1; // 0-no class, 1-within class
	unsigned int classid:2; // class 0,1,2,3
	unsigned int compres:1; // 0-no compression, 1-compression
	unsigned int mwistore:1;
	unsigned int mwiind:1;
	unsigned int mwitype:2; // voice,fax,e-mail,user
};

struct pdu {
	// length - without SCA
	int len;
	// length
	int full_len;
	// buf - PDU binary presentation
	char buf[MAX_PDU_BIN_SIZE];
	// sca
	struct address scaddr;
	// first byte
	union pdu_fb fb;
	// remote address
	struct address raddr;	// sms-deliver - originating address
							// sms-submit - destination address
							// sms-status-report - recipient address
	// message reference
	unsigned char mr; // sms-submit, sms-status-report
	// protocol id
	unsigned char pid;
	// status
	unsigned char status; // status-report
	// parameter indication
	union param_ind paramind; // status-report
	// sent time
	time_t sent;	// sms-deliver - scts
				// sms-submit - local
				// sms-status-report - scts
	// delivered time;
	time_t delivered;	// sms-deliver - local
						// sms-submit - from sms-status-report
						// sms-status-report - dt
	// validity period
	time_t validity;
	// data coding scheme
	unsigned char dacosc;
	struct dcs dcs;
	// user data length
	int udl;
	// user data
	char ud[640];
	// user data header
	// concateneted message
	unsigned char concat_ref;
	unsigned char concat_cnt;
	unsigned char concat_num;
	// single-linked list pointer
	struct pdu *next;
};
//------------------------------------------------------------------------------

// prototypes
struct pdu *pdu_parser(const char *pduhex, int pduhexlen, int pdulen, time_t ltime, int *err);
void pdu_free(struct pdu *pdu);
int dcs_parser(unsigned char inp, struct dcs *dcs);
int gsm7_to_ucs2(char **instr, size_t *inlen, int start, char **outstr, size_t *outlen);
struct pdu *calc_submit_pdu(char *content, char *destination, int flash, struct address *sca, int id);

char *get_ussd_decoded(char *ussdhex, int ussdhexlen, int dcs);

#endif //__SMS_H__

/******************************************************************************/
/* end of sms.h                                                               */
/******************************************************************************/
