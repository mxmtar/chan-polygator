/******************************************************************************/
/* sim300.c                                                                   */
/******************************************************************************/

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "at.h"
#include "sim300.h"
#include "strutil.h"
//------------------------------------------------------------------------------

const struct at_command sim300_at_com_list[AT_SIM300_MAXNUM] = {
	// int id; u_int32_t operations; char name[16]; char response[MAX_AT_CMD_RESP][16]; char description[256]; add_check_at_resp_fun_t *check_fun;
	{AT_SIM300_UNKNOWN, AT_OPER_EXEC, "", {"", ""}, "", is_str_printable},
	// SIM300 V.25TER V1.06
	{AT_SIM300_A_SLASH, AT_OPER_EXEC, "A/", {"", ""}, "Re-issues last AT command given", NULL},
	{AT_SIM300_A, AT_OPER_EXEC, "ATA", {"", ""}, "Answer an incoming call", NULL},
	{AT_SIM300_D, AT_OPER_EXEC, "ATD", {"", ""}, "Mobile originated call to dial a number", NULL},
	{AT_SIM300_D_CURMEM, AT_OPER_EXEC, "ATD", {"", ""}, "Originate call to phone number in current memory", NULL},
	{AT_SIM300_D_PHBOOK, AT_OPER_EXEC, "ATD", {"", ""}, "Originate call to phone number in memory which corresponds to field <STR>", NULL},
	{AT_SIM300_DL, AT_OPER_EXEC, "ATDL",  {"", ""}, "Redial last telephone number used", NULL},
	{AT_SIM300_E, AT_OPER_EXEC, "ATE",  {"", ""}, "Set command echo mode", NULL},
	{AT_SIM300_H, AT_OPER_EXEC, "ATH",  {"", ""}, "Disconnect existing connection", NULL},
	{AT_SIM300_I, AT_OPER_EXEC, "ATI",  {"", ""}, "Display product identification information", NULL},
	{AT_SIM300_L, AT_OPER_EXEC, "ATL",  {"", ""}, "Set monitor speaker loudness", NULL},
	{AT_SIM300_M, AT_OPER_EXEC, "ATM",  {"", ""}, "Set monitor speaker mode", NULL},
	{AT_SIM300_3PLUS, AT_OPER_EXEC, "+++",  {"", ""}, "Switch from data mode or PPP online mode to command mode", NULL},
	{AT_SIM300_O, AT_OPER_EXEC, "ATO",  {"", ""}, "Switch from command mode to data mode", NULL},
	{AT_SIM300_P, AT_OPER_EXEC, "ATP",  {"", ""}, "Select pulse dialling", NULL},
	{AT_SIM300_Q, AT_OPER_EXEC, "ATQ",  {"", ""}, "Set result code presentation mode", NULL},
	{AT_SIM300_S0, AT_OPER_READ|AT_OPER_WRITE, "ATS0",  {"", ""}, "Set number of rings before automatically answering the call", NULL},
	{AT_SIM300_S3, AT_OPER_READ|AT_OPER_WRITE, "ATS3",  {"", ""}, "Set command line termination character", NULL},
	{AT_SIM300_S4, AT_OPER_READ|AT_OPER_WRITE, "ATS4",  {"", ""}, "Set response formatting character", NULL},
	{AT_SIM300_S5, AT_OPER_READ|AT_OPER_WRITE, "ATS5",  {"", ""}, "Set command line editing character", NULL},
	{AT_SIM300_S6, AT_OPER_READ|AT_OPER_WRITE, "ATS6",  {"", ""}, "Set pause before blind dialling", NULL},
	{AT_SIM300_S7, AT_OPER_READ|AT_OPER_WRITE, "ATS7",  {"", ""}, "Set number of seconds to wait for connection completion", NULL},
	{AT_SIM300_S8, AT_OPER_READ|AT_OPER_WRITE, "ATS8",  {"", ""}, "Set number of seconds to wait when comma dial modifier used", NULL},
	{AT_SIM300_S10, AT_OPER_READ|AT_OPER_WRITE, "ATS10",  {"", ""}, "Set disconnect delay after indicating the absence of data carrier", NULL},
	{AT_SIM300_T, AT_OPER_EXEC, "ATT",  {"", ""}, "Select tone dialling", NULL},
	{AT_SIM300_V, AT_OPER_EXEC, "ATV",  {"", ""}, "Set result code format mode", NULL},
	{AT_SIM300_X, AT_OPER_EXEC, "ATX",  {"", ""}, "Set connect result code format and monitor call progress", NULL},
	{AT_SIM300_Z, AT_OPER_EXEC, "ATZ",  {"", ""}, "Set all current parameters to user defined profile", NULL},
	{AT_SIM300_andC, AT_OPER_EXEC, "AT&C",  {"", ""}, "Set DCD function mode", NULL},
	{AT_SIM300_andD, AT_OPER_EXEC, "AT&D",  {"", ""}, "Set DTR function mode", NULL},
	{AT_SIM300_andF, AT_OPER_EXEC, "AT&F",  {"", ""}, "Set all current parameters to manufacturer defaults", NULL},
	{AT_SIM300_andV, AT_OPER_EXEC, "AT&V",  {"", ""}, "Display current configuration", NULL},
	{AT_SIM300_andW, AT_OPER_EXEC, "AT&W",  {"", ""}, "Store current parameter to user defined profile", NULL},
	{AT_SIM300_DR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+DR",  {"+DR:", ""}, "V.42bis data compression reporting control", NULL},
	{AT_SIM300_DS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+DS",  {"+DS:", ""}, "V.42bis data compression control", NULL},
	{AT_SIM300_GCAP, AT_OPER_TEST|AT_OPER_EXEC, "AT+GCAP",  {"+GCAP:", ""}, "Request complete TA capabilities list", NULL},
	{AT_SIM300_GMI, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMI",  {"", ""}, "Request manufacturer identification", is_str_printable},
	{AT_SIM300_GMM, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMM",  {"", ""}, "Request TA model identification", is_str_printable},
	{AT_SIM300_GMR, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMR",  {"", ""}, "Request TA revision indentification of software release", is_str_printable},
	{AT_SIM300_GOI, AT_OPER_TEST|AT_OPER_EXEC, "AT+GOI",  {"", ""}, "Request global object identification", is_str_printable},
	{AT_SIM300_GSN, AT_OPER_TEST|AT_OPER_EXEC, "AT+GSN",  {"", ""}, "Request ta serial number identification (IMEI)", is_str_digit},
	{AT_SIM300_ICF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+ICF",  {"+ICF:", ""}, "Set TE-TA control character framing", NULL},
	{AT_SIM300_IFC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+IFC",  {"+IFC:", ""}, "Set TE-TA local data flow control", NULL},
	{AT_SIM300_ILRR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+ILRR",  {"+ILRR:", ""}, "Set TE-TA local rate reporting mode", NULL},
	{AT_SIM300_IPR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+IPR",  {"+IPR:", ""}, "Set TE-TA fixed local rate", NULL},

	// SIM300 GSM07.07 V1.06
	{AT_SIM300_CACM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CACM",  {"+CACM:", ""}, "Accumulated call meter(ACM) reset or query", NULL},
	{AT_SIM300_CAMM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CAMM",  {"+CAMM:", ""}, "Accumulated call meter maximum(ACMMAX) set or query", NULL},
	{AT_SIM300_CAOC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CAOC",  {"+CAOC:", ""}, "Advice of charge", NULL},
	{AT_SIM300_CBST, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CBST",  {"+CBST:", ""}, "Select bearer service type", NULL},
	{AT_SIM300_CCFC, AT_OPER_TEST|AT_OPER_WRITE, "AT+CCFC",  {"+CCFC:", ""}, "Call forwarding number and conditions control", NULL},
	{AT_SIM300_CCUG, AT_OPER_READ|AT_OPER_WRITE, "AT+CCUG",  {"+CCUG:", ""}, "Closed user group control", NULL},
	{AT_SIM300_CCWA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCWA",  {"+CCWA:", ""}, "Call waiting control", NULL},
	{AT_SIM300_CEER, AT_OPER_EXEC|AT_OPER_TEST, "AT+CEER",  {"+CEER:", ""}, "Extended error report", NULL},
	{AT_SIM300_CGMI, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMI",  {"", ""}, "Request manufacturer identification", is_str_printable},
	{AT_SIM300_CGMM, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMM",  {"", ""}, "Request model identification", is_str_printable},
	{AT_SIM300_CGMR, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMR",  {"", ""}, "Request TA revision identification of software release", is_str_printable},
	{AT_SIM300_CGSN, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGSN",  {"", ""}, "Request product serial number identification (identical with +GSN)", is_str_digit},
	{AT_SIM300_CSCS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCS",  {"+CSCS:", ""}, "Select TE character set", NULL},
	{AT_SIM300_CSTA, AT_OPER_TEST|AT_OPER_READ, "AT+CSTA",  {"+CSTA:", ""}, "Select type of address", NULL},
	{AT_SIM300_CHLD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CHLD",  {"+CHLD:", ""}, "Call hold and multiparty", NULL},
	{AT_SIM300_CIMI, AT_OPER_EXEC|AT_OPER_TEST, "AT+CIMI",  {"", ""}, "Request international mobile subscriber identity", is_str_digit},
	{AT_SIM300_CKPD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CKPD",  {"", ""}, "Keypad control", NULL},
	{AT_SIM300_CLCC, AT_OPER_EXEC|AT_OPER_TEST, "AT+CLCC",  {"+CLCC:", ""}, "List current calls of ME", NULL},
	{AT_SIM300_CLCK, AT_OPER_TEST|AT_OPER_WRITE, "AT+CLCK",  {"+CLCK:", ""}, "Facility lock", NULL},
	{AT_SIM300_CLIP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLIP",  {"+CLIP:", ""}, "Calling line identification presentation", NULL},
	{AT_SIM300_CLIR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLIR",  {"+CLIR:", ""}, "Calling line identification restriction", NULL},
	{AT_SIM300_CMEE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMEE",  {"+CMEE:", ""}, "Report mobile equipment error", NULL},
	{AT_SIM300_COLP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+COLP",  {"+COLP:", ""}, "Connected line identification presentation", NULL},
	{AT_SIM300_COPS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+COPS",  {"+COPS:", ""}, "Operator selection", NULL},
	{AT_SIM300_CPAS, AT_OPER_EXEC|AT_OPER_TEST, "AT+CPAS",  {"+CPAS:", ""}, "Mobile equipment activity status", NULL},
	{AT_SIM300_CPBF, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBF",  {"+CPBF:", ""}, "Find phonebook entries", NULL},
	{AT_SIM300_CPBR, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBR",  {"+CPBR:", ""}, "Read current phonebook entries", NULL},
	{AT_SIM300_CPBS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPBS",  {"+CPBS:", ""}, "Select phonebook memory storage", NULL},
	{AT_SIM300_CPBW, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBW",  {"+CPBW:", ""}, "Write phonebook entry", NULL},
	{AT_SIM300_CPIN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPIN",  {"+CPIN:", ""}, "Enter PIN", NULL},
	{AT_SIM300_CPWD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPWD",  {"+CPWD:", ""}, "Change password", NULL},
	{AT_SIM300_CR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CR",  {"+CR:", ""}, "Service reporting control", NULL},
	{AT_SIM300_CRC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRC",  {"+CRC:", ""}, "Set cellular result codes for incoming call indication", NULL},
	{AT_SIM300_CREG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CREG",  {"+CREG:", ""}, "Network registration", NULL},
	{AT_SIM300_CRLP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRLP",  {"+CRLP:", ""}, "Select radio link protocol parameter", NULL},
	{AT_SIM300_CRSM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRSM",  {"+CRSM:", ""}, "Restricted SIM access", NULL},
	{AT_SIM300_CSQ, AT_OPER_TEST|AT_OPER_EXEC, "AT+CSQ",  {"+CSQ:", ""}, "Signal quality report", NULL},
	{AT_SIM300_FCLASS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+FCLASS",  {"+FCLASS:", ""}, "Fax: select, read or test service class", NULL},
	{AT_SIM300_FMI, AT_OPER_TEST|AT_OPER_READ, "AT+FMI",  {"", ""}, "Fax: report manufactured ID", is_str_printable},
	{AT_SIM300_FMM, AT_OPER_TEST|AT_OPER_READ, "AT+FMM",  {"", ""}, "Fax: report model ID", is_str_printable},
	{AT_SIM300_FMR, AT_OPER_TEST|AT_OPER_EXEC, "AT+FMR",  {"", ""}, "Fax: report revision ID", is_str_printable},
	{AT_SIM300_VTD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+VTD",  {"+VTD:", ""}, "Tone duration", NULL},
	{AT_SIM300_VTS, AT_OPER_TEST|AT_OPER_WRITE, "AT+VTS",  {"+VTS:", ""}, "DTMF and tone generation", NULL},
	{AT_SIM300_CMUX, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMUX",  {"+CMUX:", ""}, "Multiplexer control", NULL},
	{AT_SIM300_CNUM, AT_OPER_TEST|AT_OPER_EXEC, "AT+CNUM",  {"+CNUM:", ""}, "Subscriber number", NULL},
	{AT_SIM300_CPOL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPOL",  {"+CPOL:", ""}, "Preferred operator list", NULL},
	{AT_SIM300_COPN, AT_OPER_TEST|AT_OPER_EXEC, "AT+COPN",  {"+COPN:", ""}, "Read operator names", NULL},
	{AT_SIM300_CFUN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CFUN",  {"+CFUN:", ""}, "Set phone functionality", NULL},
	{AT_SIM300_CCLK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCLK",  {"+CCLK:", ""}, "Clock", NULL},
	{AT_SIM300_CSIM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CSIM",  {"+CSIM:", ""}, "Generic SIM access", NULL},
	{AT_SIM300_CALM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CALM",  {"+CALM:", ""}, "Alert sound mode", NULL},
	{AT_SIM300_CRSL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRSL",  {"+CRSL:", ""}, "Ringer sound level", NULL},
	{AT_SIM300_CLVL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLVL",  {"+CLVL:", ""}, "Loud speaker volume level", NULL},
	{AT_SIM300_CMUT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMUT",  {"+CMUT:", ""}, "Mute control", NULL},
	{AT_SIM300_CPUC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPUC",  {"+CPUC:", ""}, "Price per unit currency table", NULL},
	{AT_SIM300_CCWE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCWE",  {"+CCWE:", ""}, "Call meter maximum event", NULL},
	{AT_SIM300_CBC, AT_OPER_TEST|AT_OPER_EXEC, "AT+CBC",  {"+CBC:", ""}, "Battery charge", NULL},
	{AT_SIM300_CUSD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CUSD",  {"+CUSD:", ""}, "Unstructured supplementary service data", NULL},
	{AT_SIM300_CSSN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSSN",  {"+CSSN:", ""}, "Supplementary services notification", NULL},
	{AT_SIM300_CSNS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSNS",  {"+CSNS:", ""}, "Single numbering scheme", NULL},
	{AT_SIM300_CMOD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMOD",  {"+CMOD:", ""}, "Configure alternating mode calls", NULL},

	// SIM300 GSM07.05 V1.06
	{AT_SIM300_CMGD, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGD",  {"+CMGD:", ""}, "Delete SMS message", NULL},
	{AT_SIM300_CMGF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMGF",  {"+CMGF:", ""}, "Select SMS message format", NULL},
	{AT_SIM300_CMGL, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGL",  {"", ""}, "List SMS messages from preferred store", is_str_xdigit},
	{AT_SIM300_CMGR, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGR",  {"+CMGR:", ""}, "Read SMS message", is_str_xdigit},
	{AT_SIM300_CMGS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGS",  {"+CMGS:", ""}, "Send SMS message", NULL},
	{AT_SIM300_CMGW, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGW",  {"+CMGW:", ""}, "Write SMS message to memory", NULL},
	{AT_SIM300_CMSS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMSS",  {"+CMSS:", ""}, "Send SMS message from storage", NULL},
	{AT_SIM300_CMGC, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGC",  {"+CMGC:", ""}, "Send sms command", NULL},
	{AT_SIM300_CNMI, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CNMI",  {"+CNMI:", ""}, "New SMS message indications", NULL},
	{AT_SIM300_CPMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPMS",  {"+CPMS:", ""}, "Preferred SMS message storage", NULL},
	{AT_SIM300_CRES, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRES",  {"+CRES:", ""}, "Restore SMS settings", NULL},
	{AT_SIM300_CSAS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CSAS",  {"+CSAS:", ""}, "Save SMS settings", NULL},
	{AT_SIM300_CSCA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCA",  {"+CSCA:", ""}, "SMS service center address", NULL},
	{AT_SIM300_CSCB, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCB",  {"+CSCB:", ""}, "Select cell broadcast SMS messages", NULL},
	{AT_SIM300_CSDH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSDH",  {"+CSDH:", ""}, "Show SMS text mode parameters", NULL},
	{AT_SIM300_CSMP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMP",  {"+CSMP:", ""}, "Set SMS text mode parameters", NULL},
	{AT_SIM300_CSMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMS",  {"+CSMS:", ""}, "Select message service", NULL},

	// SIM300 SIMCOM V1.06
	{AT_SIM300_ECHO, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+ECHO",  {"+ECHO:", ""}, "Echo cancellation control", NULL},
	{AT_SIM300_SIDET, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+SIDET",  {"+SIDET:", ""}, "Change the side tone gain level", NULL},
	{AT_SIM300_CPOWD, AT_OPER_WRITE, "AT+CPOWD",  {"", ""}, "Power off", NULL},
	{AT_SIM300_SPIC, AT_OPER_EXEC, "AT+SPIC",  {"+SPIC:", ""}, "Times remain to input SIM PIN/PUK", NULL},
	{AT_SIM300_CMIC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMIC",  {"+CMIC:", ""}, "Change the micophone gain level", NULL},
	{AT_SIM300_UART, AT_OPER_READ|AT_OPER_WRITE, "AT+UART",  {"+UART:", ""}, "Configure dual serial port mode", NULL},
	{AT_SIM300_CALARM, AT_OPER_READ|AT_OPER_WRITE, "AT+CALARM",  {"+CALARM:", ""}, "Set alarm", NULL},
	{AT_SIM300_CADC, AT_OPER_TEST|AT_OPER_READ, "AT+CADC",  {"+CADC:", ""}, "Read adc", NULL},
	{AT_SIM300_CSNS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSNS",  {"+CSNS:", ""}, "Single numbering scheme", NULL},
	{AT_SIM300_CDSCB, AT_OPER_EXEC, "AT+CDSCB",  {"", ""}, "Reset cellbroadcast", NULL},
	{AT_SIM300_CMOD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMOD",  {"+CMOD:", ""}, "Configrue alternating mode calls", NULL},
	{AT_SIM300_CFGRI, AT_OPER_READ|AT_OPER_WRITE, "AT+CFGRI",  {"+CFGRI:", ""}, "Indicate RI when using URC", NULL},
	{AT_SIM300_CLTS, AT_OPER_TEST|AT_OPER_EXEC, "AT+CLTS",  {"+CLTS:", ""}, "Get local timestamp", NULL},
	{AT_SIM300_CEXTHS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CEXTHS",  {"+CEXTHS:", ""}, "External headset jack control", NULL},
	{AT_SIM300_CEXTBUT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CEXTBUT",  {"+CEXTBUT:", ""}, "Headset button status reporting", NULL},
	{AT_SIM300_CSMINS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMINS",  {"+CSMINS:", ""}, "SIM inserted status reporting", NULL},
	{AT_SIM300_CLDTMF, AT_OPER_EXEC|AT_OPER_WRITE, "AT+CLDTMF",  {"", ""}, "Local DTMF tone generation", NULL},
	{AT_SIM300_CDRIND, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CDRIND",  {"+CDRIND:", ""}, "CS voice/data/fax call or GPRS PDP context termination indication", NULL},
	{AT_SIM300_CSPN, AT_OPER_READ, "AT+CSPN",  {"+CSPN:", ""}, "Get service provider name from SIM", NULL},
	{AT_SIM300_CCVM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCVM",  {"+CCVM:", ""}, "Get and set the voice mail number on the SIM", NULL},
	{AT_SIM300_CBAND, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CBAND",  {"+CBAND:", ""}, "Get and set mobile operation band", NULL},
	{AT_SIM300_CHF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CHF",  {"+CHF:", ""}, "Configures hands free operation", NULL},
	{AT_SIM300_CHFA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CHFA",  {"+CHFA:", ""}, "Swap the audio channels", NULL},
	{AT_SIM300_CSCLK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCLK",  {"+CSCLK:", ""}, "Configure slow clock", NULL},
	{AT_SIM300_CENG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CENG",  {"+CENG:", ""}, "Switch on or off engineering mode", NULL},
	{AT_SIM300_SCLASS0, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+SCLASS0",  {"+SCLASS0:", ""}, "Store class 0 SMS to SIM when received class 0 SMS", NULL},
	{AT_SIM300_CCID, AT_OPER_TEST|AT_OPER_EXEC, "AT+CCID",  {"", ""}, "Show ICCID", is_str_xdigit},
	{AT_SIM300_CMTE, AT_OPER_READ, "AT+CMTE",  {"+CMTE:", ""}, "Read temperature of module", NULL},
	{AT_SIM300_CSDT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSDT",  {"+CSDT:", ""}, "Switch on or off detecting SIM card", NULL},
	{AT_SIM300_CMGDA, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGDA",  {"+CMGDA:", ""}, "Delete all SMS", NULL},
	{AT_SIM300_SIMTONE, AT_OPER_TEST|AT_OPER_WRITE, "AT+SIMTONE",  {"+SIMTONE:", ""}, "Generate specifically tone", NULL},
	{AT_SIM300_CCPD, AT_OPER_READ|AT_OPER_WRITE, "AT+CCPD",  {"+CCPD:", ""}, "Connected line identification presentation without alpha string", NULL},
	{AT_SIM300_CGID, AT_OPER_EXEC, "AT+CGID",  {"GID", ""}, "Get SIM card group identifier", NULL},
	{AT_SIM300_MORING, AT_OPER_TEST|AT_OPER_WRITE, "AT+MORING",  {"+MORING:", ""}, "Show state of mobile originated call", NULL},
	{AT_SIM300_CGMSCLASS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CGMSCLASS",  {"MULTISLOT CLASS:", ""}, "Change GPRS multislot class", NULL},
	{AT_SIM300_CMGHEX, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMGHEX",  {"CMGHEX:", ""}, "Enable to send non-ASCII character SMS", NULL},
	{AT_SIM300_EXUNSOL, AT_OPER_TEST|AT_OPER_WRITE, "AT+EXUNSOL",  {"+EXUNSOL:", ""}, "Extra unsolicited indications", NULL},
	};
//------------------------------------------------------------------------------

static const
char imei_data1[13] = {	0x02,0x09,0xD5,0xFF,0x00,0x05,0x02,
						0x03,0x00,0x00,0x00,0x25,0x03};

static const
char imei_data2[18] = {	0x02,0x0E,0xD3,0xFF,0x00,0x0A,0x02,
						0x02,0x02,0x04,0x04,0x00,0x11,0x1A,
						0x03,0x02,0x22,0x03};

static const
char imei_data3[49] = {	0x02,0x2D,0xD3,0xFF,0x00,0x29,0x02,
						0x04,0x02,0x04,0x23,0x00,0x0F,0x1A,
						0x03,0x02,0x13,0x12,0x00,0x16,0x00,
						0x00,0x0F,0x00,0x00,0x00,0x00,0x00,
						0x00,0x00,0x03,0x05,0x05,0x04,0x07,
						0x01,0x00,0x01,0x00,0x00,0x00,0x03,
						0x04,0x08,0x05,0x00,0x00,0x06,0x03};
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// sim300_build_imei_data1()
//------------------------------------------------------------------------------
int sim300_build_imei_data1(char *data, int *len)
{
	int ln;
	// check input params
	if(!data)
		return -1;
	if(!len)
		return -2;
	//
	ln = sizeof(imei_data1);
	// copy data
	memcpy(data, imei_data1, ln);
	*len = ln;

	return ln;
}
//------------------------------------------------------------------------------
// end of sim300_build_imei_data1()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// sim300_build_imei_data2()
//------------------------------------------------------------------------------
int sim300_build_imei_data2(char *data, int *len)
{
	int ln;
	// check input params
	if (!data)
		return -1;
	if (!len)
		return -2;

	ln = sizeof(imei_data2);
	// copy data
	memcpy(data, imei_data2, ln);
	*len = ln;

	return ln;
}
//------------------------------------------------------------------------------
// end of sim300_build_imei_data2()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// sim300_build_imei_data3()
//------------------------------------------------------------------------------
int sim300_build_imei_data3(const char *imei, char chk, char *data, int *len)
{
	int i;
	int ln;
	char cs;
	char *p;
	char *sp;
	// check input params
	if (!data)
		return -1;
	if (!len)
		return -2;
	if (!imei)
		return -3;
	if (!isdigit(chk))
		return -4;
	//
	ln = sizeof(imei_data3);
	sp = data;
	// copy data
	memcpy(data, imei_data3, ln);
	// copy IMEI digit into data3 buffer
	p = sp + 30;
	for (i=0; i<14; i++)
		*p++ = *imei++ - '0';
	// copy IMEI check digit
	*p = chk - '0';
	// calc checksum
	cs = 0;
	p = sp;
	for (i=0; i<47; i++)
		cs ^= *p++;
	// put checksum into data3 buffer
	p = sp + 47;
	*p = cs;

	*len = ln;
	return ln;
}
//------------------------------------------------------------------------------
// end of sim300_build_imei_data3()
//------------------------------------------------------------------------------

//==============================================================================
// begin parser response function section

//------------------------------------------------------------------------------
// at_sim300_csmins_read_parse()
//------------------------------------------------------------------------------
int at_sim300_csmins_read_parse(const char *fld, int fld_len, struct at_sim300_csmins_read *csmins)
{
	char *sp;
	char *tp;
	char *ep;

#define MAX_CSIMINS_READ_PARAM 2
	struct parsing_param params[MAX_CSIMINS_READ_PARAM];
	int param_cnt;

	// check params
	if (!fld) return -1;

	if ((fld_len <= 0) || (fld_len > 256)) return -1;

	if (!csmins) return -1;

	// init ptr
	if (!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for (param_cnt=0; param_cnt<MAX_CSIMINS_READ_PARAM; param_cnt++)
	{
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
	}

	// init at_sim300_csmins_read
	csmins->n = -1;
	csmins->sim_inserted = -1;

	// search params delimiters
	param_cnt = 0;
	while ((tp < ep) || (param_cnt < MAX_CSIMINS_READ_PARAM))
	{
		// get param type
		if (*tp == '"') {
			params[param_cnt].type = PRM_TYPE_STRING;
			params[param_cnt].buf = ++tp;
		} else if (isdigit(*tp)) {
			params[param_cnt].type = PRM_TYPE_INTEGER;
			params[param_cnt].buf = tp;
		} else {
			params[param_cnt].type = PRM_TYPE_UNKNOWN;
			params[param_cnt].buf = tp;
		}
		sp = tp;
		// search delimiter and put terminated null-symbol
		if (!(tp = strchr(sp, ',')))
			tp = ep;
		*tp = '\0';
		// set param len
		if (params[param_cnt].type == PRM_TYPE_STRING) {
			params[param_cnt].len = tp - sp - 1;
			*(tp-1) = '\0';
		} else {
			params[param_cnt].len = tp - sp;
		}
		//
		param_cnt++;
		tp++;
	}

	// processing integer params
	// n (mandatory)
	if (params[0].len > 0) {
		tp = params[0].buf;
		while (params[0].len--)
		{
			if (!isdigit(*tp++))
				return -1;
		}
		csmins->n = atoi(params[0].buf);
	} else
		return -1;

	// sim_inserted (mandatory)
	if (params[1].len > 0) {
		tp = params[1].buf;
		while (params[1].len--)
		{
			if (!isdigit(*tp++))
				return -1;
		}
		csmins->sim_inserted = atoi(params[1].buf);
	} else
		return -1;

	return param_cnt;
}
//------------------------------------------------------------------------------
// end of at_sim300_csmins_read_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_sim300_parse_cmic_read_parse()
//------------------------------------------------------------------------------
int at_sim300_cmic_read_parse(const char *fld, int fld_len, struct at_sim300_cmic_read *cmic)
{
	char *sp;
	char *tp;
	char *ep;

#define MAX_CMIC_READ_PARAM 2
	struct parsing_param params[MAX_CMIC_READ_PARAM];
	int param_cnt;

	// check params
	if (!fld) return -1;

	if ((fld_len <= 0) || (fld_len > 256)) return -1;

	if (!cmic) return -1;

	// init ptr
	if (!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for (param_cnt=0; param_cnt<MAX_CMIC_READ_PARAM; param_cnt++)
	{
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
	}

	// init at_sim300_cmic_read
	cmic->main_mic = -1;
	cmic->aux_mic = -1;

	// search params delimiters
	param_cnt = 0;
	while ((tp < ep) && (param_cnt < MAX_CMIC_READ_PARAM))
	{
		// get param type
		if (*tp == '"') {
			params[param_cnt].type = PRM_TYPE_STRING;
			params[param_cnt].buf = ++tp;
		} else if (isdigit(*tp)) {
			params[param_cnt].type = PRM_TYPE_INTEGER;
			params[param_cnt].buf = tp;
		} else {
			params[param_cnt].type = PRM_TYPE_UNKNOWN;
			params[param_cnt].buf = tp;
		}
		sp = tp;
		// search delimiter and put terminated null-symbol
		if (!(tp = strchr(sp, ',')))
			tp = ep;
		*tp = '\0';
		// set param len
		if (params[param_cnt].type == PRM_TYPE_STRING) {
			params[param_cnt].len = tp - sp - 1;
			*(tp-1) = '\0';
		} else {
			params[param_cnt].len = tp - sp;
		}
		//
		param_cnt++;
		tp++;
	}

	// processing integer params
	// Main microphone gain level
	if (params[0].len > 0) {
		tp = params[0].buf;
		while (params[0].len--)
		{
			if (!isdigit(*tp++))
				return -1;
		}
		cmic->main_mic = atoi(params[0].buf);
	} else
		return -1;

	// Aux microphone gain level
	if (params[1].len > 0) {
		tp = params[1].buf;
		while (params[1].len--)
		{
			if (!isdigit(*tp++))
				return -1;
		}
		cmic->aux_mic = atoi(params[1].buf);
	} else
		return -1;

	return param_cnt;
}
//------------------------------------------------------------------------------
// end of at_sim300_cmic_read_parse()
//------------------------------------------------------------------------------

// end of parser response function section
//==============================================================================

/******************************************************************************/
/* end of sim300.c                                                            */
/******************************************************************************/
