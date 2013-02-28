//******************************************************************************
// rtp.h
//******************************************************************************
#ifndef __RTP_H__
#define __RTP_H__
//------------------------------------------------------------------------------
#include <linux/types.h>
//------------------------------------------------------------------------------
#define RTP_VERSION 2
//------------------------------------------------------------------------------
enum {
	RTP_PT_PCMU		= 0,
	RTP_PT_GSM		= 3,
	RTP_PT_G723		= 4,
	RTP_PT_DVI4_8	= 5,
	RTP_PT_DVI4_16	= 6,
	RTP_PT_LPC		= 7,
	RTP_PT_PCMA		= 8,
	RTP_PT_G722		= 9,
	RTP_PT_L16_2	= 10,
	RTP_PT_L16_1	= 11,
	RTP_PT_QCELP	= 12,
	RTP_PT_CN		= 13,
	RTP_PT_MPA		= 14,
	RTP_PT_G728		= 15,
	RTP_PT_DVI4_11	= 16,
	RTP_PT_DVI4_22	= 17,
	RTP_PT_G729		= 18,
	RTP_PT_CELB		= 25,
	RTP_PT_JPEG		= 26,
	RTP_PT_NV		= 28,
	RTP_PT_H261		= 31,
	RTP_PT_MPV		= 32,
	RTP_PT_MP2T		= 33,
	RTP_PT_H263		= 34,
	RTP_PT_DYNAMIC	= 97,
	RTP_PT_MAXNUM	= 127,
};
//------------------------------------------------------------------------------
#define IS_RTP_PT_DYNAMIC(pt) \
	(pt >= RTP_PT_DYNAMIC) ? (1) : (0)
//------------------------------------------------------------------------------
// RTP packet header
struct rtp_hdr {
	// first octet
	unsigned int csrc_count:4;
	unsigned int extension:1;
	unsigned int padding:1;
	unsigned int version:2;
	// second octet
	unsigned int payload_type:7;
	unsigned int marker:1;

	unsigned int sequence_number:16;

	unsigned int timestamp;
	unsigned int ssrc;
};

struct rfc2833_event_payload {
	// first octet
	unsigned int event:8;
	// second octet
	unsigned int volume:6;
	unsigned int reserved:1;
	unsigned int end:1;

	unsigned int duration:16;
};

enum {
	RTP_EVENT_DTMF_0 = 0,
	RTP_EVENT_DTMF_1 = 1,
	RTP_EVENT_DTMF_2 = 2,
	RTP_EVENT_DTMF_3 = 3,
	RTP_EVENT_DTMF_4 = 4,
	RTP_EVENT_DTMF_5 = 5,
	RTP_EVENT_DTMF_6 = 6,
	RTP_EVENT_DTMF_7 = 7,
	RTP_EVENT_DTMF_8 = 8,
	RTP_EVENT_DTMF_9 = 9,
	RTP_EVENT_DTMF_ASTERISK = 10,
	RTP_EVENT_DTMF_HASH = 11,
	RTP_EVENT_DTMF_A = 12,
	RTP_EVENT_DTMF_B = 13,
	RTP_EVENT_DTMF_C = 14,
	RTP_EVENT_DTMF_D = 15,
};

extern char rtp_event_dtmf_to_char(u_int8_t event);

#define rtp_is_event_dtmf(_evt) \
({ \
	int __res; \
	if ((_evt >= 0) && (_evt <= 15)) { \
		__res = 1; \
	} else { \
		__res = 0; \
	} \
	__res; \
})

#endif //__RTP_H__

//******************************************************************************
// end of rtp.h
//******************************************************************************
