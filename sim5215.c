/******************************************************************************/
/* sim5215.c                                                                  */
/******************************************************************************/

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "at.h"
#include "sim5215.h"
#include "strutil.h"

const struct at_command sim5215_at_com_list[] = {
	// int id; u_int32_t operations; char name[16]; char response[MAX_AT_CMD_RESP][16]; char description[256]; add_check_at_resp_fun_t *check_fun;
	{AT_SIM5215_UNKNOWN, AT_OPER_EXEC, "", {"", ""}, "", is_str_printable},

	// General Commands
	{AT_SIM5215_I, AT_OPER_EXEC, "ATI", {"", ""}, "Display product identification information", NULL},
	{AT_SIM5215_CGMI, AT_OPER_TEST|AT_OPER_EXEC, "AT+CGMI",  {"", ""}, "Request manufacturer identification", is_str_printable},
	{AT_SIM5215_CGMM, AT_OPER_TEST|AT_OPER_EXEC, "AT+CGMM",  {"", ""}, "Request model identification", is_str_printable},
	{AT_SIM5215_CGMR, AT_OPER_TEST|AT_OPER_EXEC, "AT+CGMR",  {"", ""}, "Request TA revision identification of software release", is_str_printable},
	{AT_SIM5215_CGSN, AT_OPER_TEST|AT_OPER_EXEC, "AT+CGSN",  {"", ""}, "Request product serial number identification", is_str_digit},
	{AT_SIM5215_CSCS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CSCS",  {"+CSCS:", ""}, "Select TE character set", NULL},
	{AT_SIM5215_CIMI, AT_OPER_TEST|AT_OPER_EXEC, "AT+CIMI",  {"", ""}, "Request international mobile subscriber identity", is_str_digit},
	{AT_SIM5215_GCAP, AT_OPER_TEST|AT_OPER_EXEC, "AT+GCAP",  {"+GCAP:", ""}, "Request complete TA capabilities list", NULL},
	{AT_SIM5215_CATR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CATR",  {"+CATR:", ""}, "Configure URC destination interface", NULL},
	{AT_SIM5215_A_SLASH, AT_OPER_EXEC, "A/", {"", ""}, "Repeat last command", NULL},

	// Call Control Commands and Methods
	{AT_SIM5215_CSTA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CSTA",  {"+CSTA:", ""}, "Select type of address", NULL},
	{AT_SIM5215_CMOD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CMOD",  {"+CMOD:", ""}, "Call mode", NULL},
	{AT_SIM5215_D, AT_OPER_EXEC, "ATD", {"", ""}, "Dial command", NULL},
	{AT_SIM5215_D_MEM, AT_OPER_EXEC, "ATD", {"", ""}, "Originate call from specified memory", NULL},
	{AT_SIM5215_D_CURMEM, AT_OPER_EXEC, "ATD", {"", ""}, "Originate call to phone number in current memory", NULL},
	{AT_SIM5215_D_PHBOOK, AT_OPER_EXEC, "ATD", {"", ""}, "Originate call to phone number in memory which corresponds to field <STR>", NULL},
	{AT_SIM5215_A, AT_OPER_EXEC, "ATA", {"", ""}, "Answer an incoming call", NULL},
	{AT_SIM5215_3PLUS, AT_OPER_EXEC, "+++",  {"", ""}, "Switch from data mode or PPP online mode to command mode", NULL},
	{AT_SIM5215_O, AT_OPER_EXEC, "ATO",  {"", ""}, "Switch from command mode to data mode", NULL},
	{AT_SIM5215_CVHU, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CVHU",  {"+CVHU:", ""}, "Voice hang up control", NULL},
	{AT_SIM5215_H, AT_OPER_EXEC, "ATH",  {"", ""}, "Disconnect existing connection", NULL},
	{AT_SIM5215_CHUP, AT_OPER_TEST|AT_OPER_EXEC, "AT+CHUP",  {"VOICE CALL:END:", ""}, "Hang up control", NULL},
	{AT_SIM5215_CBST, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CBST",  {"+CBST:", ""}, "Select bearer service type", NULL},
	{AT_SIM5215_CRLP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CRLP",  {"+CRLP:", ""}, "Select radio link protocol parameter", NULL},
	{AT_SIM5215_CR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CR",  {"+CR:", ""}, "Service reporting control", NULL},
	{AT_SIM5215_CEER, AT_OPER_TEST|AT_OPER_EXEC, "AT+CEER",  {"+CEER:", ""}, "Extended error report", NULL},
	{AT_SIM5215_CRC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CRC",  {"+CRC:", ""}, "Set cellular result codes for incoming call indication", NULL},
	{AT_SIM5215_VTS, AT_OPER_TEST|AT_OPER_WRITE, "AT+VTS",  {"+VTS:", ""}, "DTMF and tone generation", NULL},
	{AT_SIM5215_CLVL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLVL",  {"+CLVL:", ""}, "Loud speaker volume level", NULL},
	{AT_SIM5215_VMUTE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+VMUTE",  {"+VMUTE:", ""}, "Speaker mute control", NULL},
	{AT_SIM5215_CMUT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMUT",  {"+CMUT:", ""}, "Microphone mute control", NULL},
	{AT_SIM5215_AUTOANSWER, AT_OPER_READ|AT_OPER_WRITE, "AT+AUTOANSWER",  {"+AUTOANSWER:", ""}, "Automatic answer quickly", NULL},
	{AT_SIM5215_S0, AT_OPER_READ|AT_OPER_WRITE, "ATS0",  {"", ""}, "Set number of rings before automatically answering the call", is_str_digit},
	{AT_SIM5215_CALM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CALM",  {"+CALM:", ""}, "Alert sound mode", NULL},
	{AT_SIM5215_CRSL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRSL",  {"+CRSL:", ""}, "Ringer sound level", NULL},
	{AT_SIM5215_CSDVC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSDVC",  {"+CSDVC:", ""}, "Switch voice channel device", NULL},
	{AT_SIM5215_CPTONE, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPTONE",  {"+CPTONE:", ""}, "Play tone", NULL},
	{AT_SIM5215_CPCM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPCM",  {"+CPCM:", ""}, "External PCM codec mode configuration", NULL},
	{AT_SIM5215_CPCMFMT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPCMFMT",  {"+CPCMFMT:", ""}, "Change the PCM format", NULL},
	{AT_SIM5215_CPCMREG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPCMREG",  {"+CPCMREG:", ""}, "Control PCM data transfer by diagnostics port", NULL},
	{AT_SIM5215_VTD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+VTD",  {"+VTD:", ""}, "Tone duration", NULL},

	// Video Call Related Commands
	{AT_SIM5215_VPMAKE, AT_OPER_WRITE, "AT+VPMAKE",  {"", ""}, "Originate video call", NULL},
	{AT_SIM5215_VPANSWER, AT_OPER_EXEC, "AT+VPANSWER",  {"", ""}, "Answer video call", NULL},
	{AT_SIM5215_VPEND, AT_OPER_EXEC, "AT+VPEND",  {"", ""}, "End video call", NULL},
	{AT_SIM5215_VPDTMF, AT_OPER_TEST|AT_OPER_WRITE, "AT+VPDTMF",  {"+VPDTMF:", ""}, "Send DTMF tone during video call", NULL},
	{AT_SIM5215_VPSOURCE, AT_OPER_TEST|AT_OPER_WRITE, "AT+VPSOURCE",  {"", ""}, "Select video TX source", NULL},
	{AT_SIM5215_VPRECORD, AT_OPER_TEST|AT_OPER_WRITE, "AT+VPRECORD",  {"+VPRECORD:", ""}, "Record video during video call", NULL},
	{AT_SIM5215_VPLOOP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+VPLOOP",  {"+VPLOOP:", ""}, "Loopback far-end video frame during video call", NULL},
	{AT_SIM5215_VPSM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+VPSM",  {"+VPSM:", ""}, "Switch video call to CSD mode", NULL},
	{AT_SIM5215_VPQLTY, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+VPQLTY",  {"+VPQLTY:", ""}, "Set video quality", NULL},

	// SMS Related Commands
	{AT_SIM5215_CSMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMS",  {"+CSMS:", ""}, "Select message service", NULL},
	{AT_SIM5215_CPMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPMS",  {"+CPMS:", ""}, "Preferred SMS message storage", NULL},
	{AT_SIM5215_CMGF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CMGF",  {"+CMGF:", ""}, "Select SMS message format", NULL},
	{AT_SIM5215_CSCA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCA",  {"+CSCA:", ""}, "SMS service center address", NULL},
	{AT_SIM5215_CSCB, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSCB",  {"+CSCB:", ""}, "Select cell broadcast SMS messages", NULL},
	{AT_SIM5215_CSDH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CSDH",  {"+CSDH:", ""}, "Show SMS text mode parameters", NULL},
	{AT_SIM5215_CNMA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CNMA",  {"+CNMA:", ""}, "New message acknowledgement to ME/TA", NULL},
	{AT_SIM5215_CNMI, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CNMI",  {"+CNMI:", ""}, "New SMS message indications", NULL},
	{AT_SIM5215_CMGL, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGL",  {"", ""}, "List SMS messages from preferred store", is_str_xdigit},
	{AT_SIM5215_CMGR, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGR",  {"+CMGR:", ""}, "Read SMS message", is_str_xdigit},
	{AT_SIM5215_CMGS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGS",  {"+CMGS:", ""}, "Send SMS message", NULL},
	{AT_SIM5215_CMSS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMSS",  {"+CMSS:", ""}, "Send SMS message from storage", NULL},
	{AT_SIM5215_CMGW, AT_OPER_TEST|AT_OPER_WRITE, "AT+CMGW",  {"+CMGW:", ""}, "Write SMS message to memory", NULL},
	{AT_SIM5215_CMGD, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGD",  {"+CMGD:", ""}, "Delete SMS message", NULL},
	{AT_SIM5215_CSMP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSMP",  {"+CSMP:", ""}, "Set SMS text mode parameters", NULL},
	{AT_SIM5215_CMGRO, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGRO",  {"+CMGRO:", ""}, "Read message only", NULL},
	{AT_SIM5215_CMGMT, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGMT",  {"+CMGMT:", ""}, "Change message status", NULL},
	{AT_SIM5215_CMVP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMVP",  {"+CMVP:", ""}, "Set message valid period", NULL},
	{AT_SIM5215_CMGRD, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGRD",  {"+CMGRD:", ""}, "Read and delete message", NULL},
	{AT_SIM5215_CMGSO, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGSO",  {"+CMGSO:", ""}, "Send message quickly", NULL},
	{AT_SIM5215_CMGWO, AT_OPER_READ|AT_OPER_WRITE, "AT+CMGWO",  {"+CMGWO:", ""}, "Write message to memory quickly", NULL},

	// Camera Related Commands
	{AT_SIM5215_CCAMS, AT_OPER_EXEC, "AT+CCAMS",  {"", ""}, "Start camera", NULL},
	{AT_SIM5215_CCAME, AT_OPER_EXEC, "AT+CCAME",  {"", ""}, "Stop camera", NULL},
	{AT_SIM5215_CCAMSETD, AT_OPER_WRITE, "AT+CCAMSETD",  {"", ""}, "Set camera dimension", NULL},
	{AT_SIM5215_CCAMSETF, AT_OPER_WRITE, "AT+CCAMSETF",  {"", ""}, "Set camera FPS", NULL},
	{AT_SIM5215_CCAMSETR, AT_OPER_WRITE, "AT+CCAMSETR",  {"", ""}, "Set camera rotation", NULL},
	{AT_SIM5215_CCAMSETN, AT_OPER_WRITE, "AT+CCAMSETN",  {"", ""}, "Set camera night shot mode", NULL},
	{AT_SIM5215_CCAMSETWB, AT_OPER_WRITE, "AT+CCAMSETWB",  {"", ""}, "Set camera white balance", NULL},
	{AT_SIM5215_CCAMSETB, AT_OPER_WRITE, "AT+CCAMSETB",  {"", ""}, "Set camera brightness", NULL},
	{AT_SIM5215_CCAMSETZ, AT_OPER_TEST|AT_OPER_WRITE, "AT+CCAMSETZ",  {"+CCAMSETZ:", ""}, "Set camera zoom", NULL},
	{AT_SIM5215_CCAMTP, AT_OPER_EXEC, "AT+CCAMTP",  {"", ""}, "Take picture", NULL},
	{AT_SIM5215_CCAMEP, AT_OPER_EXEC, "AT+CCAMEP",  {"", ""}, "Save picture", NULL},
	{AT_SIM5215_CCAMRS, AT_OPER_EXEC, "AT+CCAMRS",  {"", ""}, "Start video record", NULL},
	{AT_SIM5215_CCAMRP, AT_OPER_EXEC, "AT+CCAMRP",  {"", ""}, "Pause video record", NULL},
	{AT_SIM5215_CCAMRR, AT_OPER_EXEC, "AT+CCAMRR",  {"", ""}, "Resume video record", NULL},
	{AT_SIM5215_CCAMRE, AT_OPER_EXEC, "AT+CCAMRE",  {"", ""}, "Stop video record", NULL},

	// Audio Application Commands
	{AT_SIM5215_CQCPREC, AT_OPER_WRITE, "AT+CQCPREC",  {"", ""}, "Start recording sound clips", NULL},
	{AT_SIM5215_CQCPPAUSE, AT_OPER_EXEC, "AT+CQCPPAUSE",  {"", ""}, "Pause sound record", NULL},
	{AT_SIM5215_CQCPRESUME, AT_OPER_EXEC, "AT+CQCPRESUME",  {"", ""}, "Resume sound record", NULL},
	{AT_SIM5215_CQCPSTOP, AT_OPER_EXEC, "AT+CQCPSTOP",  {"", ""}, "Stop sound record", NULL},
	{AT_SIM5215_CCMXPLAY, AT_OPER_WRITE, "AT+CCMXPLAY",  {"", ""}, "Play audio file", NULL},
	{AT_SIM5215_CCMXPAUSE, AT_OPER_EXEC, "AT+CCMXPAUSE",  {"", ""}, "Pause playing audio file", NULL},
	{AT_SIM5215_CCMXRESUME, AT_OPER_EXEC, "AT+CCMXRESUME",  {"", ""}, "Resume playing audio file", NULL},
	{AT_SIM5215_CCMXSTOP, AT_OPER_EXEC, "AT+CCMXSTOP",  {"", ""}, "Stop playing audio file", NULL},

	// Network Service Related Commands
	{AT_SIM5215_CREG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CREG",  {"+CREG:", ""}, "Network registration", NULL},
	{AT_SIM5215_COPS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+COPS",  {"+COPS:", ""}, "Operator selection", NULL},
	{AT_SIM5215_CLCK, AT_OPER_TEST|AT_OPER_WRITE, "AT+CLCK",  {"+CLCK:", ""}, "Facility lock", NULL},
	{AT_SIM5215_CPWD, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPWD",  {"+CPWD:", ""}, "Change password", NULL},
	{AT_SIM5215_CLIP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CLIP",  {"+CLIP:", ""}, "Calling line identification presentation", NULL},
	{AT_SIM5215_CLIR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CLIR",  {"+CLIR:", ""}, "Calling line identification restriction", NULL},
	{AT_SIM5215_COLP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+COLP",  {"+COLP:", ""}, "Connected line identification presentation", NULL},
	{AT_SIM5215_CCUG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CCUG",  {"+CCUG:", ""}, "Closed user group control", NULL},
	{AT_SIM5215_CCFC, AT_OPER_TEST|AT_OPER_WRITE, "AT+CCFC",  {"+CCFC:", ""}, "Call forwarding number and conditions control", NULL},
	{AT_SIM5215_CCWA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CCWA",  {"+CCWA:", ""}, "Call waiting control", NULL},
	{AT_SIM5215_CHLD, AT_OPER_TEST|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CHLD",  {"+CHLD:", ""}, "Call hold and multiparty", NULL},
	{AT_SIM5215_CUSD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CUSD",  {"+CUSD:", ""}, "Unstructured supplementary service data", NULL},
	{AT_SIM5215_CAOC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CAOC",  {"+CAOC:", ""}, "Advice of charge", NULL},
	{AT_SIM5215_CSSN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSSN",  {"+CSSN:", ""}, "Supplementary services notification", NULL},
	{AT_SIM5215_CLCC, AT_OPER_TEST|AT_OPER_EXEC, "AT+CLCC",  {"+CLCC:", ""}, "List current calls of ME", NULL},
	{AT_SIM5215_CPOL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPOL",  {"+CPOL:", ""}, "Preferred operator list", NULL},
	{AT_SIM5215_COPN, AT_OPER_TEST|AT_OPER_EXEC, "AT+COPN",  {"+COPN:", ""}, "Read operator names", NULL},
	{AT_SIM5215_CNMP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CNMP",  {"+CNMP:", ""}, "Preferred mode selection", NULL},
	{AT_SIM5215_CNBP, AT_OPER_TEST|AT_OPER_WRITE, "AT+CNBP",  {"+CNBP:", ""}, "Preferred band selection", NULL},
	{AT_SIM5215_CNAOP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CNAOP",  {"+CNAOP:", ""}, "Acquisitions order preference", NULL},
	{AT_SIM5215_CNSDP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CNSDP",  {"+CNSDP:", ""}, "Preferred service domain selection", NULL},
	{AT_SIM5215_CPSI, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPSI",  {"+CPSI:", ""}, "Inquiring UE system information", NULL},
	{AT_SIM5215_CNSMOD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CNSMOD",  {"+CNSMOD:", ""}, "Show network system mode", NULL},
	{AT_SIM5215_CTZU, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CTZU",  {"+CTZU:", ""}, "Automatic time and time zone update", NULL},
	{AT_SIM5215_CTZR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CTZR",  {"+CTZR:", ""}, "Time and time Zone Reporting", NULL},
	{AT_SIM5215_CCINFO, AT_OPER_TEST|AT_OPER_EXEC, "AT+CCINFO",  {"+CCINFO:", ""}, "Show cell system information", NULL},
	{AT_SIM5215_CSCHN, AT_OPER_TEST|AT_OPER_EXEC, "AT+CSCHN",  {"+CSCHN:", ""}, "Show cell channel information", NULL},
	{AT_SIM5215_CSPR, AT_OPER_TEST|AT_OPER_EXEC, "AT+CSPR",  {"+CSPR:", ""}, "Show serving cell radio parameter", NULL},
	{AT_SIM5215_CRUS, AT_OPER_TEST|AT_OPER_EXEC, "AT+CRUS",  {"+CRUS:", ""}, "Show cell set system information", NULL},

	// Mobile Equipment Control and Status Commands
	{AT_SIM5215_CMEE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CMEE",  {"+CMEE:", ""}, "Report mobile equipment error", NULL},
	{AT_SIM5215_CPAS, AT_OPER_TEST|AT_OPER_EXEC, "AT+CPAS",  {"+CPAS:", ""}, "Mobile equipment activity status", NULL},
	{AT_SIM5215_CFUN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CFUN",  {"+CFUN:", ""}, "Set phone functionality", NULL},
	{AT_SIM5215_CPIN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPIN",  {"+CPIN:", ""}, "Enter PIN", NULL},
	{AT_SIM5215_CSQ, AT_OPER_TEST|AT_OPER_EXEC, "AT+CSQ",  {"+CSQ:", ""}, "Signal quality report", NULL},
	{AT_SIM5215_AUTOCSQ, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+AUTOCSQ",  {"+AUTOCSQ:", ""}, "Set CSQ report", NULL},
	{AT_SIM5215_CACM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CACM",  {"+CACM:", ""}, "Accumulated call meter(ACM) reset or query", NULL},
	{AT_SIM5215_CAMM, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CAMM",  {"+CAMM:", ""}, "Accumulated call meter maximum(ACMMAX) set or query", NULL},
	{AT_SIM5215_CPUC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPUC",  {"+CPUC:", ""}, "Price per unit currency table", NULL},
	{AT_SIM5215_CPOF, AT_OPER_EXEC, "AT+CPOF",  {"", ""}, "Control phone to power down", NULL},
	{AT_SIM5215_CCLK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCLK",  {"+CCLK:", ""}, "Real time clock", NULL},
	{AT_SIM5215_CRFEN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRFEN",  {"+CRFEN:", ""}, "RF check at initialization", NULL},
	{AT_SIM5215_CRESET, AT_OPER_TEST|AT_OPER_EXEC, "AT+CRESET",  {"+CRESET:", ""}, "Reset ME", NULL},
	{AT_SIM5215_SIMEI, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+SIMEI",  {"+SIMEI:", ""}, "Set module IMEI", NULL},
	{AT_SIM5215_CSIMLOCK, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSIMLOCK",  {"+CSIMLOCK:", ""}, "Request and change password", NULL},
	{AT_SIM5215_DSWITCH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+DSWITCH",  {"+DSWITCH:", ""}, "Change diagnostics port mode", NULL},
	{AT_SIM5215_CPASSMGR, AT_OPER_WRITE, "AT+CPASSMGR",  {"", ""}, "Manages password", NULL},
	{AT_SIM5215_CPLMNWLIST, AT_OPER_READ|AT_OPER_WRITE, "AT+CPLMNWLIST",  {"+CPLMNWLIST:", ""}, "Manages PLMNs allowed by user", NULL},

	// SIM Related Commands
	{AT_SIM5215_CICCID, AT_OPER_TEST|AT_OPER_EXEC, "AT+CICCID",  {"+ICCID:", ""}, "Read ICCID in SIM card", NULL},
	{AT_SIM5215_CSIM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CSIM",  {"+CSIM:", ""}, "Generic SIM access", NULL},
	{AT_SIM5215_CRSM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRSM",  {"+CRSM:", ""}, "Restricted SIM access", NULL},
	{AT_SIM5215_CSIMSEL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSIMSEL",  {"+CSIMSEL:", ""}, "Switch between two SIM card", NULL},
	{AT_SIM5215_SPIC, AT_OPER_TEST|AT_OPER_EXEC, "AT+SPIC",  {"+SPIC:", ""}, "Times remain to input SIM PIN/PUK", NULL},

	// Hardware Related Commands
	{AT_SIM5215_CTXGAIN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CTXGAIN",  {"+CTXGAIN:", ""}, "Set TX gain", NULL},
	{AT_SIM5215_CRXGAIN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRXGAIN",  {"+CRXGAIN:", ""}, "Set RX gain", NULL},
	{AT_SIM5215_CTXVOL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CTXVOL",  {"+CTXVOL:", ""}, "Set TX volume", NULL},
	{AT_SIM5215_CRXVOL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRXVOL",  {"+CRXVOL:", ""}, "Set RX volume", NULL},
	{AT_SIM5215_CTXFTR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CTXFTR",  {"+CTXFTR:", ""}, "Set TX filter", NULL},
	{AT_SIM5215_CRXFTR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CRXFTR",  {"+CRXFTR:", ""}, "Set RX filter", NULL},
	{AT_SIM5215_CVALARM, AT_OPER_TEST|AT_OPER_WRITE, "AT+CVALARM",  {"+CVALARM:", ""}, "Low voltage Alarm", NULL},
	{AT_SIM5215_CRIIC, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRIIC",  {"+CRIIC:", ""}, "Read values from register of IIC device", NULL},
	{AT_SIM5215_CWIIC, AT_OPER_TEST|AT_OPER_WRITE, "AT+CWIIC",  {"+CWIIC:", ""}, "Write values from register of IIC device", NULL},
	{AT_SIM5215_CVAUXS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CVAUXS",  {"+CVAUXS:", ""}, "Set state of the pin named VREG_AUX1", NULL},
	{AT_SIM5215_CVAUXV, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CVAUXV",  {"+CVAUXV:", ""}, "Set voltage value of the pin named VREG_AUX1", NULL},
	{AT_SIM5215_CGPIO, AT_OPER_WRITE, "AT+CGPIO",  {"", ""}, "Set Trigger mode of interrupt GPIO", NULL},
	{AT_SIM5215_CGDRT, AT_OPER_WRITE, "AT+CGDRT",  {"", ""}, "Set the direction of specified GPIO", NULL},
	{AT_SIM5215_CGSETV, AT_OPER_WRITE, "AT+CGSETV",  {"", ""}, "Set the value of specified GPIO", NULL},
	{AT_SIM5215_CGGETV, AT_OPER_WRITE, "AT+CGGETV",  {"+CGGETV:", ""}, "Get the value of specified GPIO", NULL},
	{AT_SIM5215_CADC, AT_OPER_TEST|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CADC",  {"+CADC:", ""}, "Read ADC value", NULL},
	{AT_SIM5215_CMICAMP1, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CMICAMP1",  {"+CMICAMP1:", ""}, "Set value of micamp1", NULL},
	{AT_SIM5215_CVLVL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CVLVL",  {"+CVLVL:", ""}, "Set value of sound level", NULL},
	{AT_SIM5215_SIDET, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+SIDET",  {"+SIDET:", ""}, "Change the side tone gain level", NULL},
	{AT_SIM5215_CRIRS, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRIRS",  {"+CRIRS:", ""}, "Reset RI pin of serial port", NULL},
	{AT_SIM5215_CSUART, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CSUART",  {"+CSUART:", ""}, "Switch UART line mode", NULL},
	{AT_SIM5215_CDCDMD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CDCDMD",  {"+CDCDMD:", ""}, "Set DCD pin mode", NULL},
	{AT_SIM5215_CDCDVL, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CDCDVL",  {"+CDCDVL:", ""}, "Set DCD pin high-low in GPIO mode", NULL},
	{AT_SIM5215_CCGSWT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCGSWT",  {"+CCGSWT:", ""}, "Switch between camera interface and GPIO", NULL},
	{AT_SIM5215_CPMVT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CPMVT",  {"+CPMVT:", ""}, "Set threshold voltage", NULL},
	{AT_SIM5215_CUSBSPD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CUSBSPD",  {"+CUSBSPD:", ""}, "Switch USB high or full speed", NULL},
	{AT_SIM5215_CCAMMD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CCAMMD",  {"+CCAMMD:", ""}, "Switch the AK8856 mode", NULL},

	// Phonebook Related Commands
	{AT_SIM5215_CNUM, AT_OPER_TEST|AT_OPER_EXEC, "AT+CNUM",  {"+CNUM:", ""}, "Subscriber number", NULL},
	{AT_SIM5215_CPBS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CPBS",  {"+CPBS:", ""}, "Select phonebook memory storage", NULL},
	{AT_SIM5215_CPBR, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBR",  {"+CPBR:", ""}, "Read current phonebook entries", NULL},
	{AT_SIM5215_CPBF, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBF",  {"+CPBF:", ""}, "Find phonebook entries", NULL},
	{AT_SIM5215_CPBW, AT_OPER_TEST|AT_OPER_WRITE, "AT+CPBW",  {"+CPBW:", ""}, "Write phonebook entry", NULL},
	{AT_SIM5215_CEMNLIST, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CEMNLIST",  {"+CEMNLIST:", ""}, "Set the list of emergency number", NULL},

	// File System Related Commands
	{AT_SIM5215_FSCD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+FSCD",  {"+FSCD:", ""}, "Select directory as current directory", NULL},
	{AT_SIM5215_FSMKDIR, AT_OPER_TEST|AT_OPER_WRITE, "AT+FSMKDIR",  {"+FSMKDIR:", ""}, "Make new directory in current directory", NULL},
	{AT_SIM5215_FSRMDIR, AT_OPER_TEST|AT_OPER_WRITE, "AT+FSRMDIR",  {"+FSRMDIR:", ""}, "Delete directory in current directory", NULL},
	{AT_SIM5215_FSLS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+FSLS",  {"+FSLS:", ""}, "List directories/files in current directory", NULL},
	{AT_SIM5215_FSDEL, AT_OPER_TEST|AT_OPER_WRITE, "AT+FSDEL",  {"+FSDEL:", ""}, "Delete file in current directory", NULL},
	{AT_SIM5215_FSRENAME, AT_OPER_TEST|AT_OPER_WRITE, "AT+FSRENAME",  {"+FSRENAME:", ""}, "Rename file in current directory", NULL},
	{AT_SIM5215_FSATTRI, AT_OPER_TEST|AT_OPER_WRITE, "AT+FSATTRI",  {"+FSATTRI:", ""}, "Request file attributes", NULL},
	{AT_SIM5215_FSMEM, AT_OPER_TEST|AT_OPER_EXEC, "AT+FSMEM",  {"+FSMEM:", ""}, "Check the size of available memory", NULL},
	{AT_SIM5215_FSFMT, AT_OPER_TEST|AT_OPER_EXEC, "AT+FSFMT",  {"+FSFMT:", ""}, "Format the storage card", NULL},
	{AT_SIM5215_FSLOCA, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+FSLOCA",  {"+FSLOCA:", ""}, "Select storage place", NULL},

	// File Transmission Related Commands
	{AT_SIM5215_CTXFILE, AT_OPER_TEST|AT_OPER_WRITE, "AT+CTXFILE",  {"+CTXFILE:", ""}, "Select file transmitted to PC host", NULL},
	{AT_SIM5215_CRXFILE, AT_OPER_TEST|AT_OPER_WRITE, "AT+CRXFILE",  {"+CRXFILE:", ""}, "Set name of file received from PC host", NULL},

	// V24-V25 Commands
	{AT_SIM5215_IPR, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+IPR",  {"+IPR:", ""}, "Set local baud rate temporarily", NULL},
	{AT_SIM5215_IPREX, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+IPREX",  {"+IPREX:", ""}, "Set local baud rate permanently", NULL},
	{AT_SIM5215_ICF, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+ICF",  {"+ICF:", ""}, "Set control character framing", NULL},
	{AT_SIM5215_IFC, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+IFC",  {"+IFC:", ""}, "Set local data flow control", NULL},
	{AT_SIM5215_andC, AT_OPER_EXEC, "AT&C",  {"", ""}, "Set DCD function mode", NULL},
	{AT_SIM5215_E, AT_OPER_EXEC, "ATE",  {"", ""}, "Set command echo mode", NULL},
	{AT_SIM5215_andV, AT_OPER_EXEC, "AT&V",  {"", ""}, "Display current configuration", NULL},

	// Commands for Packet Domain
	{AT_SIM5215_CGDCONT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGDCONT",  {"+CGDCONT:", ""}, "Define PDP Context", NULL},
	{AT_SIM5215_CGQREQ, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGQREQ",  {"+CGQREQ:", ""}, "Quality of Service Profile (Requested)", NULL},
	{AT_SIM5215_CGEQREQ, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGEQREQ",  {"+CGEQREQ:", ""}, "3G Quality of Service Profile (Requested)", NULL},
	{AT_SIM5215_CGQMIN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGQMIN",  {"+CGQMIN:", ""}, "Quality of Service Profile (Minimum acceptable)", NULL},
	{AT_SIM5215_CGEQMIN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGEQMIN",  {"+CGEQMIN:", ""}, "3G Quality of Service Profile (Minimum acceptable)", NULL},
	{AT_SIM5215_CGATT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CGATT",  {"+CGATT:", ""}, "Packet Domain attach or detach", NULL},
	{AT_SIM5215_CGACT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CGACT",  {"+CGACT:", ""}, "PDP context activate or deactivate", NULL},
	{AT_SIM5215_CGDATA, AT_OPER_TEST|AT_OPER_WRITE, "AT+CGDATA",  {"+CGDATA:", ""}, "Enter data state", NULL},
	{AT_SIM5215_CGPADDR, AT_OPER_TEST|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGPADDR",  {"+CGPADDR:", ""}, "Show PDP address", NULL},
	{AT_SIM5215_CGCLASS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGCLASS",  {"+CGCLASS:", ""}, "GPRS mobile station class", NULL},
	{AT_SIM5215_CGEREP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGEREP",  {"+CGEREP:", ""}, "GPRS event reporting", NULL},
	{AT_SIM5215_CGREG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGREG",  {"+CGREG:", ""}, "GPRS network registration status", NULL},
	{AT_SIM5215_CGSMS, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CGSMS",  {"+CGSMS:", ""}, "Select service for MO SMS messages", NULL},
	{AT_SIM5215_CGAUTH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGAUTH",  {"+CGAUTH:", ""}, "Set type of authentication for PDP-IP connections of GPRS", NULL},

	// TCP/IP Related Commands
	{AT_SIM5215_CGSOCKCONT, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CGSOCKCONT",  {"+CGSOCKCONT:", ""}, "Define socket PDP Context", NULL},
	{AT_SIM5215_CSOCKSETPN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CSOCKSETPN",  {"+CSOCKSETPN:", ""}, "Set active PDP context’s profile number", NULL},
	{AT_SIM5215_CSOCKAUTH, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CSOCKAUTH",  {"+CSOCKAUTH:", ""}, "Set type of authentication for PDP-IP connections of socket", NULL},
	{AT_SIM5215_IPADDR, AT_OPER_TEST|AT_OPER_EXEC, "AT+IPADDR",  {"+IPADDR:", ""}, "Inquire socket PDP address", NULL},
	{AT_SIM5215_NETOPEN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+NETOPEN",  {"+NETOPEN:", ""}, "Open socket", NULL},
	{AT_SIM5215_TCPCONNECT, AT_OPER_TEST|AT_OPER_WRITE, "AT+TCPCONNECT",  {"+TCPCONNECT:", ""}, "Establish TCP connection", NULL},
	{AT_SIM5215_TCPWRITE, AT_OPER_TEST|AT_OPER_WRITE, "AT+TCPWRITE",  {"+TCPWRITE:", ""}, "Send TCP data", NULL},
	{AT_SIM5215_UDPSEND, AT_OPER_TEST|AT_OPER_WRITE, "AT+UDPSEND",  {"+UDPSEND:", ""}, "Send UDP data", NULL},
	{AT_SIM5215_SERVERSTART, AT_OPER_TEST|AT_OPER_EXEC, "AT+SERVERSTART",  {"+SERVERSTART:", ""}, "Startup TCP server", NULL},
	{AT_SIM5215_LISTCLIENT, AT_OPER_TEST|AT_OPER_WRITE, "AT+LISTCLIENT",  {"+LISTCLIENT:", ""}, "List all of clients’ information", NULL},
	{AT_SIM5215_CLOSECLIENT, AT_OPER_TEST|AT_OPER_WRITE, "AT+CLOSECLIENT",  {"+CLOSECLIENT:", ""}, "Disconnect specified client", NULL},
	{AT_SIM5215_ACTCLIENT, AT_OPER_TEST|AT_OPER_WRITE, "AT+ACTCLIENT",  {"+ACTCLIENT:", ""}, "Activate specified client", NULL},
	{AT_SIM5215_NETCLOSE, AT_OPER_TEST|AT_OPER_EXEC, "AT+NETCLOSE",  {"+NETCLOSE:", ""}, "Close socket", NULL},
	{AT_SIM5215_CIPHEAD, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CIPHEAD",  {"+CIPHEAD:", ""}, "Add an IP head when receiving data", NULL},
	{AT_SIM5215_CIPSRIP, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CIPSRIP",  {"+CIPSRIP:", ""}, "Set whether display IP address and port of sender when receiving data", NULL},
	{AT_SIM5215_CIPCCFG, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE|AT_OPER_EXEC, "AT+CIPCCFG",  {"+CIPCCFG:", ""}, "Configure parameters of socket", NULL},
	{AT_SIM5215_CIPOPEN, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CIPOPEN",  {"+CIPOPEN:", ""}, "Establish connection in multi-client mode", NULL},
	{AT_SIM5215_CIPSEND, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CIPSEND",  {"+CIPSEND:", ""}, "Send data in multi-client mode", NULL},
	{AT_SIM5215_CIPCLOSE, AT_OPER_TEST|AT_OPER_READ|AT_OPER_WRITE, "AT+CIPCLOSE",  {"+CIPCLOSE:", ""}, "Close connection in Multi-client mode", NULL},

	// SIM Application Toolkit (SAT) Commands
	{AT_SIM5215_STIN, AT_OPER_TEST|AT_OPER_READ, "AT+STIN",  {"+STIN:", ""}, "SAT Indication", NULL},
	{AT_SIM5215_STGI, AT_OPER_TEST|AT_OPER_WRITE, "AT+STGI",  {"+STGI:", ""}, "Get SAT information", NULL},
	{AT_SIM5215_STGR, AT_OPER_TEST|AT_OPER_WRITE, "AT+STGR",  {"+STGR:", ""}, "SAT respond", NULL},
};

size_t sim5215_at_com_list_length()
{
	return sizeof(sim5215_at_com_list)/sizeof(sim5215_at_com_list[0]);
}

/******************************************************************************/
/* end of sim5215.c                                                           */
/******************************************************************************/
