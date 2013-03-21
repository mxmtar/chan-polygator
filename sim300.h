/******************************************************************************/
/* sim300.h                                                                   */
/******************************************************************************/

#ifndef __SIM300_H__
#define __SIM300_H__

#include <sys/types.h>

#include "at.h"

//------------------------------------------------------------------------------
// at command id
enum {
	AT_SIM300_UNKNOWN = AT_UNKNOWN,
	// SIM300 V.25TER V1.06
	AT_SIM300_A_SLASH = AT_A_SLASH,		// A/ - Re-issues last AT command given
	AT_SIM300_A = AT_A,					// ATA - Answer an incoming call
	AT_SIM300_D = AT_D,					// ATD - Mobile originated call to dial a number
	AT_SIM300_D_CURMEM = AT_D_CURMEM,	// ATD><N> - Originate call to phone number in current memory
	AT_SIM300_D_PHBOOK = AT_D_PHBOOK,	// ATD><STR> - Originate call to phone number in memory which corresponds to field <STR>
	AT_SIM300_DL = AT_DL,				// ATDL - Redial last telephone number used
	AT_SIM300_E = AT_E,					// ATE - Set command echo mode
	AT_SIM300_H = AT_H,					// ATH - Disconnect existing connection
	AT_SIM300_I = AT_I,					// ATI - Display product identification information
	AT_SIM300_L = AT_L,					// ATL - Set monitor speaker loudness
	AT_SIM300_M = AT_M,					// ATM - Set monitor speaker mode
	AT_SIM300_3PLUS = AT_3PLUS,			// +++ - Switch from data mode or PPP online mode to command mode
	AT_SIM300_O = AT_O,					// ATO - Switch from command mode to data mode
	AT_SIM300_P = AT_P,					// ATP - Select pulse dialling
	AT_SIM300_Q = AT_Q,					// ATQ - Set result code presentation mode
	AT_SIM300_S0 = AT_S0,				// ATS0 - Set number of rings before automatically answering the call
	AT_SIM300_S3 = AT_S3,				// ATS3 - Set command line termination character
	AT_SIM300_S4 = AT_S4,				// ATS4 - Set response formatting character
	AT_SIM300_S5 = AT_S5,				// ATS5 - Set command line editing character
	AT_SIM300_S6 = AT_S6,				// ATS6 - Set pause before blind dialling
	AT_SIM300_S7 = AT_S7,				// ATS7 - Set number of seconds to wait for connection completion
	AT_SIM300_S8 = AT_S8,				// ATS8 - Set number of seconds to wait when comma dial modifier used
	AT_SIM300_S10 = AT_S10,				// ATS10 - Set disconnect delay after indicating the absence of data carrier
	AT_SIM300_T = AT_T,					// ATT - Select tone dialling
	AT_SIM300_V = AT_V,					// ATV - Set result code format mode
	AT_SIM300_X = AT_X,					// ATX - Set connect result code format and monitor call progress
	AT_SIM300_Z = AT_Z,					// ATZ - Set all current parameters to user defined profile
	AT_SIM300_andC = AT_andC,			// AT&C - Set DCD function mode
	AT_SIM300_andD = AT_andD,			// AT&D - Set DTR function mode
	AT_SIM300_andF = AT_andF,			// AT&F - Set all current parameters to manufacturer defaults
	AT_SIM300_andV = AT_andV,			// AT&V - Display current configuration
	AT_SIM300_andW = AT_andW,			// AT&W - Store current parameter to user defined profile
	AT_SIM300_DR = AT_DR,				// AT+DR - V.42bis data compression reporting control
	AT_SIM300_DS = AT_DS,				// AT+DS - V.42bis data compression control
	AT_SIM300_GCAP = AT_GCAP,			// AT+GCAP - Request complete TA capabilities list
	AT_SIM300_GMI = AT_GMI,				// AT+GMI - Request manufacturer identification
	AT_SIM300_GMM = AT_GMM,				// AT+GMM - Request TA model identification
	AT_SIM300_GMR = AT_GMR,				// AT+GMR - Request TA revision indentification of software release
	AT_SIM300_GOI = AT_GOI,				// AT+GOI - Request global object identification
	AT_SIM300_GSN = AT_GSN,				// AT+GSN - Request ta serial number identification (IMEI)
	AT_SIM300_ICF = AT_ICF,				// AT+ICF - Set TE-TA control character framing
	AT_SIM300_IFC = AT_IFC,				// AT+IFC - Set TE-TA local data flow control
	AT_SIM300_ILRR = AT_ILRR,			// AT+ILRR - Set TE-TA local rate reporting mode
	AT_SIM300_IPR = AT_IPR,				// AT+IPR - Set TE-TA fixed local rate

	// SIM300 GSM07.07 V1.06
	AT_SIM300_CACM = AT_CACM,		// AT+CACM - Accumulated call meter(ACM) reset or query
	AT_SIM300_CAMM = AT_CAMM,		// AT+CAMM - Accumulated call meter maximum(ACMMAX) set or query
	AT_SIM300_CAOC = AT_CAOC,		// AT+CAOC - Advice of charge
	AT_SIM300_CBST = AT_CBST,		// AT+CBST - Select bearer service type
	AT_SIM300_CCFC = AT_CCFC,		// AT+CCFC - Call forwarding number and conditions control
	AT_SIM300_CCUG = AT_CCUG,		// AT+CCUG - Closed user group control
	AT_SIM300_CCWA = AT_CCWA,		// AT+CCWA - Call waiting control
	AT_SIM300_CEER = AT_CEER,		// AT+CEER - Extended error report
	AT_SIM300_CGMI = AT_CGMI,		// AT+CGMI - Request manufacturer identification
	AT_SIM300_CGMM = AT_CGMM,		// AT+CGMM - Request model identification
	AT_SIM300_CGMR = AT_CGMR,		// AT+CGMR - Request TA revision identification of software release
	AT_SIM300_CGSN = AT_CGSN,		// AT+CGSN - Request product serial number identification (identical with +GSN)
	AT_SIM300_CSCS = AT_CSCS,		// AT+CSCS - Select TE character set
	AT_SIM300_CSTA = AT_CSTA,		// AT+CSTA - Select type of address
	AT_SIM300_CHLD = AT_CHLD,		// AT+CHLD - Call hold and multiparty
	AT_SIM300_CIMI = AT_CIMI,		// AT+CIMI - Request international mobile subscriber identity
	AT_SIM300_CKPD = AT_CKPD,		// AT+CKPD - Keypad control
	AT_SIM300_CLCC = AT_CLCC,		// AT+CLCC - List current calls of ME
	AT_SIM300_CLCK = AT_CLCK,		// AT+CLCK - Facility lock
	AT_SIM300_CLIP = AT_CLIP,		// AT+CLIP - Calling line identification presentation
	AT_SIM300_CLIR = AT_CLIR,		// AT+CLIR - Calling line identification restriction
	AT_SIM300_CMEE = AT_CMEE,		// AT+CMEE - Report mobile equipment error
	AT_SIM300_COLP = AT_COLP,		// AT+COLP - Connected line identification presentation
	AT_SIM300_COPS = AT_COPS,		// AT+COPS - Operator selection
	AT_SIM300_CPAS = AT_CPAS,		// AT+CPAS - Mobile equipment activity status
	AT_SIM300_CPBF = AT_CPBF,		// AT+CPBF - Find phonebook entries
	AT_SIM300_CPBR = AT_CPBR,		// AT+CPBR - Read current phonebook entries
	AT_SIM300_CPBS = AT_CPBS,		// AT+CPBS - Select phonebook memory storage
	AT_SIM300_CPBW = AT_CPBW,		// AT+CPBW - Write phonebook entry
	AT_SIM300_CPIN = AT_CPIN,		// AT+CPIN - Enter PIN
	AT_SIM300_CPWD = AT_CPWD,		// AT+CPWD - Change password
	AT_SIM300_CR = AT_CR,			// AT+CR - Service reporting control
	AT_SIM300_CRC = AT_CRC,			// AT+CRC - Set cellular result codes for incoming call indication
	AT_SIM300_CREG = AT_CREG,		// AT+CREG - Network registration
	AT_SIM300_CRLP = AT_CRLP,		// AT+CRLP - Select radio link protocol parameter
	AT_SIM300_CRSM = AT_CRSM,		// AT+CRSM - Restricted SIM access
	AT_SIM300_CSQ = AT_CSQ,			// AT+CSQ - Signal quality report
	AT_SIM300_FCLASS = AT_FCLASS,	// AT+FCLASS - Fax: select, read or test service class
	AT_SIM300_FMI = AT_FMI,			// AT+FMI - Fax: report manufactured ID (SIM300)
	AT_SIM300_FMM = AT_FMM,			// AT+FMM - Fax: report model ID (SIM300)
	AT_SIM300_FMR = AT_FMR,			// AT+FMR - Fax: report revision ID (SIM300)
	AT_SIM300_VTD = AT_VTD,			// AT+VTD - Tone duration
	AT_SIM300_VTS = AT_VTS,			// AT+VTS - DTMF and tone generation
	AT_SIM300_CMUX = AT_CMUX,		// AT+CMUX - Multiplexer control
	AT_SIM300_CNUM = AT_CNUM,		// AT+CNUM - Subscriber number
	AT_SIM300_CPOL = AT_CPOL,		// AT+CPOL - Preferred operator list
	AT_SIM300_COPN = AT_COPN,		// AT+COPN - Read operator names
	AT_SIM300_CFUN = AT_CFUN,		// AT+CFUN - Set phone functionality
	AT_SIM300_CCLK = AT_CCLK,		// AT+CCLK - Clock
	AT_SIM300_CSIM = AT_CSIM,		// AT+CSIM - Generic SIM access
	AT_SIM300_CALM = AT_CALM,		// AT+CALM - Alert sound mode
	AT_SIM300_CRSL = AT_CRSL,		// AT+CRSL - Ringer sound level
	AT_SIM300_CLVL = AT_CLVL,		// AT+CLVL - Loud speaker volume level
	AT_SIM300_CMUT = AT_CMUT,		// AT+CMUT - Mute control
	AT_SIM300_CPUC = AT_CPUC,		// AT+CPUC - Price per unit currency table
	AT_SIM300_CCWE = AT_CCWE,		// AT+CCWE - Call meter maximum event
	AT_SIM300_CBC = AT_CBC,			// AT+CBC - Battery charge
	AT_SIM300_CUSD = AT_CUSD,		// AT+CUSD - Unstructured supplementary service data
	AT_SIM300_CSSN = AT_CSSN,		// AT+CSSN - Supplementary services notification

	// SIM300 GSM07.05 V1.06
	AT_SIM300_CMGD = AT_CMGD,		// AT+CMGD - Delete SMS message
	AT_SIM300_CMGF = AT_CMGF,		// AT+CMGF - Select SMS message format
	AT_SIM300_CMGL = AT_CMGL,		// AT+CMGL - List SMS messages from preferred store
	AT_SIM300_CMGR = AT_CMGR,		// AT+CMGR - Read SMS message
	AT_SIM300_CMGS = AT_CMGS,		// AT+CMGS - Send SMS message
	AT_SIM300_CMGW = AT_CMGW,		// AT+CMGW - Write SMS message to memory
	AT_SIM300_CMSS = AT_CMSS,		// AT+CMSS - Send SMS message from storage
	AT_SIM300_CMGC = AT_CMGC,		// AT+CMGC - Send sms command
	AT_SIM300_CNMI = AT_CNMI,		// AT+CNMI - New SMS message indications
	AT_SIM300_CPMS = AT_CPMS,		// AT+CPMS - Preferred SMS message storage
	AT_SIM300_CRES = AT_CRES,		// AT+CRES - Restore SMS settings
	AT_SIM300_CSAS = AT_CSAS,		// AT+CSAS - Save SMS settings
	AT_SIM300_CSCA = AT_CSCA,		// AT+CSCA - SMS service center address
	AT_SIM300_CSCB = AT_CSCB,		// AT+CSCB - Select cell broadcast SMS messages
	AT_SIM300_CSDH = AT_CSDH,		// AT+CSDH - Show SMS text mode parameters
	AT_SIM300_CSMP = AT_CSMP,		// AT+CSMP - Set SMS text mode parameters
	AT_SIM300_CSMS = AT_CSMS,		// AT+CSMS - Select message service

	// SIM300 SIMCOM V1.06
	AT_SIM300_ECHO,					// AT+ECHO - Echo cancellation control
	AT_SIM300_SIDET,				// AT+SIDET - Change the side tone gain level
	AT_SIM300_CPOWD,				// AT+CPOWD - Power off
	AT_SIM300_SPIC,					// AT+SPIC - Times remain to input SIM PIN/PUK
	AT_SIM300_CMIC,					// AT+CMIC - Change the micophone gain level
	AT_SIM300_UART,					// AT+UART - Configure dual serial port mode
	AT_SIM300_CALARM,				// AT+CALARM - Set alarm
	AT_SIM300_CADC,					// AT+CADC - Read adc
	AT_SIM300_CSNS,					// AT+CSNS - Single numbering scheme
	AT_SIM300_CDSCB,				// AT+CDSCB - Reset cellbroadcast
	AT_SIM300_CMOD,					// AT+CMOD - Configrue alternating mode calls
	AT_SIM300_CFGRI,				// AT+CFGRI - Indicate RI when using URC
	AT_SIM300_CLTS,					// AT+CLTS - Get local timestamp
	AT_SIM300_CEXTHS,				// AT+CEXTHS - External headset jack control
	AT_SIM300_CEXTBUT,				// AT+CEXTBUT - Headset button status reporting
	AT_SIM300_CSMINS,				// AT+CSMINS - SIM inserted status reporting
	AT_SIM300_CLDTMF,				// AT+CLDTMF - Local DTMF tone generation
	AT_SIM300_CDRIND,				// AT+CDRIND - CS voice/data/fax call or GPRS PDP context termination indication
	AT_SIM300_CSPN,					// AT+CSPN - Get service provider name from SIM
	AT_SIM300_CCVM,					// AT+CCVM - Get and set the voice mail number on the SIM
	AT_SIM300_CBAND,				// AT+CBAND - Get and set mobile operation band
	AT_SIM300_CHF,					// AT+CHF - Configures hands free operation
	AT_SIM300_CHFA,					// AT+CHFA - Swap the audio channels
	AT_SIM300_CSCLK,				// AT+CSCLK - Configure slow clock
	AT_SIM300_CENG,					// AT+CENG - Switch on or off engineering mode
	AT_SIM300_SCLASS0,				// AT+SCLASS0 - Store class 0 SMS to SIM when received class 0 SMS
	AT_SIM300_CCID,					// AT+CCID - Show ICCID
	AT_SIM300_CMTE,					// AT+CMTE - Read temperature of module
	AT_SIM300_CSDT,					// AT+CSDT - Switch on or off detecting SIM card
	AT_SIM300_CMGDA,				// AT+CMGDA - Delete all SMS
	AT_SIM300_SIMTONE,				// AT+SIMTONE - Generate specifically tone
	AT_SIM300_CCPD,					// AT+CCPD - Connected line identification presentation without alpha string
	AT_SIM300_CGID,					// AT+CGID - Get SIM card group identifier
	AT_SIM300_MORING,				// AT+MORING - Show state of mobile originated call
	AT_SIM300_CGMSCLASS,			// AT+CGMSCLASS - Change GPRS multislot class
	AT_SIM300_CMGHEX,				// AT+CMGHEX - Enable to send non-ASCII character SMS
	AT_SIM300_EXUNSOL,				// AT+EXUNSOL - Extra unsolicited indications
};
//------------------------------------------------------------------------------
// sim300 AT command parameters
// read
// cmic read
struct at_sim300_cmic_read {
	// integer (mandatory)
	int main_mic;	// Main microphone gain level
	// integer (mandatory)
	int aux_mic;	// Aux microphone gain level
};
// csmins read
struct at_sim300_csmins_read {
	// integer (mandatory)
	int n;	// Unsolicited event code status
	// integer (mandatory)
	int sim_inserted;	// SIM inserted status
};
//------------------------------------------------------------------------------

extern const struct at_command sim300_at_com_list[];
extern size_t sim300_at_com_list_length();

extern int at_sim300_cmic_read_parse(const char *fld, int fld_len, struct at_sim300_cmic_read *cmic);
extern int at_sim300_csmins_read_parse(const char *fld, int fld_len, struct at_sim300_csmins_read *csmins);

extern int sim300_build_imei_data1(char *data, int *len);
extern int sim300_build_imei_data2(char *data, int *len);
extern int sim300_build_imei_data3(const char *imei, char chk, char *data, int *len);

#endif //__SIM300_H__

/******************************************************************************/
/* end of sim300.h                                                            */
/******************************************************************************/
