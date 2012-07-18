/******************************************************************************/
/* m10.c                                                                      */
/******************************************************************************/
/* $Rev:: 140                        $                                        */
/* $Author:: maksym                  $                                        */
/* $Date:: 2012-03-20 18:19:44 +0200#$                                        */
/******************************************************************************/

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "at.h"
#include "m10.h"
#include "strutil.h"
//------------------------------------------------------------------------------

const struct at_command m10_at_com_list[AT_M10_MAXNUM] = {
	// int id; u_int32_t operations; char name[16]; char response[MAX_AT_CMD_RESP][16]; char description[256]; add_check_at_resp_fun_t *check_fun;
	{AT_M10_UNKNOWN, AT_OPER_EXEC, "", {"", ""}, "", is_str_printable},
	// M10 V.25TER V1.04
	{AT_M10_A_SLASH, AT_OPER_EXEC, "A/", {"", ""}, "Re-issues last AT command given", NULL},
	{AT_M10_A, AT_OPER_EXEC, "ATA", {"", ""}, "Answer an incoming call", NULL},
	{AT_M10_D, AT_OPER_EXEC, "ATD", {"", ""}, "Mobile originated call to dial a number", NULL},
	{AT_M10_D_CURMEM, AT_OPER_EXEC, "ATD", {"", ""}, "Originate call to phone number in current memory", NULL},
	{AT_M10_DL, AT_OPER_EXEC, "ATDL",  {"", ""}, "Redial last telephone number used", NULL},
	{AT_M10_E, AT_OPER_EXEC, "ATE",  {"", ""}, "Set command echo mode", NULL},
	{AT_M10_H, AT_OPER_EXEC, "ATH",  {"", ""}, "Disconnect existing connection", NULL},
	{AT_M10_I, AT_OPER_EXEC, "ATI",  {"", ""}, "Display product identification information", NULL},
	{AT_M10_L, AT_OPER_EXEC, "ATL",  {"", ""}, "Set monitor speaker loudness", NULL},
	{AT_M10_M, AT_OPER_EXEC, "ATM",  {"", ""}, "Set monitor speaker mode", NULL},
	{AT_M10_3PLUS, AT_OPER_EXEC, "+++",  {"", ""}, "Switch from data mode or PPP online mode to command mode", NULL},
	{AT_M10_O, AT_OPER_EXEC, "ATO",  {"", ""}, "Switch from command mode to data mode", NULL},
	{AT_M10_P, AT_OPER_EXEC, "ATP",  {"", ""}, "Select pulse dialling", NULL},
	{AT_M10_Q, AT_OPER_EXEC, "ATQ",  {"", ""}, "Set result code presentation mode", NULL},
	{AT_M10_S0, AT_OPER_READ|AT_OPER_WRITE, "ATS0",  {"", ""}, "Set number of rings before automatically answering the call", NULL},
	{AT_M10_S3, AT_OPER_READ|AT_OPER_WRITE, "ATS3",  {"", ""}, "Set command line termination character", NULL},
	{AT_M10_S4, AT_OPER_READ|AT_OPER_WRITE, "ATS4",  {"", ""}, "Set response formatting character", NULL},
	{AT_M10_S5, AT_OPER_READ|AT_OPER_WRITE, "ATS5",  {"", ""}, "Set command line editing character", NULL},
	{AT_M10_S6, AT_OPER_READ|AT_OPER_WRITE, "ATS6",  {"", ""}, "Set pause before blind dialling", NULL},
	{AT_M10_S7, AT_OPER_READ|AT_OPER_WRITE, "ATS7",  {"", ""}, "Set number of seconds to wait for connection completion", NULL},
	{AT_M10_S8, AT_OPER_READ|AT_OPER_WRITE, "ATS8",  {"", ""}, "Set number of seconds to wait when comma dial modifier used", NULL},
	{AT_M10_S10, AT_OPER_READ|AT_OPER_WRITE, "ATS10",  {"", ""}, "Set disconnect delay after indicating the absence of data carrier", NULL},
	{AT_M10_T, AT_OPER_EXEC, "ATT",  {"", ""}, "Select tone dialling", NULL},
	{AT_M10_V, AT_OPER_EXEC, "ATV",  {"", ""}, "Set result code format mode", NULL},
	{AT_M10_X, AT_OPER_EXEC, "ATX",  {"", ""}, "Set connect result code format and monitor call progress", NULL},
	{AT_M10_Z, AT_OPER_EXEC, "ATZ",  {"", ""}, "Set all current parameters to user defined profile", NULL},
	{AT_M10_andC, AT_OPER_EXEC, "AT&C",  {"", ""}, "Set DCD function mode", NULL},
	{AT_M10_andD, AT_OPER_EXEC, "AT&D",  {"", ""}, "Set DTR function mode", NULL},
	{AT_M10_andF, AT_OPER_EXEC, "AT&F",  {"", ""}, "Set all current parameters to manufacturer defaults", NULL},
	{AT_M10_andV, AT_OPER_EXEC, "AT&V",  {"", ""}, "Display current configuration", NULL},
	{AT_M10_andW, AT_OPER_EXEC, "AT&W",  {"", ""}, "Store current parameter to user defined profile", NULL},
	{AT_M10_DR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+DR",  {"+DR:", ""}, "V.42bis data compression reporting control", NULL},
	{AT_M10_DS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+DS",  {"+DS:", ""}, "V.42bis data compression control", NULL},
	{AT_M10_GCAP, AT_OPER_TEST|AT_OPER_EXEC, "AT+GCAP",  {"+GCAP:", ""}, "Request complete TA capabilities list", NULL},
	{AT_M10_GMI, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMI",  {"", ""}, "Request manufacturer identification", is_str_printable},
	{AT_M10_GMM, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMM",  {"", ""}, "Request TA model identification", is_str_printable},
	{AT_M10_GMR, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMR",  {"", ""}, "Request TA revision indentification of software release", is_str_printable},
	{AT_M10_GOI, AT_OPER_TEST|AT_OPER_EXEC, "AT+GOI",  {"", ""}, "Request global object identification", is_str_printable},
	{AT_M10_GSN, AT_OPER_TEST|AT_OPER_EXEC, "AT+GSN",  {"", ""}, "Request ta serial number identification (IMEI)", is_str_digit},
	{AT_M10_ICF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+ICF",  {"+ICF:", ""}, "Set TE-TA control character framing", NULL},
	{AT_M10_IFC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+IFC",  {"+IFC:", ""}, "Set TE-TA local data flow control", NULL},
	{AT_M10_ILRR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+ILRR",  {"+ILRR:", ""}, "Set TE-TA local rate reporting mode", NULL},
	{AT_M10_IPR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+IPR",  {"+IPR:", ""}, "Set TE-TA fixed local rate", NULL},

	// M10 GSM07.07 V1.04
	{AT_M10_CACM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CACM",  {"+CACM:", ""}, "Accumulated call meter(ACM) reset or query", NULL},
	{AT_M10_CAMM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CAMM",  {"+CAMM:", ""}, "Accumulated call meter maximum(ACMMAX) set or query", NULL},
	{AT_M10_CAOC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CAOC",  {"+CAOC:", ""}, "Advice of charge", NULL},
	{AT_M10_CBST, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CBST",  {"+CBST:", ""}, "Select bearer service type", NULL},
	{AT_M10_CCFC, AT_OPER_TEST|AT_OPER_WRITE, "AT+CCFC",  {"+CCFC:", ""}, "Call forwarding number and conditions control", NULL},
	{AT_M10_CCUG, AT_OPER_READ|AT_OPER_WRITE, "AT+CCUG",  {"+CCUG:", ""}, "Closed user group control", NULL},
	{AT_M10_CCWA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCWA",  {"+CCWA:", ""}, "Call waiting control", NULL},
	{AT_M10_CEER, AT_OPER_EXEC|AT_OPER_TEST, "AT+CEER",  {"+CEER:", ""}, "Extended error report", NULL},
	{AT_M10_CGMI, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMI",  {"", ""}, "Request manufacturer identification", is_str_printable},
	{AT_M10_CGMM, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMM",  {"", ""}, "Request model identification", is_str_printable},
	{AT_M10_CGMR, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMR",  {"", ""}, "Request TA revision identification of software release", is_str_printable},
	{AT_M10_CGSN, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGSN",  {"", ""}, "Request product serial number identification (identical with +GSN)", is_str_digit},
	{AT_M10_CSCS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCS",  {"+CSCS:", ""}, "Select TE character set", NULL},
	{AT_M10_CSTA, AT_OPER_TEST|AT_OPER_READ, "AT+CSTA",  {"+CSTA:", ""}, "Select type of address", NULL},
	{AT_M10_CHLD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CHLD",  {"+CHLD:", ""}, "Call hold and multiparty", NULL},
	{AT_M10_CIMI, AT_OPER_EXEC|AT_OPER_TEST, "AT+CIMI",  {"", ""}, "Request international mobile subscriber identity", is_str_digit},
	{AT_M10_CKPD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CKPD",  {"", ""}, "Keypad control", NULL},
	{AT_M10_CLCC, AT_OPER_EXEC|AT_OPER_TEST, "AT+CLCC",  {"+CLCC:", ""}, "List current calls of ME", NULL},
	{AT_M10_CLCK, AT_OPER_TEST|AT_OPER_WRITE, "AT+CLCK",  {"+CLCK:", ""}, "Facility lock", NULL},
	{AT_M10_CLIP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLIP",  {"+CLIP:", ""}, "Calling line identification presentation", NULL},
	{AT_M10_CLIR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLIR",  {"+CLIR:", ""}, "Calling line identification restriction", NULL},
	{AT_M10_CMEE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMEE",  {"+CMEE:", ""}, "Report mobile equipment error", NULL},
	{AT_M10_COLP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+COLP",  {"+COLP:", ""}, "Connected line identification presentation", NULL},
	{AT_M10_COPS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+COPS",  {"+COPS:", ""}, "Operator selection", NULL},
	{AT_M10_CPAS, AT_OPER_EXEC|AT_OPER_TEST, "AT+CPAS",  {"+CPAS:", ""}, "Mobile equipment activity status", NULL},
	{AT_M10_CPBF, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBF",  {"+CPBF:", ""}, "Find phonebook entries", NULL},
	{AT_M10_CPBR, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBR",  {"+CPBR:", ""}, "Read current phonebook entries", NULL},
	{AT_M10_CPBS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPBS",  {"+CPBS:", ""}, "Select phonebook memory storage", NULL},
	{AT_M10_CPBW, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBW",  {"+CPBW:", ""}, "Write phonebook entry", NULL},
	{AT_M10_CPIN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPIN",  {"+CPIN:", ""}, "Enter PIN", NULL},
	{AT_M10_CPWD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPWD",  {"+CPWD:", ""}, "Change password", NULL},
	{AT_M10_CR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CR",  {"+CR:", ""}, "Service reporting control", NULL},
	{AT_M10_CRC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRC",  {"+CRC:", ""}, "Set cellular result codes for incoming call indication", NULL},
	{AT_M10_CREG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CREG",  {"+CREG:", ""}, "Network registration", NULL},
	{AT_M10_CRLP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRLP",  {"+CRLP:", ""}, "Select radio link protocol parameter", NULL},
	{AT_M10_CRSM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRSM",  {"+CRSM:", ""}, "Restricted SIM access", NULL},
	{AT_M10_CSQ, AT_OPER_TEST|AT_OPER_EXEC, "AT+CSQ",  {"+CSQ:", ""}, "Signal quality report", NULL},
	{AT_M10_FCLASS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+FCLASS",  {"+FCLASS:", ""}, "Fax: select, read or test service class", NULL},
	{AT_M10_VTD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+VTD",  {"+VTD:", ""}, "Tone duration", NULL},
	{AT_M10_VTS, AT_OPER_TEST|AT_OPER_WRITE, "AT+VTS",  {"+VTS:", ""}, "DTMF and tone generation", NULL},
	{AT_M10_CMUX, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMUX",  {"+CMUX:", ""}, "Multiplexer control", NULL},
	{AT_M10_CNUM, AT_OPER_TEST|AT_OPER_EXEC, "AT+CNUM",  {"+CNUM:", ""}, "Subscriber number", NULL},
	{AT_M10_CPOL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPOL",  {"+CPOL:", ""}, "Preferred operator list", NULL},
	{AT_M10_COPN, AT_OPER_TEST|AT_OPER_EXEC, "AT+COPN",  {"+COPN:", ""}, "Read operator names", NULL},
	{AT_M10_CFUN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CFUN",  {"+CFUN:", ""}, "Set phone functionality", NULL},
	{AT_M10_CCLK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCLK",  {"+CCLK:", ""}, "Clock", NULL},
	{AT_M10_CSIM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CSIM",  {"+CSIM:", ""}, "Generic SIM access", NULL},
	{AT_M10_CALM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CALM",  {"+CALM:", ""}, "Alert sound mode", NULL},
	{AT_M10_CRSL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRSL",  {"+CRSL:", ""}, "Ringer sound level", NULL},
	{AT_M10_CLVL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLVL",  {"+CLVL:", ""}, "Loud speaker volume level", NULL},
	{AT_M10_CMUT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMUT",  {"+CMUT:", ""}, "Mute control", NULL},
	{AT_M10_CPUC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPUC",  {"+CPUC:", ""}, "Price per unit currency table", NULL},
	{AT_M10_CCWE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCWE",  {"+CCWE:", ""}, "Call meter maximum event", NULL},
	{AT_M10_CBC, AT_OPER_TEST|AT_OPER_EXEC, "AT+CBC",  {"+CBC:", ""}, "Battery charge", NULL},
	{AT_M10_CUSD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CUSD",  {"+CUSD:", ""}, "Unstructured supplementary service data", NULL},
	{AT_M10_CSSN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSSN",  {"+CSSN:", ""}, "Supplementary services notification", NULL},
	{AT_M10_CSNS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSNS",  {"+CSNS:", ""}, "Single numbering scheme", NULL},
	{AT_M10_CMOD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMOD",  {"+CMOD:", ""}, "Configure alternating mode calls", NULL},

	// M10 GSM07.05 V1.04
	{AT_M10_CMGD, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGD",  {"+CMGD:", ""}, "Delete SMS message", NULL},
	{AT_M10_CMGF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMGF",  {"+CMGF:", ""}, "Select SMS message format", NULL},
	{AT_M10_CMGL, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGL",  {"", ""}, "List SMS messages from preferred store", is_str_xdigit},
	{AT_M10_CMGR, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGR",  {"+CMGR:", ""}, "Read SMS message", is_str_xdigit},
	{AT_M10_CMGS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGS",  {"+CMGS:", ""}, "Send SMS message", NULL},
	{AT_M10_CMGW, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGW",  {"+CMGW:", ""}, "Write SMS message to memory", NULL},
	{AT_M10_CMSS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMSS",  {"+CMSS:", ""}, "Send SMS message from storage", NULL},
	{AT_M10_CMGC, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGC",  {"+CMGC:", ""}, "Send sms command", NULL},
	{AT_M10_CNMI, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CNMI",  {"+CNMI:", ""}, "New SMS message indications", NULL},
	{AT_M10_CPMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPMS",  {"+CPMS:", ""}, "Preferred SMS message storage", NULL},
	{AT_M10_CRES, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRES",  {"+CRES:", ""}, "Restore SMS settings", NULL},
	{AT_M10_CSAS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CSAS",  {"+CSAS:", ""}, "Save SMS settings", NULL},
	{AT_M10_CSCA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCA",  {"+CSCA:", ""}, "SMS service center address", NULL},
	{AT_M10_CSCB, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCB",  {"+CSCB:", ""}, "Select cell broadcast SMS messages", NULL},
	{AT_M10_CSDH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSDH",  {"+CSDH:", ""}, "Show SMS text mode parameters", NULL},
	{AT_M10_CSMP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMP",  {"+CSMP:", ""}, "Set SMS text mode parameters", NULL},
	{AT_M10_CSMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMS",  {"+CSMS:", ""}, "Select message service", NULL},

	// M10 Quectel V1.04
	{AT_M10_QECHO, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QECHO",  {"+QECHO:", ""}, "Echo cancellation control", NULL},
	{AT_M10_QSIDET, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QSIDET",  {"+QSIDET", ""}, "Change the side tone gain level", NULL}, // without semicolon
	{AT_M10_QPOWD, AT_OPER_WRITE, "AT+QPOWD",  {"", ""}, "Power off", NULL},
	{AT_M10_QTRPIN, AT_OPER_EXEC, "AT+QTRPIN",  {"+QTRPIN:", ""}, "Times remain to input SIM PIN/PUK", NULL},
	{AT_M10_QMIC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QMIC",  {"+QMIC:", ""}, "Change the microphone gain level", NULL},
	{AT_M10_QALARM, AT_OPER_TEST|AT_OPER_WRITE, "AT+QALARM",  {"+QALARM:", ""}, "Set alarm", NULL},
	{AT_M10_QADC, AT_OPER_TEST|AT_OPER_READ, "AT+QADC",  {"+QADC:", ""}, "Read ADC", NULL},
	{AT_M10_QRSTCB, AT_OPER_EXEC, "AT+QRSTCB",  {"", ""}, "Reset cell broadcast", NULL},
	{AT_M10_QINDRI, AT_OPER_READ|AT_OPER_WRITE, "AT+QINDRI",  {"+QINDRI:", ""}, "Indicate RI when using URC", NULL},
	{AT_M10_QEXTHS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QEXTHS",  {"+QEXTHS:", ""}, "External headset jack control", NULL},
	{AT_M10_QHSBTN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QHSBTN",  {"+QHSBTN:", ""}, "Headset button status reporting", NULL},
	{AT_M10_QSIMSTAT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QSIMSTAT",  {"+QSIMSTAT:", ""}, "SIM inserted status reporting", NULL},
	{AT_M10_QLDTMF, AT_OPER_EXEC|AT_OPER_WRITE, "AT+QLDTMF",  {"", ""}, "Generate local DTMF tone", NULL},
	{AT_M10_QCGTIND, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QCGTIND",  {"+QCGTIND:", ""}, "Circuit switched call or GPRS PDP context termination indication", NULL},
	{AT_M10_QSPN, AT_OPER_READ, "AT+QSPN",  {"+QSPN:", ""}, "Get service provider name from SIM", NULL},
	{AT_M10_QBAND, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QBAND",  {"+QBAND:", ""}, "Get and set mobile operation band", NULL},
	{AT_M10_QAUDCH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QAUDCH",  {"+QAUDCH:", ""}, "Swap the audio channels", NULL},
	{AT_M10_QSCLK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QSCLK",  {"+QSCLK:", ""}, "Configure slow clock", NULL},
	{AT_M10_QENG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QENG",  {"+QENG:", ""}, "Report cell description in engineering mode", NULL},
	{AT_M10_QCLASS0, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QCLASS0",  {"+QCLASS0:", ""}, "Store Class 0 SMS to SIM when received Class 0 SMS", NULL},
	{AT_M10_QCCID, AT_OPER_EXEC|AT_OPER_TEST, "AT+QCCID",  {"", ""}, "Show ICCID", is_str_xdigit},
	{AT_M10_QTEMP, AT_OPER_READ|AT_OPER_WRITE, "AT+QTEMP",  {"+QTEMP:", ""}, "Set critical temperature operating mode or query temperature", NULL},
	{AT_M10_QSIMDET, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QSIMDET",  {"+QSIMDET:", ""}, "Switch on or off detecting SIM card", NULL},
	{AT_M10_QMGDA, AT_OPER_TEST|AT_OPER_WRITE, "AT+QMGDA",  {"+QMGDA:", ""}, "Delete all SMS", NULL},
	{AT_M10_QLTONE, AT_OPER_TEST|AT_OPER_WRITE, "AT+QLTONE",  {"+QLTONE:", ""}, "Generate local specific tone", NULL},
	{AT_M10_QCLIP, AT_OPER_READ|AT_OPER_WRITE, "AT+QCLIP",  {"+QCLIP:", ""}, "Connected line identification presentation without alpha string", NULL},
	{AT_M10_QGID, AT_OPER_EXEC, "AT+QGID",  {"+QGID:", ""}, "Get SIM card group identifier", NULL},
	{AT_M10_QMOSTAT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QMOSTAT",  {"+QMOSTAT:", ""}, "Show state of mobile originated call", NULL},
	{AT_M10_QGPCLASS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QGPCLASS",  {"", ""}, "Change GPRS multi-slot class", is_str_printable},
	{AT_M10_QMGHEX, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QMGHEX",  {"+QMGHEX:", ""}, "Enable to send non-ASCII character SMS", NULL},
	{AT_M10_QAUDLOOP, AT_OPER_TEST|AT_OPER_WRITE, "AT+QAUDLOOP",  {"+QAUDLOOP:", ""}, "Audio channel loop back test", NULL},
	{AT_M10_QSMSCODE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QSMSCODE",  {"+QSMSCODE:", ""}, "Configure SMS code mode", NULL},
	{AT_M10_QIURC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QIURC",  {"+QIURC:", ""}, "Enable or disable initial URC presentation", NULL},
	{AT_M10_QCSPWD, AT_OPER_WRITE, "AT+QCSPWD",  {"", ""}, "Change PS super password", NULL},
	{AT_M10_QEXTUNSOL, AT_OPER_TEST|AT_OPER_WRITE, "AT+QEXTUNSOL",  {"+QEXTUNSOL:", ""}, "Enable/disable proprietary unsolicited indication", NULL},
	{AT_M10_QSFR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QSFR",  {"+QSFR:", ""}, "Prefernce speech coding", NULL},
	{AT_M10_QSPCH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QSPCH",  {"+QSPCH:", ""}, "Speech channel type report", NULL},
	{AT_M10_QSCANF, AT_OPER_TEST|AT_OPER_WRITE, "AT+QSCANF",  {"+QSCANF:", ""}, "Scan power of GSM frequency", NULL},
	{AT_M10_QLOCKF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QLOCKF",  {"+QLOCKF:", ""}, "Lock GSM frequency", NULL},
	{AT_M10_QGPIO, AT_OPER_TEST|AT_OPER_WRITE, "AT+QGPIO",  {"+QGPIO:", ""}, "Configure GPIO pin", NULL},
	{AT_M10_QINISTAT, AT_OPER_EXEC|AT_OPER_TEST, "AT+QINISTAT",  {"+QINISTAT:", ""}, "Query state of initialization", NULL},
	{AT_M10_QFGR, AT_OPER_TEST|AT_OPER_WRITE, "AT+QFGR",  {"+QFGR:", ""}, "Read customer file", NULL},
	{AT_M10_QFGW, AT_OPER_TEST|AT_OPER_WRITE, "AT+QFGW",  {"", ""}, "Write customer file", NULL},
	{AT_M10_QFGL, AT_OPER_TEST|AT_OPER_WRITE, "AT+QFGL",  {"+QFGL:", ""}, "List customer files", NULL},
	{AT_M10_QFGD, AT_OPER_TEST|AT_OPER_WRITE, "AT+QFGD",  {"", ""}, "Delete customer file", NULL},
	{AT_M10_QFGM, AT_OPER_TEST|AT_OPER_WRITE, "AT+QFGM",  {"+QFGM:", ""}, "Query free space for customer files", NULL},
	{AT_M10_QSRT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QSRT",  {"+QSRT:", ""}, "Select ring tone", NULL},
	{AT_M10_QNSTATUS, AT_OPER_EXEC|AT_OPER_TEST, "AT+QNSTATUS",  {"+QNSTATUS:", ""}, "Query GSM network status", NULL},
	{AT_M10_QECHOEX, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+QECHOEX",  {"+QECHOEX:", ""}, "Extended echo cancellation control", NULL},
	{AT_M10_EGPAU, AT_OPER_EXEC|AT_OPER_TEST, "AT+EGPAU",  {"+EGPAU:", ""}, "PPP authentication", NULL},
	{AT_M10_QNITZ, AT_OPER_TEST|AT_OPER_WRITE, "AT+QNITZ",  {"+QNITZ:", ""}, "Network time synchronization", NULL},
	{AT_M10_EGMR, AT_OPER_TEST|AT_OPER_WRITE, "AT+EGMR",  {"+EGMR:", ""}, "Set/Inquiry product serial number identification", NULL},
	};
//------------------------------------------------------------------------------

//==============================================================================
// begin parser response function section

//------------------------------------------------------------------------------
// at_m10_qsimstat_read_parse()
//------------------------------------------------------------------------------
int at_m10_qsimstat_read_parse(const char *fld, int fld_len, struct at_m10_qsimstat_read *qsimstat){

	char *sp;
	char *tp;
	char *ep;

#define MAX_QSIMSTAT_READ_PARAM 2
	struct parsing_param params[MAX_QSIMSTAT_READ_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!qsimstat) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_QSIMSTAT_READ_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_m10_qsimstat_read
	qsimstat->n = -1;
	qsimstat->sim_inserted = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) || (param_cnt < MAX_QSIMSTAT_READ_PARAM)){
		// get param type
		if(*tp == '"'){
			params[param_cnt].type = PRM_TYPE_STRING;
			params[param_cnt].buf = ++tp;
			}
		else if(isdigit(*tp)){
			params[param_cnt].type = PRM_TYPE_INTEGER;
			params[param_cnt].buf = tp;
			}
		else{
			params[param_cnt].type = PRM_TYPE_UNKNOWN;
			params[param_cnt].buf = tp;
			}
		sp = tp;
		// search delimiter and put terminated null-symbol
		if(!(tp = strchr(sp, ',')))
			tp = ep;
		*tp = '\0';
		// set param len
		if(params[param_cnt].type == PRM_TYPE_STRING){
			params[param_cnt].len = tp - sp - 1;
			*(tp-1) = '\0';
			}
		else{
			params[param_cnt].len = tp - sp;
			}
		//
		param_cnt++;
		tp++;
		}

	// processing integer params
	// n (mandatory)
	if(params[0].len > 0){
		tp = params[0].buf;
		while(params[0].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		qsimstat->n = atoi(params[0].buf);
		}
	else
		return -1;

	// sim_inserted (mandatory)
	if(params[1].len > 0){
		tp = params[1].buf;
		while(params[1].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		qsimstat->sim_inserted = atoi(params[1].buf);
		}
	else
		return -1;

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_m10_qsimstat_read_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_m10_parse_qmic_read_parse()
//------------------------------------------------------------------------------
int at_m10_qmic_read_parse(const char *fld, int fld_len, struct at_m10_qmic_read *qmic){

	char *sp;
	char *tp;
	char *ep;

#define MAX_QMIC_READ_PARAM 3
	struct parsing_param params[MAX_QMIC_READ_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!qmic) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_QMIC_READ_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_m10_qmic_read
	qmic->normal_mic = -1;
	qmic->headset_mic = -1;
	qmic->loudspeaker_mic = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt < MAX_QMIC_READ_PARAM)){
		// get param type
		if(*tp == '"'){
			params[param_cnt].type = PRM_TYPE_STRING;
			params[param_cnt].buf = ++tp;
			}
		else if(isdigit(*tp)){
			params[param_cnt].type = PRM_TYPE_INTEGER;
			params[param_cnt].buf = tp;
			}
		else{
			params[param_cnt].type = PRM_TYPE_UNKNOWN;
			params[param_cnt].buf = tp;
			}
		sp = tp;
		// search delimiter and put terminated null-symbol
		if(!(tp = strchr(sp, ',')))
			tp = ep;
		*tp = '\0';
		// set param len
		if(params[param_cnt].type == PRM_TYPE_STRING){
			params[param_cnt].len = tp - sp - 1;
			*(tp-1) = '\0';
			}
		else{
			params[param_cnt].len = tp - sp;
			}
		//
		param_cnt++;
		tp++;
		}

	// processing integer params
	// Normal microphone gain level
	if(params[0].len > 0){
		tp = params[0].buf;
		while(params[0].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		qmic->normal_mic = atoi(params[0].buf);
		}
	else
		return -1;

	// Headset microphone gain level
	if(params[1].len > 0){
		tp = params[1].buf;
		while(params[1].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		qmic->headset_mic = atoi(params[1].buf);
		}
	else
		return -1;

	// Louspeaker microphone gain level
	if(params[2].len > 0){
		tp = params[2].buf;
		while(params[2].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		qmic->loudspeaker_mic = atoi(params[2].buf);
		}
	else
		return -1;

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_m10_qmic_read_parse()
//------------------------------------------------------------------------------

// end of parser function section
//==============================================================================

/******************************************************************************/
/* end of m10.c                                                               */
/******************************************************************************/
