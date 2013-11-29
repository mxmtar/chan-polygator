/******************************************************************************/
/* m10.h                                                                      */
/******************************************************************************/

#ifndef __M10_H__
#define __M10_H__

#include <sys/types.h>

#include "at.h"

//------------------------------------------------------------------------------
// at command id
enum {
	AT_M10_UNKNOWN = AT_UNKNOWN,
	// M10 V.25TER V1.04
	AT_M10_A_SLASH = AT_A_SLASH,		// A/ - Re-issues last AT command given
	AT_M10_A = AT_A,					// ATA - Answer an incoming call
	AT_M10_D = AT_D,					// ATD - Mobile originated call to dial a number
	AT_M10_D_CURMEM = AT_D_CURMEM,		// ATD><N> - Originate call to phone number in current memory
	AT_M10_DL = AT_DL,					// ATDL - Redial last telephone number used
	AT_M10_E = AT_E,					// ATE - Set command echo mode
	AT_M10_H = AT_H,					// ATH - Disconnect existing connection
	AT_M10_I = AT_I,					// ATI - Display product identification information
	AT_M10_L = AT_L,					// ATL - Set monitor speaker loudness
	AT_M10_M = AT_M,					// ATM - Set monitor speaker mode
	AT_M10_3PLUS = AT_3PLUS,			// +++ - Switch from data mode or PPP online mode to command mode
	AT_M10_O = AT_O,					// ATO - Switch from command mode to data mode
	AT_M10_P = AT_P,					// ATP - Select pulse dialling
	AT_M10_Q = AT_Q,					// ATQ - Set result code presentation mode
	AT_M10_S0 = AT_S0,					// ATS0 - Set number of rings before automatically answering the call
	AT_M10_S3 = AT_S3,					// ATS3 - Set command line termination character
	AT_M10_S4 = AT_S4,					// ATS4 - Set response formatting character
	AT_M10_S5 = AT_S5,					// ATS5 - Set command line editing character
	AT_M10_S6 = AT_S6,					// ATS6 - Set pause before blind dialling
	AT_M10_S7 = AT_S7,					// ATS7 - Set number of seconds to wait for connection completion
	AT_M10_S8 = AT_S8,					// ATS8 - Set number of seconds to wait when comma dial modifier used
	AT_M10_S10 = AT_S10,				// ATS10 - Set disconnect delay after indicating the absence of data carrier
	AT_M10_T = AT_T,					// ATT - Select tone dialling
	AT_M10_V = AT_V,					// ATV - Set result code format mode
	AT_M10_X = AT_X,					// ATX - Set connect result code format and monitor call progress
	AT_M10_Z = AT_Z,					// ATZ - Set all current parameters to user defined profile
	AT_M10_andC = AT_andC,				// AT&C - Set DCD function mode
	AT_M10_andD = AT_andD,				// AT&D - Set DTR function mode
	AT_M10_andF = AT_andF,				// AT&F - Set all current parameters to manufacturer defaults
	AT_M10_andV = AT_andV,				// AT&V - Display current configuration
	AT_M10_andW = AT_andW,				// AT&W - Store current parameter to user defined profile
	AT_M10_DR = AT_DR,					// AT+DR - V.42bis data compression reporting control
	AT_M10_DS = AT_DS,					// AT+DS - V.42bis data compression control
	AT_M10_GCAP = AT_GCAP,				// AT+GCAP - Request complete TA capabilities list
	AT_M10_GMI = AT_GMI,				// AT+GMI - Request manufacturer identification
	AT_M10_GMM = AT_GMM,				// AT+GMM - Request TA model identification
	AT_M10_GMR = AT_GMR,				// AT+GMR - Request TA revision indentification of software release
	AT_M10_GOI = AT_GOI,				// AT+GOI - Request global object identification
	AT_M10_GSN = AT_GSN,				// AT+GSN - Request ta serial number identification (IMEI)
	AT_M10_ICF = AT_ICF,				// AT+ICF - Set TE-TA control character framing
	AT_M10_IFC = AT_IFC,				// AT+IFC - Set TE-TA local data flow control
	AT_M10_ILRR = AT_ILRR,				// AT+ILRR - Set TE-TA local rate reporting mode
	AT_M10_IPR = AT_IPR,				// AT+IPR - Set TE-TA fixed local rate

	// M10 GSM07.07 V1.04
	AT_M10_CACM = AT_CACM,		// AT+CACM - Accumulated call meter(ACM) reset or query
	AT_M10_CAMM = AT_CAMM,		// AT+CAMM - Accumulated call meter maximum(ACMMAX) set or query
	AT_M10_CAOC = AT_CAOC,		// AT+CAOC - Advice of charge
	AT_M10_CBST = AT_CBST,		// AT+CBST - Select bearer service type
	AT_M10_CCFC = AT_CCFC,		// AT+CCFC - Call forwarding number and conditions control
	AT_M10_CCUG = AT_CCUG,		// AT+CCUG - Closed user group control
	AT_M10_CCWA = AT_CCWA,		// AT+CCWA - Call waiting control
	AT_M10_CEER = AT_CEER,		// AT+CEER - Extended error report
	AT_M10_CGMI = AT_CGMI,		// AT+CGMI - Request manufacturer identification
	AT_M10_CGMM = AT_CGMM,		// AT+CGMM - Request model identification
	AT_M10_CGMR = AT_CGMR,		// AT+CGMR - Request TA revision identification of software release
	AT_M10_CGSN = AT_CGSN,		// AT+CGSN - Request product serial number identification (identical with +GSN)
	AT_M10_CSCS = AT_CSCS,		// AT+CSCS - Select TE character set
	AT_M10_CSTA = AT_CSTA,		// AT+CSTA - Select type of address
	AT_M10_CHLD = AT_CHLD,		// AT+CHLD - Call hold and multiparty
	AT_M10_CIMI = AT_CIMI,		// AT+CIMI - Request international mobile subscriber identity
	AT_M10_CKPD = AT_CKPD,		// AT+CKPD - Keypad control
	AT_M10_CLCC = AT_CLCC,		// AT+CLCC - List current calls of ME
	AT_M10_CLCK = AT_CLCK,		// AT+CLCK - Facility lock
	AT_M10_CLIP = AT_CLIP,		// AT+CLIP - Calling line identification presentation
	AT_M10_CLIR = AT_CLIR,		// AT+CLIR - Calling line identification restriction
	AT_M10_CMEE = AT_CMEE,		// AT+CMEE - Report mobile equipment error
	AT_M10_COLP = AT_COLP,		// AT+COLP - Connected line identification presentation
	AT_M10_COPS = AT_COPS,		// AT+COPS - Operator selection
	AT_M10_CPAS = AT_CPAS,		// AT+CPAS - Mobile equipment activity status
	AT_M10_CPBF = AT_CPBF,		// AT+CPBF - Find phonebook entries
	AT_M10_CPBR = AT_CPBR,		// AT+CPBR - Read current phonebook entries
	AT_M10_CPBS = AT_CPBS,		// AT+CPBS - Select phonebook memory storage
	AT_M10_CPBW = AT_CPBW,		// AT+CPBW - Write phonebook entry
	AT_M10_CPIN = AT_CPIN,		// AT+CPIN - Enter PIN
	AT_M10_CPWD = AT_CPWD,		// AT+CPWD - Change password
	AT_M10_CR = AT_CR,			// AT+CR - Service reporting control
	AT_M10_CRC = AT_CRC,		// AT+CRC - Set cellular result codes for incoming call indication
	AT_M10_CREG = AT_CREG,		// AT+CREG - Network registration
	AT_M10_CRLP = AT_CRLP,		// AT+CRLP - Select radio link protocol parameter
	AT_M10_CRSM = AT_CRSM,		// AT+CRSM - Restricted SIM access
	AT_M10_CSQ = AT_CSQ,		// AT+CSQ - Signal quality report
	AT_M10_FCLASS = AT_FCLASS,	// AT+FCLASS - Fax: select, read or test service class
	AT_M10_VTD = AT_VTD,		// AT+VTD - Tone duration
	AT_M10_VTS = AT_VTS,		// AT+VTS - DTMF and tone generation
	AT_M10_CMUX = AT_CMUX,		// AT+CMUX - Multiplexer control
	AT_M10_CNUM = AT_CNUM,		// AT+CNUM - Subscriber number
	AT_M10_CPOL = AT_CPOL,		// AT+CPOL - Preferred operator list
	AT_M10_COPN = AT_COPN,		// AT+COPN - Read operator names
	AT_M10_CFUN = AT_CFUN,		// AT+CFUN - Set phone functionality
	AT_M10_CCLK = AT_CCLK,		// AT+CCLK - Clock
	AT_M10_CSIM = AT_CSIM,		// AT+CSIM - Generic SIM access
	AT_M10_CALM = AT_CALM,		// AT+CALM - Alert sound mode
	AT_M10_CRSL = AT_CRSL,		// AT+CRSL - Ringer sound level
	AT_M10_CLVL = AT_CLVL,		// AT+CLVL - Loud speaker volume level
	AT_M10_CMUT = AT_CMUT,		// AT+CMUT - Mute control
	AT_M10_CPUC = AT_CPUC,		// AT+CPUC - Price per unit currency table
	AT_M10_CCWE = AT_CCWE,		// AT+CCWE - Call meter maximum event
	AT_M10_CBC = AT_CBC,		// AT+CBC - Battery charge
	AT_M10_CUSD = AT_CUSD,		// AT+CUSD - Unstructured supplementary service data
	AT_M10_CSSN = AT_CSSN,		// AT+CSSN - Supplementary services notification
	AT_M10_CSNS = AT_CSNS,		// AT+CSNS - Single numbering scheme (M10)
	AT_M10_CMOD = AT_CMOD,		// AT+CMOD - Configure alternating mode calls (M10)

	// M10 GSM07.05 V1.04
	AT_M10_CMGD = AT_CMGD,		// AT+CMGD - Delete SMS message
	AT_M10_CMGF = AT_CMGF,		// AT+CMGF - Select SMS message format
	AT_M10_CMGL = AT_CMGL,		// AT+CMGL - List SMS messages from preferred store
	AT_M10_CMGR = AT_CMGR,		// AT+CMGR - Read SMS message
	AT_M10_CMGS = AT_CMGS,		// AT+CMGS - Send SMS message
	AT_M10_CMGW = AT_CMGW,		// AT+CMGW - Write SMS message to memory
	AT_M10_CMSS = AT_CMSS,		// AT+CMSS - Send SMS message from storage
	AT_M10_CMGC = AT_CMGC,		// AT+CMGC - Send sms command
	AT_M10_CNMI = AT_CNMI,		// AT+CNMI - New SMS message indications
	AT_M10_CPMS = AT_CPMS,		// AT+CPMS - Preferred SMS message storage
	AT_M10_CRES = AT_CRES,		// AT+CRES - Restore SMS settings
	AT_M10_CSAS = AT_CSAS,		// AT+CSAS - Save SMS settings
	AT_M10_CSCA = AT_CSCA,		// AT+CSCA - SMS service center address
	AT_M10_CSCB = AT_CSCB,		// AT+CSCB - Select cell broadcast SMS messages
	AT_M10_CSDH = AT_CSDH,		// AT+CSDH - Show SMS text mode parameters
	AT_M10_CSMP = AT_CSMP,		// AT+CSMP - Set SMS text mode parameters
	AT_M10_CSMS = AT_CSMS,		// AT+CSMS - Select message service

	// M10 Quectel V1.04
	AT_M10_QECHO,				// AT+QECHO - Echo cancellation control
	AT_M10_QSIDET,				// AT+QSIDET - Change the side tone gain level
	AT_M10_QPOWD,				// AT+QPOWD - Power off
	AT_M10_QTRPIN,				// AT+QTRPIN - Times remain to input SIM PIN/PUK
	AT_M10_QMIC,				// AT+QMIC - Change the microphone gain level
	AT_M10_QALARM,				// AT+QALARM - Set alarm
	AT_M10_QADC,				// AT+QADC - Read ADC
	AT_M10_QRSTCB,				// AT+QRSTCB - Reset cell broadcast
	AT_M10_QINDRI,				// AT+QINDRI - Indicate RI when using URC
	AT_M10_QEXTHS,				// AT+QEXTHS - External headset jack control
	AT_M10_QHSBTN,				// AT+QHSBTN - Headset button status reporting
	AT_M10_QSIMSTAT,			// AT+QSIMSTAT - SIM inserted status reporting
	AT_M10_QLDTMF,				// AT+QLDTMF - Generate local DTMF tone
	AT_M10_QCGTIND,				// AT+QCGTIND - Circuit switched call or GPRS PDP context termination indication
	AT_M10_QSPN,				// AT+QSPN - Get service provider name from SIM
	AT_M10_QBAND,				// AT+QBAND - Get and set mobile operation band
	AT_M10_QAUDCH,				// AT+QAUDCH - Swap the audio channels
	AT_M10_QSCLK,				// AT+QSCLK - Configure slow clock
	AT_M10_QENG,				// AT+QENG - Report cell description in engineering mode
	AT_M10_QCLASS0,				// AT+QCLASS0 - Store Class 0 SMS to SIM when received Class 0 SMS
	AT_M10_QCCID,				// AT+QCCID - Show ICCID
	AT_M10_QTEMP,				// AT+QTEMP - Set critical temperature operating mode or query temperature
	AT_M10_QSIMDET,				// AT+QSIMDET - Switch on or off detecting SIM card
	AT_M10_QMGDA,				// AT+QMGDA - Delete all SMS
	AT_M10_QLTONE,				// AT+QLTONE - Generate local specific tone
	AT_M10_QCLIP,				// AT+QCLIP - Connected line identification presentation without alpha string
	AT_M10_QGID,				// AT+QGID - Get SIM card group identifier
	AT_M10_QMOSTAT,				// AT+QMOSTAT - Show state of mobile originated call
	AT_M10_QGPCLASS,			// AT+QGPCLASS - Change GPRS multi-slot class
	AT_M10_QMGHEX,				// AT+QMGHEX - Enable to send non-ASCII character SMS
	AT_M10_QAUDLOOP,			// AT+QAUDLOOP - Audio channel loop back test
	AT_M10_QSMSCODE,			// AT+QSMSCODE - Configure SMS code mode
	AT_M10_QIURC,				// AT+QIURC - Enable or disable initial URC presentation
	AT_M10_QCSPWD,				// AT+QCSPWD - Change PS super password
	AT_M10_QEXTUNSOL,			// AT+QEXTUNSOL - Enable/disable proprietary unsolicited indication
	AT_M10_QSFR,				// AT+QSFR - Prefernce speech coding
	AT_M10_QSPCH,				// AT+QSPCH - Speech channel type report
	AT_M10_QSCANF,				// AT+QSCANF - Scan power of GSM frequency
	AT_M10_QLOCKF,				// AT+QLOCKF - Lock GSM frequency
	AT_M10_QGPIO,				// AT+QGPIO - Configure GPIO pin
	AT_M10_QINISTAT,			// AT+QINISTAT - Query state of initialization
	AT_M10_QFGR,				// AT+QFGR - Read customer file
	AT_M10_QFGW,				// AT+QFGW - Write customer file
	AT_M10_QFGL,				// AT+QFGL - List customer files
	AT_M10_QFGD,				// AT+QFGD - Delete customer file
	AT_M10_QFGM,				// AT+QFGM - Query free space for customer files
	AT_M10_QSRT,				// AT+QSRT - Select ring tone
	AT_M10_QNSTATUS,			// AT+QNSTATUS - Query GSM network status
	AT_M10_QECHOEX,				// AT+QECHOEX - Extended echo cancellation control
	AT_M10_EGPAU,				// AT+EGPAU - PPP authentication
	AT_M10_QNITZ,				// AT+QNITZ - Network time synchronization

	//
	AT_M10_EGMR,				// AT+EGMR - Set/Inquiry product serial number identification

	//
	AT_M10_MAXNUM,
};
//------------------------------------------------------------------------------
// m10 AT command parameters
// read
// qmic read
struct at_m10_qmic_read{
	// integer (mandatory)
	int normal_mic;	// Normal microphone gain level
	// integer (mandatory)
	int headset_mic;	// Headset microphone gain level
	// integer (mandatory)
	int loudspeaker_mic;	// Louspeaker microphone gain level
	};
// qsimstat read
struct at_m10_qsimstat_read{
	// integer (mandatory)
	int n;	// Unsolicited event code status
	// integer (mandatory)
	int sim_inserted;	// SIM inserted status
	};
//------------------------------------------------------------------------------

extern const struct at_command m10_at_com_list[];
extern size_t m10_at_com_list_length();

extern int at_m10_qmic_read_parse(const char *fld, int fld_len, struct at_m10_qmic_read *qmic);
extern int at_m10_qsimstat_read_parse(const char *fld, int fld_len, struct at_m10_qsimstat_read *qsimstat);
//------------------------------------------------------------------------------
#endif //__M10_H__

/******************************************************************************/
/* end of m10.h                                                               */
/******************************************************************************/
