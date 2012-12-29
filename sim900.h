/******************************************************************************/
/* sim900.h                                                                   */
/******************************************************************************/

#ifndef __SIM900_H__
#define __SIM900_H__

#include <sys/types.h>

#include "at.h"

//------------------------------------------------------------------------------
// at command id
enum {
	AT_SIM900_UNKNOWN = AT_UNKNOWN,
	// SIM900 V.25TER V1.04
	AT_SIM900_A_SLASH = AT_A_SLASH,		// A/ - Re-issues last AT command given
	AT_SIM900_A = AT_A,					// ATA - Answer an incoming call
	AT_SIM900_D = AT_D,					// ATD - Mobile originated call to dial a number
	AT_SIM900_D_CURMEM = AT_D_CURMEM,	// ATD><N> - Originate call to phone number in current memory
	AT_SIM900_D_PHBOOK = AT_D_PHBOOK,	// ATD><STR> - Originate call to phone number in memory which corresponds to field <STR>
	AT_SIM900_DL = AT_DL,				// ATDL - Redial last telephone number used
	AT_SIM900_E = AT_E,					// ATE - Set command echo mode
	AT_SIM900_H = AT_H,					// ATH - Disconnect existing connection
	AT_SIM900_I = AT_I,					// ATI - Display product identification information
	AT_SIM900_L = AT_L,					// ATL - Set monitor speaker loudness
	AT_SIM900_M = AT_M,					// ATM - Set monitor speaker mode
	AT_SIM900_3PLUS = AT_3PLUS,			// +++ - Switch from data mode or PPP online mode to command mode
	AT_SIM900_O = AT_O,					// ATO - Switch from command mode to data mode
	AT_SIM900_P = AT_P,					// ATP - Select pulse dialling
	AT_SIM900_Q = AT_Q,					// ATQ - Set result code presentation mode
	AT_SIM900_S0 = AT_S0,				// ATS0 - Set number of rings before automatically answering the call
	AT_SIM900_S3 = AT_S3,				// ATS3 - Set command line termination character
	AT_SIM900_S4 = AT_S4,				// ATS4 - Set response formatting character
	AT_SIM900_S5 = AT_S5,				// ATS5 - Set command line editing character
	AT_SIM900_S6 = AT_S6,				// ATS6 - Set pause before blind dialling
	AT_SIM900_S7 = AT_S7,				// ATS7 - Set number of seconds to wait for connection completion
	AT_SIM900_S8 = AT_S8,				// ATS8 - Set number of seconds to wait when comma dial modifier used
	AT_SIM900_S10 = AT_S10,				// ATS10 - Set disconnect delay after indicating the absence of data carrier
	AT_SIM900_T = AT_T,					// ATT - Select tone dialling
	AT_SIM900_V = AT_V,					// ATV - Set result code format mode
	AT_SIM900_X = AT_X,					// ATX - Set connect result code format and monitor call progress
	AT_SIM900_Z = AT_Z,					// ATZ - Set all current parameters to user defined profile
	AT_SIM900_andC = AT_andC,			// AT&C - Set DCD function mode
	AT_SIM900_andD = AT_andD,			// AT&D - Set DTR function mode
	AT_SIM900_andF = AT_andF,			// AT&F - Set all current parameters to manufacturer defaults
	AT_SIM900_andV = AT_andV,			// AT&V - Display current configuration
	AT_SIM900_andW = AT_andW,			// AT&W - Store current parameter to user defined profile
	AT_SIM900_GCAP = AT_GCAP,			// AT+GCAP - Request complete TA capabilities list
	AT_SIM900_GMI = AT_GMI,				// AT+GMI - Request manufacturer identification
	AT_SIM900_GMM = AT_GMM,				// AT+GMM - Request TA model identification
	AT_SIM900_GMR = AT_GMR,				// AT+GMR - Request TA revision indentification of software release
	AT_SIM900_GOI = AT_GOI,				// AT+GOI - Request global object identification
	AT_SIM900_GSN = AT_GSN,				// AT+GSN - Request ta serial number identification (IMEI)
	AT_SIM900_ICF = AT_ICF,				// AT+ICF - Set TE-TA control character framing
	AT_SIM900_IFC = AT_IFC,				// AT+IFC - Set TE-TA local data flow control
	AT_SIM900_ILRR = AT_ILRR,			// AT+ILRR - Set TE-TA local rate reporting mode
	AT_SIM900_IPR = AT_IPR,				// AT+IPR - Set TE-TA fixed local rate
	AT_SIM900_HVOIC,					// AT+HVOIC - Disconnect voive call only

	// SIM900 GSM07.07 V1.04
	AT_SIM900_CACM = AT_CACM,		// AT+CACM - Accumulated call meter(ACM) reset or query
	AT_SIM900_CAMM = AT_CAMM,		// AT+CAMM - Accumulated call meter maximum(ACMMAX) set or query
	AT_SIM900_CAOC = AT_CAOC,		// AT+CAOC - Advice of charge
	AT_SIM900_CBST = AT_CBST,		// AT+CBST - Select bearer service type
	AT_SIM900_CCFC = AT_CCFC,		// AT+CCFC - Call forwarding number and conditions control
	AT_SIM900_CCWA = AT_CCWA,		// AT+CCWA - Call waiting control
	AT_SIM900_CEER = AT_CEER,		// AT+CEER - Extended error report
	AT_SIM900_CGMI = AT_CGMI,		// AT+CGMI - Request manufacturer identification
	AT_SIM900_CGMM = AT_CGMM,		// AT+CGMM - Request model identification
	AT_SIM900_CGMR = AT_CGMR,		// AT+CGMR - Request TA revision identification of software release
	AT_SIM900_CGSN = AT_CGSN,		// AT+CGSN - Request product serial number identification (identical with +GSN)
	AT_SIM900_CSCS = AT_CSCS,		// AT+CSCS - Select TE character set
	AT_SIM900_CSTA = AT_CSTA,		// AT+CSTA - Select type of address
	AT_SIM900_CHLD = AT_CHLD,		// AT+CHLD - Call hold and multiparty
	AT_SIM900_CIMI = AT_CIMI,		// AT+CIMI - Request international mobile subscriber identity
	AT_SIM900_CKPD = AT_CKPD,		// AT+CKPD - Keypad control
	AT_SIM900_CLCC = AT_CLCC,		// AT+CLCC - List current calls of ME
	AT_SIM900_CLCK = AT_CLCK,		// AT+CLCK - Facility lock
	AT_SIM900_CLIP = AT_CLIP,		// AT+CLIP - Calling line identification presentation
	AT_SIM900_CLIR = AT_CLIR,		// AT+CLIR - Calling line identification restriction
	AT_SIM900_CMEE = AT_CMEE,		// AT+CMEE - Report mobile equipment error
	AT_SIM900_COLP = AT_COLP,		// AT+COLP - Connected line identification presentation
	AT_SIM900_COPS = AT_COPS,		// AT+COPS - Operator selection
	AT_SIM900_CPAS = AT_CPAS,		// AT+CPAS - Mobile equipment activity status
	AT_SIM900_CPBF = AT_CPBF,		// AT+CPBF - Find phonebook entries
	AT_SIM900_CPBR = AT_CPBR,		// AT+CPBR - Read current phonebook entries
	AT_SIM900_CPBS = AT_CPBS,		// AT+CPBS - Select phonebook memory storage
	AT_SIM900_CPBW = AT_CPBW,		// AT+CPBW - Write phonebook entry
	AT_SIM900_CPIN = AT_CPIN,		// AT+CPIN - Enter PIN
	AT_SIM900_CPWD = AT_CPWD,		// AT+CPWD - Change password
	AT_SIM900_CR = AT_CR,			// AT+CR - Service reporting control
	AT_SIM900_CRC = AT_CRC,			// AT+CRC - Set cellular result codes for incoming call indication
	AT_SIM900_CREG = AT_CREG,		// AT+CREG - Network registration
	AT_SIM900_CRLP = AT_CRLP,		// AT+CRLP - Select radio link protocol parameter
	AT_SIM900_CRSM = AT_CRSM,		// AT+CRSM - Restricted SIM access
	AT_SIM900_CSQ = AT_CSQ,			// AT+CSQ - Signal quality report
	AT_SIM900_FCLASS = AT_FCLASS,	// AT+FCLASS - Fax: select, read or test service class
	AT_SIM900_FMI = AT_FMI,			// AT+FMI - Fax: report manufactured ID (SIM300)
	AT_SIM900_FMM = AT_FMM,			// AT+FMM - Fax: report model ID (SIM300)
	AT_SIM900_FMR = AT_FMR,			// AT+FMR - Fax: report revision ID (SIM300)
	AT_SIM900_VTD = AT_VTD,			// AT+VTD - Tone duration
	AT_SIM900_VTS = AT_VTS,			// AT+VTS - DTMF and tone generation
	AT_SIM900_CMUX = AT_CMUX,		// AT+CMUX - Multiplexer control
	AT_SIM900_CNUM = AT_CNUM,		// AT+CNUM - Subscriber number
	AT_SIM900_CPOL = AT_CPOL,		// AT+CPOL - Preferred operator list
	AT_SIM900_COPN = AT_COPN,		// AT+COPN - Read operator names
	AT_SIM900_CFUN = AT_CFUN,		// AT+CFUN - Set phone functionality
	AT_SIM900_CCLK = AT_CCLK,		// AT+CCLK - Clock
	AT_SIM900_CSIM = AT_CSIM,		// AT+CSIM - Generic SIM access
	AT_SIM900_CALM = AT_CALM,		// AT+CALM - Alert sound mode
	AT_SIM900_CALS,					// AT+CALS - Alert sound select
	AT_SIM900_CRSL = AT_CRSL,		// AT+CRSL - Ringer sound level
	AT_SIM900_CLVL = AT_CLVL,		// AT+CLVL - Loud speaker volume level
	AT_SIM900_CMUT = AT_CMUT,		// AT+CMUT - Mute control
	AT_SIM900_CPUC = AT_CPUC,		// AT+CPUC - Price per unit currency table
	AT_SIM900_CCWE = AT_CCWE,		// AT+CCWE - Call meter maximum event
	AT_SIM900_CBC = AT_CBC,			// AT+CBC - Battery charge
	AT_SIM900_CUSD = AT_CUSD,		// AT+CUSD - Unstructured supplementary service data
	AT_SIM900_CSSN = AT_CSSN,		// AT+CSSN - Supplementary services notification

	// SIM900 GSM07.05 V1.04
	AT_SIM900_CMGD = AT_CMGD,		// AT+CMGD - Delete SMS message
	AT_SIM900_CMGF = AT_CMGF,		// AT+CMGF - Select SMS message format
	AT_SIM900_CMGL = AT_CMGL,		// AT+CMGL - List SMS messages from preferred store
	AT_SIM900_CMGR = AT_CMGR,		// AT+CMGR - Read SMS message
	AT_SIM900_CMGS = AT_CMGS,		// AT+CMGS - Send SMS message
	AT_SIM900_CMGW = AT_CMGW,		// AT+CMGW - Write SMS message to memory
	AT_SIM900_CMSS = AT_CMSS,		// AT+CMSS - Send SMS message from storage
	AT_SIM900_CNMI = AT_CNMI,		// AT+CNMI - New SMS message indications
	AT_SIM900_CPMS = AT_CPMS,		// AT+CPMS - Preferred SMS message storage
	AT_SIM900_CRES = AT_CRES,		// AT+CRES - Restore SMS settings
	AT_SIM900_CSAS = AT_CSAS,		// AT+CSAS - Save SMS settings
	AT_SIM900_CSCA = AT_CSCA,		// AT+CSCA - SMS service center address
	AT_SIM900_CSCB = AT_CSCB,		// AT+CSCB - Select cell broadcast SMS messages
	AT_SIM900_CSDH = AT_CSDH,		// AT+CSDH - Show SMS text mode parameters
	AT_SIM900_CSMP = AT_CSMP,		// AT+CSMP - Set SMS text mode parameters
	AT_SIM900_CSMS = AT_CSMS,		// AT+CSMS - Select message service

	// SIM900 SIMCOM V1.04
	AT_SIM900_SIDET,				// AT+SIDET - Change the side tone gain level
	AT_SIM900_CPOWD,				// AT+CPOWD - Power off	
	AT_SIM900_SPIC,					// AT+SPIC - Times remain to input SIM PIN/PUK
	AT_SIM900_CMIC,					// AT+CMIC - Change the micophone gain level
	AT_SIM900_CALA,					// AT+CALA - Set alarm time
	AT_SIM900_CALD,					// AT+CALD - Delete alarm
	AT_SIM900_CADC,					// AT+CADC - Read adc
	AT_SIM900_CSNS,					// AT+CSNS - Single numbering scheme
	AT_SIM900_CDSCB,				// AT+CDSCB - Reset cellbroadcast
	AT_SIM900_CMOD,					// AT+CMOD - Configrue alternating mode calls
	AT_SIM900_CFGRI,				// AT+CFGRI - Indicate RI when using URC
	AT_SIM900_CLTS,					// AT+CLTS - Get local timestamp
	AT_SIM900_CEXTHS,				// AT+CEXTHS - External headset jack control
	AT_SIM900_CEXTBUT,				// AT+CEXTBUT - Headset button status reporting
	AT_SIM900_CSMINS,				// AT+CSMINS - SIM inserted status reporting
	AT_SIM900_CLDTMF,				// AT+CLDTMF - Local DTMF tone generation
	AT_SIM900_CDRIND,				// AT+CDRIND - CS voice/data/fax call or GPRS PDP context termination indication
	AT_SIM900_CSPN,					// AT+CSPN - Get service provider name from SIM
	AT_SIM900_CCVM,					// AT+CCVM - Get and set the voice mail number on the SIM
	AT_SIM900_CBAND,				// AT+CBAND - Get and set mobile operation band
	AT_SIM900_CHF,					// AT+CHF - Configures hands free operation
	AT_SIM900_CHFA,					// AT+CHFA - Swap the audio channels
	AT_SIM900_CSCLK,				// AT+CSCLK - Configure slow clock
	AT_SIM900_CENG,					// AT+CENG - Switch on or off engineering mode
	AT_SIM900_SCLASS0,				// AT+SCLASS0 - Store class 0 SMS to SIM when received class 0 SMS
	AT_SIM900_CCID,					// AT+CCID - Show ICCID
	AT_SIM900_CMTE,					// AT+CMTE - Set critical temperature operating mode or query temperature
	AT_SIM900_CBTE,					// AT+CBTE - Read temperature of module
	AT_SIM900_CSDT,					// AT+CSDT - Switch on or off detecting SIM card
	AT_SIM900_CMGDA,				// AT+CMGDA - Delete all SMS
	AT_SIM900_STTONE,				// AT+STTONE - Play SIM Toolkit tone
	AT_SIM900_SIMTONE,				// AT+SIMTONE - Generate specifically tone
	AT_SIM900_CCPD,					// AT+CCPD - Connected line identification presentation without alpha string
	AT_SIM900_CGID,					// AT+CGID - Get SIM card group identifier
	AT_SIM900_MORING,				// AT+MORING - Show state of mobile originated call
	AT_SIM900_CMGHEX,				// AT+CMGHEX - Enable to send non-ASCII character SMS
	AT_SIM900_CCODE,				// AT+CCODE - Configrue SMS code mode
	AT_SIM900_CIURC,				// AT+CIURC - Enable or disable initial URC presentation
	AT_SIM900_CPSPWD,				// AT+CPSPWD - Change PS super password
	AT_SIM900_EXUNSOL,				// AT+EXUNSOL - Extra unsolicited indications
	AT_SIM900_CGMSCLASS,			// AT+CGMSCLASS - Change GPRS multislot class
	AT_SIM900_CDEVICE,				// AT+CDEVICE - View current flash device type
	AT_SIM900_CCALR,				// AT+CCALR - Call ready query
	AT_SIM900_GSV,					// AT+GSV - Display product identification information
	AT_SIM900_SGPIO,				// AT+SGPIO - Control the GPIO
	AT_SIM900_SPWM,					// AT+SPWM - Generate PWM
	AT_SIM900_ECHO,					// AT+ECHO - Echo cancellation control
	AT_SIM900_CAAS,					// AT+CAAS - Control auto audio switch
	AT_SIM900_SVR,					// AT+SVR - Configrue voice coding type for voice calls
	AT_SIM900_GSMBUSY,				// AT+GSMBUSY - Reject incoming call
	AT_SIM900_CEMNL,				// AT+CEMNL - Set the list of emergency number
	AT_SIM900_CELLLOCK,				// AT+CELLLOCK - Set the list of arfcn which needs to be locked
	AT_SIM900_SLEDS,				// AT+SLEDS - Set the timer period of net light

	AT_SIM900_MAXNUM,
};

struct at_sim900_cmic_read {
	// integer (mandatory)
	int main_hs_mic;	// Main handset microphone gain level
	// integer (mandatory)
	int aux_hs_mic;	// Aux handset microphone gain level
	// integer (mandatory)
	int main_hf_mic;	// Main handfree microphone gain level
	// integer (mandatory)
	int aux_hf_mic;	// Aux handfree microphone gain level
};

struct at_sim900_csmins_read {
	// integer (mandatory)
	int n;	// Unsolicited event code status
	// integer (mandatory)
	int sim_inserted;	// SIM inserted status
};

extern const struct at_command sim900_at_com_list[];

extern int at_sim900_cmic_read_parse(const char *fld, int fld_len, struct at_sim900_cmic_read *cmic);
extern int at_sim900_csmins_read_parse(const char *fld, int fld_len, struct at_sim900_csmins_read *csmins);

extern char *sim900_cmd_sel_mem_reg_build(const char *out, u_int32_t start, u_int32_t size);
extern size_t sim900_cmd_sel_mem_reg_size(void);
extern char *sim900_cmd_erase_mem_reg_build(const char *out, u_int32_t start, u_int32_t size);
extern size_t sim900_cmd_erase_mem_reg_size(void);
extern char *sim900_cmd_set_code_section_build(const char *out, u_int32_t size);
extern size_t sim900_cmd_set_code_section_size(void);
extern char *sim900_cmd_calc_checksum_build(const char *out, u_int32_t start, u_int32_t checksum, u_int32_t size);
extern size_t sim900_cmd_calc_checksum_size(void);

#endif //__SIM900_H__

/******************************************************************************/
/* end of sim900.h                                                            */
/******************************************************************************/
