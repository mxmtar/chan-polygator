/******************************************************************************/
/* at.h                                                                       */
/******************************************************************************/

#ifndef __AT_H__
#define __AT_H__

#include <sys/types.h>
//------------------------------------------------------------------------------
// AT command enumerator
enum {
	AT_UNKNOWN = 0,

	// V.25TER
	AT_A_SLASH,		// A/ - Re-issues last AT command given
	AT_A,			// ATA - Answer an incoming call
	AT_D,			// ATD - Mobile originated call to dial a number
	AT_D_MEM,		// ATD><MEM><N> - Originate call to phone number in memory <MEM>
	AT_D_CURMEM,	// ATD><N> - Originate call to phone number in current memory
	AT_D_PHBOOK,	// ATD><STR> - Originate call to phone number in memory which corresponds to field <STR>
	AT_DL,			// ATDL - Redial last telephone number used
	AT_E,			// ATE - Set command echo mode
	AT_H,			// ATH - Disconnect existing connection
	AT_I,			// ATI - Display product identification information
	AT_L,			// ATL - Set monitor speaker loudness
	AT_M,			// ATM - Set monitor speaker mode
	AT_3PLUS,		// +++ - Switch from data mode or PPP online mode to command mode
	AT_O,			// ATO - Switch from command mode to data mode
	AT_P,			// ATP - Select pulse dialling
	AT_Q,			// ATQ - Set result code presentation mode
	AT_S0,			// ATS0 - Set number of rings before automatically answering the call
	AT_S3,			// ATS3 - Set command line termination character
	AT_S4,			// ATS4 - Set response formatting character
	AT_S5,			// ATS5 - Set command line editing character
	AT_S6,			// ATS6 - Set pause before blind dialling
	AT_S7,			// ATS7 - Set number of seconds to wait for connection completion
	AT_S8,			// ATS8 - Set number of seconds to wait when comma dial modifier used
	AT_S10,			// ATS10 - Set disconnect delay after indicating the absence of data carrier
	AT_T,			// ATT - Select tone dialling
	AT_V,			// ATV - Set result code format mode
	AT_X,			// ATX - Set connect result code format and monitor call progress
	AT_Z,			// ATZ - Set all current parameters to user defined profile
	AT_andC,		// AT&C - Set DCD function mode
	AT_andD,		// AT&D - Set DTR function mode
	AT_andF,		// AT&F - Set all current parameters to manufacturer defaults
	AT_andV,		// AT&V - Display current configuration
	AT_andW,		// AT&W - Store current parameter to user defined profile
	AT_DR,			// AT+DR - V.42bis data compression reporting control
	AT_DS,			// AT+DS - V.42bis data compression control
	AT_GCAP,		// AT+GCAP - Request complete ta capabilities list
	AT_GMI,			// AT+GMI - Request manufacturer identification
	AT_GMM,			// AT+GMM - Request ta model identification
	AT_GMR,			// AT+GMR - Request ta revision indentification of software release
	AT_GOI,			// AT+GOI - Request global object identification
	AT_GSN,			// AT+GSN - Request ta serial number identification (IMEI)
	AT_ICF,			// AT+ICF - Set TE-TA control character framing
	AT_IFC,			// AT+IFC - Set TE-TA local data flow control
	AT_ILRR,		// AT+ILRR - Set TE-TA local rate reporting mode
	AT_IPR,			// AT+IPR - Set TE-TA fixed local rate

	// GSM07.07
	AT_CACM,		// AT+CACM - Accumulated call meter(ACM) reset or query
	AT_CAMM,		// AT+CAMM - Accumulated call meter maximum(ACMMAX) set or query
	AT_CAOC,		// AT+CAOC - Advice of charge
	AT_CBST,		// AT+CBST - Select bearer service type
	AT_CCFC,		// AT+CCFC - Call forwarding number and conditions control
	AT_CCUG,		// AT+CCUG - Closed user group control
	AT_CCWA,		// AT+CCWA - Call waiting control
	AT_CEER,		// AT+CEER - Extended error report
	AT_CGMI,		// AT+CGMI - Request manufacturer identification
	AT_CGMM,		// AT+CGMM - Request model identification
	AT_CGMR,		// AT+CGMR - Request ta revision identification of software release
	AT_CGSN,		// AT+CGSN - Request product serial number identification (identical with +GSN)
	AT_CSCS,		// AT+CSCS - Select TE character set
	AT_CSTA,		// AT+CSTA - Select type of address
	AT_CHLD,		// AT+CHLD - Call hold and multiparty
	AT_CIMI,		// AT+CIMI - Request international mobile subscriber identity
	AT_CKPD,		// AT+CKPD - Keypad control
	AT_CLCC,		// AT+CLCC - List current calls of ME
	AT_CLCK,		// AT+CLCK - Facility lock
	AT_CLIP,		// AT+CLIP - Calling line identification presentation
	AT_CLIR,		// AT+CLIR - Calling line identification restriction
	AT_CMEE,		// AT+CMEE - Report mobile equipment error
	AT_COLP,		// AT+COLP - Connected line identification presentation
	AT_COPS,		// AT+COPS - Operator selection
	AT_CPAS,		// AT+CPAS - Mobile equipment activity status
	AT_CPBF,		// AT+CPBF - Find phonebook entries
	AT_CPBR,		// AT+CPBR - Read current phonebook entries
	AT_CPBS,		// AT+CPBS - Select phonebook memory storage
	AT_CPBW,		// AT+CPBW - Write phonebook entry
	AT_CPIN,		// AT+CPIN - Enter PIN
	AT_CPWD,		// AT+CPWD - Change password
	AT_CR,			// AT+CR - Service reporting control
	AT_CRC,			// AT+CRC - Set cellular result codes for incoming call indication
	AT_CREG,		// AT+CREG - Network registration
	AT_CRLP,		// AT+CRLP - Select radio link protocol parameter
	AT_CRSM,		// AT+CRSM - Restricted SIM access
	AT_CSQ,			// AT+CSQ - Signal quality report
	AT_FCLASS,		// AT+FCLASS - Fax: select, read or test service class
	AT_FMI,			// AT+FMI - Fax: report manufactured ID (SIM300)
	AT_FMM,			// AT+FMM - Fax: report model ID (SIM300)
	AT_FMR,			// AT+FMR - Fax: report revision ID (SIM300)
	AT_VTD,			// AT+VTD - Tone duration
	AT_VTS,			// AT+VTS - DTMF and tone generation
	AT_CMUX,		// AT+CMUX - Multiplexer control
	AT_CNUM,		// AT+CNUM - Subscriber number
	AT_CPOL,		// AT+CPOL - Preferred operator list
	AT_COPN,		// AT+COPN - Read operator names
	AT_CFUN,		// AT+CFUN - Set phone functionality
	AT_CCLK,		// AT+CCLK - Clock
	AT_CSIM,		// AT+CSIM - Generic SIM access
	AT_CALM,		// AT+CALM - Alert sound mode
	AT_CRSL,		// AT+CRSL - Ringer sound level
	AT_CLVL,		// AT+CLVL - Loud speaker volume level
	AT_CMUT,		// AT+CMUT - Mute control
	AT_CPUC,		// AT+CPUC - Price per unit currency table
	AT_CCWE,		// AT+CCWE - Call meter maximum event
	AT_CBC,			// AT+CBC - Battery charge
	AT_CUSD,		// AT+CUSD - Unstructured supplementary service data
	AT_CSSN,		// AT+CSSN - Supplementary services notification
	AT_CSNS,		// AT+CSNS - Single numbering scheme (M10)
	AT_CMOD,		// AT+CMOD - Configure alternating mode calls (M10)

	// GSM07.05
	AT_CMGD,		// AT+CMGD - Delete SMS message
	AT_CMGF,		// AT+CMGF - Select SMS message format
	AT_CMGL,		// AT+CMGL - List SMS messages FROM preferred store
	AT_CMGR,		// AT+CMGR - Read SMS message
	AT_CMGS,		// AT+CMGS - Send SMS message
	AT_CMGW,		// AT+CMGW - Write SMS message to memory
	AT_CMSS,		// AT+CMSS - Send SMS message from storage
	AT_CMGC,		// AT+CMGC - Send SMS command
	AT_CNMI,		// AT+CNMI - New SMS message indications
	AT_CPMS,		// AT+CPMS - Preferred SMS message storage
	AT_CRES,		// AT+CRES - Restore SMS settings
	AT_CSAS,		// AT+CSAS - Save SMS settings
	AT_CSCA,		// AT+CSCA - SMS service center address
	AT_CSCB,		// AT+CSCB - Select cell broadcast SMS messages
	AT_CSDH,		// AT+CSDH - Show SMS text mode parameters
	AT_CSMP,		// AT+CSMP - Set SMS text mode parameters
	AT_CSMS,		// AT+CSMS - Select message service
};
//------------------------------------------------------------------------------
// CME ERROR CODE
enum {
	CME_ERROR_PHONE_FAILURE									= 0,
	CME_ERROR_NO_CONNECTION_TO_PHONE						= 1,
	CME_ERROR_PHONE_ADAPTOR_LINK_RESERVED					= 2,
	CME_ERROR_OPERATION_NOT_ALLOWED							= 3,
	CME_ERROR_OPERATION_NOT_SUPPORTED						= 4,
	CME_ERROR_PH_SIM_PIN_REQUIRED							= 5,
	CME_ERROR_PH_FSIM_PIN_REQUIRED							= 6,
	CME_ERROR_PH_FSIM_PUK_REQUIRED							= 7,
	CME_ERROR_SIM_NOT_INSERTED								= 10,
	CME_ERROR_SIM_PIN_REQUIRED								= 11,
	CME_ERROR_SIM_PUK_REQUIRED								= 12,
	CME_ERROR_SIM_FAILURE									= 13,
	CME_ERROR_SIM_BUSY										= 14,
	CME_ERROR_SIM_WRONG										= 15,
	CME_ERROR_INCORRECT_PASSWORD							= 16,
	CME_ERROR_SIM_PIN2_REQUIRED								= 17,
	CME_ERROR_SIM_PUK2_REQUIRED								= 18,
	CME_ERROR_MEMORY_FULL									= 20,
	CME_ERROR_INVALID_INDEX									= 21,
	CME_ERROR_NOT_FOUND										= 22,
	CME_ERROR_MEMORY_FAILURE								= 23,
	CME_ERROR_TEXT_STRING_TOO_LONG							= 24,
	CME_ERROR_INVALID_CHARACTERS_IN_TEXT_STRING				= 25,
	CME_ERROR_DIAL_STRING_TOO_LONG							= 26,
	CME_ERROR_INVALID_CHARACTERS_IN_DIAL_STRING				= 27,
	CME_ERROR_NO_NETWORK_SERVICE							= 30,
	CME_ERROR_NETWORK_TIMEOUT								= 31,
	CME_ERROR_NETWORK_NOT_ALLOWED_EMERGENCY_CALLS_ONLY		= 32,
	CME_ERROR_NETWORK_PERSONALIZATION_PIN_REQUIRED			= 40,
	CME_ERROR_NETWORK_PERSONALIZATION_PUK_REQUIRED			= 41,
	CME_ERROR_NETWORK_SUBSET_PERSONALIZATION_PIN_REQUIRED	= 42,
	CME_ERROR_NETWORK_SUBSET_PERSONALIZATION_PUK_REQUIRED	= 43,
	CME_ERROR_SERVICE_PROVIDER_PERSONALIZATION_PIN_REQUIRED	= 44,
	CME_ERROR_SERVICE_PROVIDER_PERSONALIZATION_PUK_REQUIRED	= 45,
	CME_ERROR_CORPORATE_PERSONALIZATION_PIN_REQUIRED		= 46,
	CME_ERROR_CORPORATE_PERSONALIZATION_PUK_REQUIRED		= 47,
	CME_ERROR_UNKNOWN										= 100,
	CME_ERROR_ILLEGAL_MS									= 103,
	CME_ERROR_ILLEGAL_ME									= 106,
	CME_ERROR_GPRS_SERVICES_NOT_ALLOWED						= 107,
	CME_ERROR_PLMN_NOT_ALLOWED								= 111,
	CME_ERROR_LOCATION_AREA_NOT_ALLOWED						= 112,
	CME_ERROR_ROAMING_NOT_ALLOWED_IN_THIS_LOCATION_AREA		= 113,
	CME_ERROR_SERVICE_OPTION_NOT_SUPPORTED					= 132,
	CME_ERROR_REQUESTED_SERVICE_OPTION_NOT_SUBSCRIBED		= 133,
	CME_ERROR_SERVICE_OPTION_TEMPORARILY_OUT_OF_ORDER		= 134,
	CME_ERROR_UNSPECIFIED_GPRS_ERROR						= 148,
	CME_ERROR_PDP_AUTHENTICATION_FAILURE					= 149,
	CME_ERROR_INVALID_MOBILE_CLASS							= 150,
};
//------------------------------------------------------------------------------
// CMS ERROR CODE
enum {
	CMS_ERROR_ME_FAILURE				= 300,
	CMS_ERROR_SMS_ME_RESERVED			= 301,
	CMS_ERROR_OPER_NOT_ALLOWED			= 302,
	CMS_ERROR_OPER_NOT_SUPPORTED		= 303,
	CMS_ERROR_INVALID_PDU_MODE			= 304,
	CMS_ERROR_INVALID_TEXT_MODE			= 305,
	CMS_ERROR_SIM_NOT_INSERTED			= 310,
	CMS_ERROR_SIM_PIN_NECESSARY			= 311,
	CMS_ERROR_PH_SIM_PIN_NECESSARY		= 312,
	CMS_ERROR_SIM_FAILURE				= 313,
	CMS_ERROR_SIM_BUSY					= 314,
	CMS_ERROR_SIM_WRONG					= 315,
	CMS_ERROR_SIM_PUK_REQUIRED			= 316,
	CMS_ERROR_SIM_PIN2_REQUIRED			= 317,
	CMS_ERROR_SIM_PUK2_REQUIRED			= 318,
	CMS_ERROR_MEMORY_FAILURE			= 320,
	CMS_ERROR_INVALID_MEMORY_INDEX		= 321,
	CMS_ERROR_MEMORY_FULL				= 322,
	CMS_ERROR_SMSC_ADDRESS_UNKNOWN		= 330,
	CMS_ERROR_NO_NETWORK				= 331,
	CMS_ERROR_NETWORK_TIMEOUT			= 332,
	CMS_ERROR_UNKNOWN					= 500,
	CMS_ERROR_SIM_NOT_READY				= 512,
	CMS_ERROR_UNREAD_SIM_RECORDS		= 513,
	CMS_ERROR_CB_ERROR_UNKNOWN			= 514,
	CMS_ERROR_PS_BUSY					= 515,
	CMS_ERROR_SM_BL_NOT_READY			= 517,
	CMS_ERROR_INVAL_CHARS_IN_PDU		= 528,
	CMS_ERROR_INCORRECT_PDU_LENGTH		= 529,
	CMS_ERROR_INVALID_MTI				= 530,
	CMS_ERROR_INVAL_CHARS_IN_ADDR		= 531,
	CMS_ERROR_INVALID_ADDRESS			= 532,
	CMS_ERROR_INCORRECT_PDU_UDL_L		= 533,
	CMS_ERROR_INCORRECT_SCA_LENGTH		= 534,
	CMS_ERROR_INVALID_FIRST_OCTET		= 536,
	CMS_ERROR_INVALID_COMMAND_TYPE		= 537,
	CMS_ERROR_SRR_BIT_NOT_SET			= 538,
	CMS_ERROR_SRR_BIT_SET				= 539,
	CMS_ERROR_INVALID_UDH_IE			= 540,
};
//------------------------------------------------------------------------------
// registartion status
enum {
	REG_STAT_NOTREG_NOSEARCH = 0,
	REG_STAT_REG_HOME_NET = 1,
	REG_STAT_NOTREG_SEARCH = 2,
	REG_STAT_REG_DENIED = 3,
	REG_STAT_UNKNOWN = 4,
	REG_STAT_REG_ROAMING = 5,
};
//------------------------------------------------------------------------------
// call class
enum {
	CALL_CLASS_VOICE = (1 << 0),
	CALL_CLASS_DATA = (1 << 1),
	CALL_CLASS_FAX = (1 << 2),
};
//------------------------------------------------------------------------------
// parsing param type
enum {
	PRM_TYPE_UNKNOWN = 0,
	PRM_TYPE_STRING = 1,
	PRM_TYPE_INTEGER = 2,
};
//------------------------------------------------------------------------------
// AT command operation
enum {
	AT_OPER_EXEC = (1 << 0),
	AT_OPER_TEST = (1 << 1),
	AT_OPER_READ = (1 << 2),
	AT_OPER_WRITE = (1 << 3),
	AT_OPER_COUNT,
};
//------------------------------------------------------------------------------
// at command
#define MAX_AT_CMD_RESP 2
typedef int add_check_at_resp_fun_t(const char*);
struct at_command {
	int id;
	u_int32_t operations;
	char name[16];
	char response[MAX_AT_CMD_RESP][16];
	char description[256];
	add_check_at_resp_fun_t *check_fun;
};
//------------------------------------------------------------------------------
// at command operation
struct at_command_operation {
	u_int32_t id;
	char str[4];
};
//------------------------------------------------------------------------------
// parsing param
struct parsing_param {
	int type;
	char *buf;
	int len;
};
//------------------------------------------------------------------------------
// general AT command parameters
// exec
// clcc exec
struct at_gen_clcc_exec {
	// integer (mandatory)
	int id;		// line id
	// integer (mandatory)
	int dir;	// call direction
	// integer (mandatory)
	int stat;	// call state
	// integer (mandatory)
	int mode;	// call mode
	// integer (mandatory)
	int mpty;	// call multiparty
	// string (mandatory)
	char *number;	// phone number
	int number_len;
	// integer (mandatory)
	int type;	// number type
};
// csq exec
struct at_gen_csq_exec {
	// integer (mandatory)
	int rssi;	// rssi level
	// integer (mandatory)
	int ber;		// ber level
};
// cnum exec
struct at_gen_cnum_exec {
	// string
	char *alpha;	// optional name
	int alpha_len;
	// string (mandatory)
	char *number;	// phone number
	int number_len;
	// integer (mandatory)
	int type;		// type of address
	// integer
	int speed;		// speed
	// integer
	int service;	// service related to phone number
	// integer
	int itc;		// information transfer capability
};
// read
// clir read
struct at_gen_clir_read {
	// integer (mandatory)
	int n;	// CLIR setting
	// integer (mandatory)
	int m;	// CLIR status
};
// cops read
struct at_gen_cops_read {
	// integer (mandatory)
	int mode;	// mode of registration
	// integer
	int format;	// information transfer capability
	// string
	char *oper;	// operator presented in format above
	int oper_len;
};
// creg read
struct at_gen_creg_read {
	// integer (mandatory)
	int n;			// unsolicited result enable
	// integer (mandatory)
	int stat;		// registration status
	// string
	char *lac;		// location area code
	int lac_len;
	// string
	char *ci;		// cell ID
	int ci_len;
};
// csca read
struct at_gen_csca_read {
	// string (mandatory)
	char *sca;		// service center address
	int sca_len;
	// integer (mandatory)
	int tosca;		// type of service center address
};
// write
// ccwa write
struct at_gen_ccwa_write {
	// integer (mandatory)
	int status;	// call wait status
	// integer (mandatory)
	int class;	// call class
};
// cusd write
struct at_gen_cusd_write {
	// integer (mandatory)
	int n;	// control of registration
	// string (optional)
	char *str;	// ussd response
	int str_len;
	// integer (optional)
	int dcs;	// Cell Broadcast Data Coding Scheme
};
// cmgr write
struct at_gen_cmgr_write {
	// integer (mandatory)
	int stat;
	// string (optional)
	char *alpha;
	int alpha_len;
	// integer (mandatory)
	int length;
};
// unsolicited
// clip unsolicited
struct at_gen_clip_unsol {
	// string (mandatory)
	char *number;	// phone number
	int number_len;
	// integer (mandatory)
	int type;	// type of address
	// string (mandatory)
	char *alphaid;	// phone book number representation
	int alphaid_len;
	// integer (mandatory)
	int cli_validity;	// information of CLI validity
};
//------------------------------------------------------------------------------
// prototype
extern struct at_command *get_at_com_by_id(int id, const struct at_command *list, int maxnum);
extern int is_at_com_done(const char *response);
extern int is_at_com_response(struct at_command *at, const char *response);
extern char *get_at_com_oper_by_id(u_int32_t oper);
extern char *reg_status_print(int stat);
extern char *reg_status_print_short(int stat);
extern char *rssi_print(char *obuf, int rssi);
extern char *rssi_print_short(char *obuf, int rssi);
extern char *ber_print(int ber);
extern char *ber_print_short(int ber);
extern char *cme_error_print(int ec);
extern char *cms_error_print(int ec);
// prototype parse function
// exec
extern int at_gen_clcc_exec_parse(const char *fld, int fld_len, struct at_gen_clcc_exec *clcc);
extern int at_gen_csq_exec_parse(const char *fld, int fld_len, struct at_gen_csq_exec *csq);
extern int at_gen_cnum_exec_parse(const char *fld, int fld_len, struct at_gen_cnum_exec *cnum);
// read
extern int at_gen_clir_read_parse(const char *fld, int fld_len, struct at_gen_clir_read *clir);
extern int at_gen_cops_read_parse(const char *fld, int fld_len, struct at_gen_cops_read *cops);
extern int at_gen_creg_read_parse(const char *fld, int fld_len, struct at_gen_creg_read *creg);
extern int at_gen_csca_read_parse(const char *fld, int fld_len, struct at_gen_csca_read *csca);
// write
extern int at_gen_ccwa_write_parse(const char *fld, int fld_len, struct at_gen_ccwa_write *ccwa);
extern int at_gen_cusd_write_parse(const char *fld, int fld_len, struct at_gen_cusd_write *cusd);
extern int at_gen_cmgr_write_parse(const char *fld, int fld_len, struct at_gen_cmgr_write *cmgr);
// unsolicited
extern int at_gen_clip_unsol_parse(const char *fld, int fld_len, struct at_gen_clip_unsol *clip);

#endif //__AT_H__

/******************************************************************************/
/* end of at.h                                                                */
/******************************************************************************/
