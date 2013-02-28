/******************************************************************************/
/* rtp.c                                                                      */
/******************************************************************************/

#include <sys/types.h>

#include "rtp.h"

//------------------------------------------------------------------------------
// rtp_event_dtmf_to_char()
//------------------------------------------------------------------------------
char rtp_event_dtmf_to_char(u_int8_t event)
{
	switch (event) {
		case RTP_EVENT_DTMF_0: return '0';
		case RTP_EVENT_DTMF_1: return '1';
		case RTP_EVENT_DTMF_2: return '2';
		case RTP_EVENT_DTMF_3: return '3';
		case RTP_EVENT_DTMF_4: return '4';
		case RTP_EVENT_DTMF_5: return '5';
		case RTP_EVENT_DTMF_6: return '6';
		case RTP_EVENT_DTMF_7: return '7';
		case RTP_EVENT_DTMF_8: return '8';
		case RTP_EVENT_DTMF_9: return '9';
		case RTP_EVENT_DTMF_ASTERISK: return '*';
		case RTP_EVENT_DTMF_HASH: return '#';
		case RTP_EVENT_DTMF_A: return 'A';
		case RTP_EVENT_DTMF_B: return 'B';
		case RTP_EVENT_DTMF_C: return 'C';
		case RTP_EVENT_DTMF_D: return 'D';
		default: return '?';
	}
}
//------------------------------------------------------------------------------
// end of rtp_event_dtmf_to_char()
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of rtp.c                                                               */
/******************************************************************************/
