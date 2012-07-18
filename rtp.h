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
	// third .........
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
	// third ..............
	unsigned int duration:16;
};
//------------------------------------------------------------------------------
#endif //__RTP_H__
//******************************************************************************
// end of rtp.h
//******************************************************************************
