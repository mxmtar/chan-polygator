/******************************************************************************/
/* sim900.c                                                                   */
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
#include "sim900.h"
#include "strutil.h"
//------------------------------------------------------------------------------

unsigned char sim900_code_page[0x10000];

const unsigned char sim900_set_storage_equipment_s0[9] = {
	0x04,
	0x00, 0x00, 0x00, 0x90,
	0x00, 0x00, 0x00, 0x00};

const unsigned char sim900_set_storage_equipment_s1[9] = {
	0x04,
	0x00, 0x00, 0x23, 0x90,
	0x00, 0x00, 0x01, 0x00};

const unsigned char sim900_configuration_for_erased_area_s0[9] = {
	0x09,
	0x00, 0x00, 0x23, 0x90,
	0x00, 0x00, 0x01, 0x00};

const unsigned char sim900_configuration_for_erased_area_s1[9] = {
	0x09,
	0x00, 0x00, 0x51, 0x90,
	0x00, 0x00, 0x2e, 0x00};

const unsigned char sim900_set_for_downloaded_code_information[9] = {
	0x04,
	0x00, 0x00, 0x23, 0x90,
	0x00, 0x00, 0x01, 0x00};

const unsigned char sim900_set_for_downloaded_code_section[5] = {
	0x01,
	0x00, 0x08, 0x00, 0x00};

const struct at_command sim900_at_com_list[/*AT_SIM900_MAXNUM*/] = {
	// int id; u_int32_t operations; char name[16]; char response[MAX_AT_CMD_RESP][16]; char description[256]; add_check_at_resp_fun_t *check_fun;
	{AT_SIM900_UNKNOWN, AT_OPER_EXEC, "", {"", ""}, "", is_str_printable},
	// SIM900 V.25TER V1.04
	{AT_SIM900_A_SLASH, AT_OPER_EXEC, "A/", {"", ""}, "Re-issues last AT command given", NULL},
	{AT_SIM900_A, AT_OPER_EXEC, "ATA", {"", ""}, "Answer an incoming call", NULL},
	{AT_SIM900_D, AT_OPER_EXEC, "ATD", {"", ""}, "Mobile originated call to dial a number", NULL},
	{AT_SIM900_D_CURMEM, AT_OPER_EXEC, "ATD", {"", ""}, "Originate call to phone number in current memory", NULL},
	{AT_SIM900_D_PHBOOK, AT_OPER_EXEC, "ATD", {"", ""}, "Originate call to phone number in memory which corresponds to field <STR>", NULL},
	{AT_SIM900_DL, AT_OPER_EXEC, "ATDL",  {"", ""}, "Redial last telephone number used", NULL},
	{AT_SIM900_E, AT_OPER_EXEC, "ATE",  {"", ""}, "Set command echo mode", NULL},
	{AT_SIM900_H, AT_OPER_EXEC, "ATH",  {"", ""}, "Disconnect existing connection", NULL},
	{AT_SIM900_I, AT_OPER_EXEC, "ATI",  {"", ""}, "Display product identification information", NULL},
	{AT_SIM900_L, AT_OPER_EXEC, "ATL",  {"", ""}, "Set monitor speaker loudness", NULL},
	{AT_SIM900_M, AT_OPER_EXEC, "ATM",  {"", ""}, "Set monitor speaker mode", NULL},
	{AT_SIM900_3PLUS, AT_OPER_EXEC, "+++",  {"", ""}, "Switch from data mode or PPP online mode to command mode", NULL},
	{AT_SIM900_O, AT_OPER_EXEC, "ATO",  {"", ""}, "Switch from command mode to data mode", NULL},
	{AT_SIM900_P, AT_OPER_EXEC, "ATP",  {"", ""}, "Select pulse dialling", NULL},
	{AT_SIM900_Q, AT_OPER_EXEC, "ATQ",  {"", ""}, "Set result code presentation mode", NULL},
	{AT_SIM900_S0, AT_OPER_READ|AT_OPER_WRITE, "ATS0",  {"", ""}, "Set number of rings before automatically answering the call", NULL},
	{AT_SIM900_S3, AT_OPER_READ|AT_OPER_WRITE, "ATS3",  {"", ""}, "Set command line termination character", NULL},
	{AT_SIM900_S4, AT_OPER_READ|AT_OPER_WRITE, "ATS4",  {"", ""}, "Set response formatting character", NULL},
	{AT_SIM900_S5, AT_OPER_READ|AT_OPER_WRITE, "ATS5",  {"", ""}, "Set command line editing character", NULL},
	{AT_SIM900_S6, AT_OPER_READ|AT_OPER_WRITE, "ATS6",  {"", ""}, "Set pause before blind dialling", NULL},
	{AT_SIM900_S7, AT_OPER_READ|AT_OPER_WRITE, "ATS7",  {"", ""}, "Set number of seconds to wait for connection completion", NULL},
	{AT_SIM900_S8, AT_OPER_READ|AT_OPER_WRITE, "ATS8",  {"", ""}, "Set number of seconds to wait when comma dial modifier used", NULL},
	{AT_SIM900_S10, AT_OPER_READ|AT_OPER_WRITE, "ATS10",  {"", ""}, "Set disconnect delay after indicating the absence of data carrier", NULL},
	{AT_SIM900_T, AT_OPER_EXEC, "ATT",  {"", ""}, "Select tone dialling", NULL},
	{AT_SIM900_V, AT_OPER_EXEC, "ATV",  {"", ""}, "Set result code format mode", NULL},
	{AT_SIM900_X, AT_OPER_EXEC, "ATX",  {"", ""}, "Set connect result code format and monitor call progress", NULL},
	{AT_SIM900_Z, AT_OPER_EXEC, "ATZ",  {"", ""}, "Set all current parameters to user defined profile", NULL},
	{AT_SIM900_andC, AT_OPER_EXEC, "AT&C",  {"", ""}, "Set DCD function mode", NULL},
	{AT_SIM900_andD, AT_OPER_EXEC, "AT&D",  {"", ""}, "Set DTR function mode", NULL},
	{AT_SIM900_andF, AT_OPER_EXEC, "AT&F",  {"", ""}, "Set all current parameters to manufacturer defaults", NULL},
	{AT_SIM900_andV, AT_OPER_EXEC, "AT&V",  {"", ""}, "Display current configuration", NULL},
	{AT_SIM900_andW, AT_OPER_EXEC, "AT&W",  {"", ""}, "Store current parameter to user defined profile", NULL},
	{AT_SIM900_GCAP, AT_OPER_TEST|AT_OPER_EXEC, "AT+GCAP",  {"+GCAP:", ""}, "Request complete TA capabilities list", NULL},
	{AT_SIM900_GMI, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMI",  {"", ""}, "Request manufacturer identification", is_str_printable},
	{AT_SIM900_GMM, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMM",  {"", ""}, "Request TA model identification", is_str_printable},
	{AT_SIM900_GMR, AT_OPER_TEST|AT_OPER_EXEC, "AT+GMR",  {"", ""}, "Request TA revision indentification of software release", is_str_printable},
	{AT_SIM900_GOI, AT_OPER_TEST|AT_OPER_EXEC, "AT+GOI",  {"", ""}, "Request global object identification", is_str_printable},
	{AT_SIM900_GSN, AT_OPER_TEST|AT_OPER_EXEC, "AT+GSN",  {"", ""}, "Request ta serial number identification (IMEI)", is_str_digit},
	{AT_SIM900_ICF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+ICF",  {"+ICF:", ""}, "Set TE-TA control character framing", NULL},
	{AT_SIM900_IFC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+IFC",  {"+IFC:", ""}, "Set TE-TA local data flow control", NULL},
	{AT_SIM900_ILRR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+ILRR",  {"+ILRR:", ""}, "Set TE-TA local rate reporting mode", NULL},
	{AT_SIM900_IPR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+IPR",  {"+IPR:", ""}, "Set TE-TA fixed local rate", NULL},
	{AT_SIM900_HVOIC, AT_OPER_EXEC, "AT+HVOIC",  {"", ""}, "Disconnect voive call only", NULL},

	// SIM900 GSM07.07 V1.04
	{AT_SIM900_CACM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CACM",  {"+CACM:", ""}, "Accumulated call meter(ACM) reset or query", NULL},
	{AT_SIM900_CAMM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CAMM",  {"+CAMM:", ""}, "Accumulated call meter maximum(ACMMAX) set or query", NULL},
	{AT_SIM900_CAOC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CAOC",  {"+CAOC:", ""}, "Advice of charge", NULL},
	{AT_SIM900_CBST, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CBST",  {"+CBST:", ""}, "Select bearer service type", NULL},
	{AT_SIM900_CCFC, AT_OPER_TEST|AT_OPER_WRITE, "AT+CCFC",  {"+CCFC:", ""}, "Call forwarding number and conditions control", NULL},
	{AT_SIM900_CCWA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCWA",  {"+CCWA:", ""}, "Call waiting control", NULL},
	{AT_SIM900_CEER, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CEER",  {"+CEER:", ""}, "Extended error report", NULL},
	{AT_SIM900_CGMI, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMI",  {"", ""}, "Request manufacturer identification", is_str_printable},
	{AT_SIM900_CGMM, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMM",  {"", ""}, "Request model identification", is_str_printable},
	{AT_SIM900_CGMR, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGMR",  {"", ""}, "Request TA revision identification of software release", is_str_printable},
	{AT_SIM900_CGSN, AT_OPER_EXEC|AT_OPER_TEST, "AT+CGSN",  {"", ""}, "Request product serial number identification (identical with +GSN)", is_str_digit},
	{AT_SIM900_CSCS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCS",  {"+CSCS:", ""}, "Select TE character set", NULL},
	{AT_SIM900_CSTA, AT_OPER_TEST|AT_OPER_READ, "AT+CSTA",  {"+CSTA:", ""}, "Select type of address", NULL},
	{AT_SIM900_CHLD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CHLD",  {"+CHLD:", ""}, "Call hold and multiparty", NULL},
	{AT_SIM900_CIMI, AT_OPER_EXEC|AT_OPER_TEST, "AT+CIMI",  {"", ""}, "Request international mobile subscriber identity", is_str_digit},
	{AT_SIM900_CKPD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CKPD",  {"", ""}, "Keypad control", NULL},
	{AT_SIM900_CLCC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CLCC",  {"+CLCC:", ""}, "List current calls of ME", NULL},
	{AT_SIM900_CLCK, AT_OPER_TEST|AT_OPER_WRITE, "AT+CLCK",  {"+CLCK:", ""}, "Facility lock", NULL},
	{AT_SIM900_CLIP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLIP",  {"+CLIP:", ""}, "Calling line identification presentation", NULL},
	{AT_SIM900_CLIR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLIR",  {"+CLIR:", ""}, "Calling line identification restriction", NULL},
	{AT_SIM900_CMEE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMEE",  {"+CMEE:", ""}, "Report mobile equipment error", NULL},
	{AT_SIM900_COLP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+COLP",  {"+COLP:", ""}, "Connected line identification presentation", NULL},
	{AT_SIM900_COPS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+COPS",  {"+COPS:", ""}, "Operator selection", NULL},
	{AT_SIM900_CPAS, AT_OPER_EXEC|AT_OPER_TEST, "AT+CPAS",  {"+CPAS:", ""}, "Mobile equipment activity status", NULL},
	{AT_SIM900_CPBF, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBF",  {"+CPBF:", ""}, "Find phonebook entries", NULL},
	{AT_SIM900_CPBR, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBR",  {"+CPBR:", ""}, "Read current phonebook entries", NULL},
	{AT_SIM900_CPBS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPBS",  {"+CPBS:", ""}, "Select phonebook memory storage", NULL},
	{AT_SIM900_CPBW, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBW",  {"+CPBW:", ""}, "Write phonebook entry", NULL},
	{AT_SIM900_CPIN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPIN",  {"+CPIN:", ""}, "Enter PIN", NULL},
	{AT_SIM900_CPWD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPWD",  {"+CPWD:", ""}, "Change password", NULL},
	{AT_SIM900_CR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CR",  {"+CR:", ""}, "Service reporting control", NULL},
	{AT_SIM900_CRC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRC",  {"+CRC:", ""}, "Set cellular result codes for incoming call indication", NULL},
	{AT_SIM900_CREG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CREG",  {"+CREG:", ""}, "Network registration", NULL},
	{AT_SIM900_CRLP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRLP",  {"+CRLP:", ""}, "Select radio link protocol parameter", NULL},
	{AT_SIM900_CRSM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRSM",  {"+CRSM:", ""}, "Restricted SIM access", NULL},
	{AT_SIM900_CSQ, AT_OPER_TEST|AT_OPER_EXEC, "AT+CSQ",  {"+CSQ:", ""}, "Signal quality report", NULL},
	{AT_SIM900_FCLASS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+FCLASS",  {"+FCLASS:", ""}, "Fax: select, read or test service class", NULL},
	{AT_SIM900_FMI, AT_OPER_TEST|AT_OPER_READ, "AT+FMI",  {"", ""}, "Fax: report manufactured ID", is_str_printable},
	{AT_SIM900_FMM, AT_OPER_TEST|AT_OPER_READ, "AT+FMM",  {"", ""}, "Fax: report model ID", is_str_printable},
	{AT_SIM900_FMR, AT_OPER_TEST|AT_OPER_EXEC, "AT+FMR",  {"", ""}, "Fax: report revision ID", is_str_printable},
	{AT_SIM900_VTD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+VTD",  {"+VTD:", ""}, "Tone duration", NULL},
	{AT_SIM900_VTS, AT_OPER_TEST|AT_OPER_WRITE, "AT+VTS",  {"+VTS:", ""}, "DTMF and tone generation", NULL},
	{AT_SIM900_CMUX, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMUX",  {"+CMUX:", ""}, "Multiplexer control", NULL},
	{AT_SIM900_CNUM, AT_OPER_TEST|AT_OPER_EXEC, "AT+CNUM",  {"+CNUM:", ""}, "Subscriber number", NULL},
	{AT_SIM900_CPOL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPOL",  {"+CPOL:", ""}, "Preferred operator list", NULL},
	{AT_SIM900_COPN, AT_OPER_TEST|AT_OPER_EXEC, "AT+COPN",  {"+COPN:", ""}, "Read operator names", NULL},
	{AT_SIM900_CFUN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CFUN",  {"+CFUN:", ""}, "Set phone functionality", NULL},
	{AT_SIM900_CCLK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCLK",  {"+CCLK:", ""}, "Clock", NULL},
	{AT_SIM900_CSIM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CSIM",  {"+CSIM:", ""}, "Generic SIM access", NULL},
	{AT_SIM900_CALM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CALM",  {"+CALM:", ""}, "Alert sound mode", NULL},
	{AT_SIM900_CALS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CALS", {"+CALS", ""},  "Alert sound select", NULL},
	{AT_SIM900_CRSL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRSL",  {"+CRSL:", ""}, "Ringer sound level", NULL},
	{AT_SIM900_CLVL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLVL",  {"+CLVL:", ""}, "Loud speaker volume level", NULL},
	{AT_SIM900_CMUT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMUT",  {"+CMUT:", ""}, "Mute control", NULL},
	{AT_SIM900_CPUC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPUC",  {"+CPUC:", ""}, "Price per unit currency table", NULL},
	{AT_SIM900_CCWE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCWE",  {"+CCWE:", ""}, "Call meter maximum event", NULL},
	{AT_SIM900_CBC, AT_OPER_TEST|AT_OPER_EXEC, "AT+CBC",  {"+CBC:", ""}, "Battery charge", NULL},
	{AT_SIM900_CUSD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CUSD",  {"+CUSD:", ""}, "Unstructured supplementary service data", NULL},
	{AT_SIM900_CSSN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSSN",  {"+CSSN:", ""}, "Supplementary services notification", NULL},

	// SIM900 GSM07.05 V1.04
	{AT_SIM900_CMGD, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGD",  {"+CMGD:", ""}, "Delete SMS message", NULL},
	{AT_SIM900_CMGF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMGF",  {"+CMGF:", ""}, "Select SMS message format", NULL},
	{AT_SIM900_CMGL, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGL",  {"", ""}, "List SMS messages from preferred store", is_str_xdigit},
	{AT_SIM900_CMGR, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGR",  {"+CMGR:", ""}, "Read SMS message", is_str_xdigit},
	{AT_SIM900_CMGS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGS",  {"+CMGS:", ""}, "Send SMS message", NULL},
	{AT_SIM900_CMGW, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGW",  {"+CMGW:", ""}, "Write SMS message to memory", NULL},
	{AT_SIM900_CMSS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMSS",  {"+CMSS:", ""}, "Send SMS message from storage", NULL},
	{AT_SIM900_CNMI, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CNMI",  {"+CNMI:", ""}, "New SMS message indications", NULL},
	{AT_SIM900_CPMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPMS",  {"+CPMS:", ""}, "Preferred SMS message storage", NULL},
	{AT_SIM900_CRES, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRES",  {"+CRES:", ""}, "Restore SMS settings", NULL},
	{AT_SIM900_CSAS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CSAS",  {"+CSAS:", ""}, "Save SMS settings", NULL},
	{AT_SIM900_CSCA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCA",  {"+CSCA:", ""}, "SMS service center address", NULL},
	{AT_SIM900_CSCB, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCB",  {"+CSCB:", ""}, "Select cell broadcast SMS messages", NULL},
	{AT_SIM900_CSDH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSDH",  {"+CSDH:", ""}, "Show SMS text mode parameters", NULL},
	{AT_SIM900_CSMP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMP",  {"+CSMP:", ""}, "Set SMS text mode parameters", NULL},
	{AT_SIM900_CSMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMS",  {"+CSMS:", ""}, "Select message service", NULL},

	// SIM900 SIMCOM V1.04
	{AT_SIM900_SIDET, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+SIDET",  {"+SIDET:", ""}, "Change the side tone gain level", NULL},
	{AT_SIM900_CPOWD, AT_OPER_WRITE, "AT+CPOWD",  {"", ""}, "Power off", NULL},
	{AT_SIM900_SPIC, AT_OPER_EXEC, "AT+SPIC",  {"+SPIC:", ""}, "Times remain to input SIM PIN/PUK", NULL},
	{AT_SIM900_CMIC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMIC",  {"+CMIC:", ""}, "Change the micophone gain level", NULL},
	{AT_SIM900_CALA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CALA",  {"+CALA:", ""}, "Set alarm time", NULL},
	{AT_SIM900_CALD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CALD",  {"+CALD:", ""}, "Delete alarm", NULL},
	{AT_SIM900_CADC, AT_OPER_TEST|AT_OPER_READ, "AT+CADC",  {"+CADC:", ""}, "Read adc", NULL},
	{AT_SIM900_CSNS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSNS",  {"+CSNS:", ""}, "Single numbering scheme", NULL},
	{AT_SIM900_CDSCB, AT_OPER_EXEC, "AT+CDSCB",  {"", ""}, "Reset cellbroadcast", NULL},
	{AT_SIM900_CMOD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMOD",  {"+CMOD:", ""}, "Configrue alternating mode calls", NULL},
	{AT_SIM900_CFGRI, AT_OPER_READ|AT_OPER_WRITE, "AT+CFGRI",  {"+CFGRI:", ""}, "Indicate RI when using URC", NULL},
	{AT_SIM900_CLTS, AT_OPER_TEST|AT_OPER_EXEC, "AT+CLTS",  {"+CLTS:", ""}, "Get local timestamp", NULL},
	{AT_SIM900_CEXTHS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CEXTHS",  {"+CEXTHS:", ""}, "External headset jack control", NULL},
	{AT_SIM900_CEXTBUT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CEXTBUT",  {"+CEXTBUT:", ""}, "Headset button status reporting", NULL},
	{AT_SIM900_CSMINS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMINS",  {"+CSMINS:", ""}, "SIM inserted status reporting", NULL},
	{AT_SIM900_CLDTMF, AT_OPER_EXEC|AT_OPER_WRITE, "AT+CLDTMF",  {"", ""}, "Local DTMF tone generation", NULL},
	{AT_SIM900_CDRIND, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CDRIND",  {"+CDRIND:", ""}, "CS voice/data/fax call or GPRS PDP context termination indication", NULL},
	{AT_SIM900_CSPN, AT_OPER_READ, "AT+CSPN",  {"+CSPN:", ""}, "Get service provider name from SIM", NULL},
	{AT_SIM900_CCVM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCVM",  {"+CCVM:", ""}, "Get and set the voice mail number on the SIM", NULL},
	{AT_SIM900_CBAND, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CBAND",  {"+CBAND:", ""}, "Get and set mobile operation band", NULL},
	{AT_SIM900_CHF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CHF",  {"+CHF:", ""}, "Configures hands free operation", NULL},
	{AT_SIM900_CHFA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CHFA",  {"+CHFA:", ""}, "Swap the audio channels", NULL},
	{AT_SIM900_CSCLK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCLK",  {"+CSCLK:", ""}, "Configure slow clock", NULL},
	{AT_SIM900_CENG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CENG",  {"+CENG:", ""}, "Switch on or off engineering mode", NULL},
	{AT_SIM900_SCLASS0, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+SCLASS0",  {"+SCLASS0:", ""}, "Store class 0 SMS to SIM when received class 0 SMS", NULL},
	{AT_SIM900_CCID, AT_OPER_TEST|AT_OPER_EXEC, "AT+CCID",  {"", ""}, "Show ICCID", is_str_xdigit},
	{AT_SIM900_CMTE, AT_OPER_READ, "AT+CMTE",  {"+CMTE:", ""}, "Read temperature of module", NULL},
	{AT_SIM900_CBTE, AT_OPER_READ, "AT+CBTE",  {"+CBTE:", ""}, "Battery temperature query", NULL},
	{AT_SIM900_CSDT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSDT",  {"+CSDT:", ""}, "Switch on or off detecting SIM card", NULL},
	{AT_SIM900_CMGDA, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGDA",  {"+CMGDA:", ""}, "Delete all SMS", NULL},
	{AT_SIM900_STTONE, AT_OPER_TEST|AT_OPER_WRITE, "AT+STTONE",  {"+STTONE:", ""}, "Play SIM Toolkit tone", NULL},
	{AT_SIM900_SIMTONE, AT_OPER_TEST|AT_OPER_WRITE, "AT+SIMTONE",  {"+SIMTONE:", ""}, "Generate specifically tone", NULL},
	{AT_SIM900_CCPD, AT_OPER_READ|AT_OPER_WRITE, "AT+CCPD",  {"+CCPD:", ""}, "Connected line identification presentation without alpha string", NULL},
	{AT_SIM900_CGID, AT_OPER_EXEC, "AT+CGID",  {"GID", ""}, "Get SIM card group identifier", NULL},
	{AT_SIM900_MORING, AT_OPER_TEST|AT_OPER_WRITE, "AT+MORING",  {"+MORING:", ""}, "Show state of mobile originated call", NULL},
	{AT_SIM900_CMGHEX, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMGHEX",  {"CMGHEX:", ""}, "Enable to send non-ASCII character SMS", NULL},
	{AT_SIM900_CCODE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCODE",  {"+CCODE:", ""}, "Configrue SMS code mode", NULL},
	{AT_SIM900_CIURC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CIURC",  {"+CIURC:", ""}, "Enable or disable initial URC presentation", NULL},
	{AT_SIM900_CPSPWD, AT_OPER_WRITE, "AT+CPSPWD",  {"", ""}, "Change PS super password", NULL},
	{AT_SIM900_EXUNSOL, AT_OPER_TEST|AT_OPER_WRITE, "AT+EXUNSOL",  {"+EXUNSOL:", ""}, "Extra unsolicited indications", NULL},
	{AT_SIM900_CGMSCLASS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CGMSCLASS",  {"MULTISLOT CLASS:", ""}, "Change GPRS multislot class", NULL},
	{AT_SIM900_CDEVICE, AT_OPER_READ, "AT+CDEVICE",  {"", ""}, "View current flash device type", is_str_printable},
	{AT_SIM900_CCALR, AT_OPER_TEST|AT_OPER_READ, "AT+CCALR",  {"+CCALR:", ""}, "Call ready query", NULL},
	{AT_SIM900_GSV, AT_OPER_EXEC, "AT+GSV",  {"", ""}, "Display product identification information", is_str_printable},
	{AT_SIM900_SGPIO, AT_OPER_TEST|AT_OPER_WRITE, "AT+SGPIO",  {"+SGPIO:", ""}, "Control the GPIO", NULL},
	{AT_SIM900_SPWM, AT_OPER_TEST|AT_OPER_WRITE, "AT+SPWM",  {"+SPWM:", ""}, "Generate PWM", NULL},
	{AT_SIM900_ECHO, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+ECHO",  {"+ECHO:", ""}, "Echo cancellation control", NULL},
	{AT_SIM900_CAAS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CAAS",  {"+CAAS:", ""}, "Control auto audio switch", NULL},
	{AT_SIM900_SVR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+SVR",  {"+SVR:", ""}, "Configrue voice coding type for voice calls", NULL},
	{AT_SIM900_GSMBUSY, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+GSMBUSY",  {"+GSMBUSY:", ""}, "Reject incoming call", NULL},
	{AT_SIM900_CEMNL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CEMNL",  {"+CEMNL:", ""}, "Set the list of emergency number", NULL},
	{AT_SIM900_CELLLOCK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT*CELLLOCK",  {"*CELLLOCK:", ""}, "Set the list of arfcn which needs to be locked", NULL},
	{AT_SIM900_SLEDS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+SLEDS",  {"+SLEDS:", ""}, "Set the timer period of net light", NULL},
	};
//------------------------------------------------------------------------------


//==============================================================================
// begin parser response function section

//------------------------------------------------------------------------------
// at_sim900_csmins_read_parse()
//------------------------------------------------------------------------------
int at_sim900_csmins_read_parse(const char *fld, int fld_len, struct at_sim900_csmins_read *csmins){

	char *sp;
	char *tp;
	char *ep;

#define MAX_CSIMINS_READ_PARAM 2
	struct parsing_param params[MAX_CSIMINS_READ_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!csmins) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_CSIMINS_READ_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_sim900_csmins_read
	csmins->n = -1;
	csmins->sim_inserted = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) || (param_cnt < MAX_CSIMINS_READ_PARAM)){
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
		csmins->n = atoi(params[0].buf);
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
		csmins->sim_inserted = atoi(params[1].buf);
		}
	else
		return -1;

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_sim900_csmins_read_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_sim900_parse_cmic_read_parse()
//------------------------------------------------------------------------------
int at_sim900_cmic_read_parse(const char *fld, int fld_len, struct at_sim900_cmic_read *cmic){

	char *sp;
	char *tp;
	char *ep;
	int i, ch, val;

#define MAX_CMIC_READ_PARAM 4
	struct parsing_param params[MAX_CMIC_READ_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!cmic) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_CMIC_READ_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_sim900_cmic_read
	cmic->main_hs_mic = -1;
	cmic->aux_hs_mic = -1;
	cmic->main_hf_mic = -1;
	cmic->aux_hf_mic = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt < MAX_CMIC_READ_PARAM)){
		while((tp < ep) && ((*tp == '(') || (*tp == ',')))
			tp++;
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
		while((tp < ep) && (*tp != ')') && (*tp != ',')) tp++;
		if(tp >= ep) tp = ep;
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

	for(i=0; i<param_cnt/2; i+=2){
		if(params[i].len > 0){
			tp = params[i].buf;
			while(params[i].len--){
				if(!isdigit(*tp++))
					return -1;
				}
			ch = atoi(params[i].buf);
			}
		else
			return -1;
		if(params[i+1].len > 0){
			tp = params[i+1].buf;
			while(params[i+1].len--){
				if(!isdigit(*tp++))
					return -1;
				}
			val = atoi(params[i+1].buf);
			}
		else
			return -1;
		switch(ch){
			case 3: // Aux handfree microphone gain level
				cmic->aux_hf_mic = val;
				break;
			case 2: // Main handfree microphone gain level
				cmic->main_hf_mic = val;
				break;
			case 1: // Aux handset microphone gain level
				cmic->aux_hs_mic = val;
				break;
			case 0: // Main handset microphone gain level
				cmic->main_hs_mic = val;
				break;
			default:
				break;
			}
		}

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_sim900_cmic_read_parse()
//------------------------------------------------------------------------------

// end of parser response function section
//==============================================================================

/******************************************************************************/
/* end of sim900.c                                                            */
/******************************************************************************/
