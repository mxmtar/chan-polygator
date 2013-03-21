/******************************************************************************/
/* sim5215.h                                                                  */
/******************************************************************************/

#ifndef __SIM5215_H__
#define __SIM5215_H__

#include <sys/types.h>

#include "at.h"

//------------------------------------------------------------------------------
// SIM5215 AT commands V1.05
enum {
	// Unknown
	AT_SIM5215_UNKNOWN = AT_UNKNOWN,

	// General Commands
	AT_SIM5215_I = AT_I,					// ATI (V.25ter) - Display product identification information - doc 3.1 exec
	AT_SIM5215_CGMI = AT_CGMI,				// AT+CGMI (3GPP TS 27.007) - Request manufacturer identification - doc 3.2 test exec
	AT_SIM5215_CGMM = AT_CGMM,				// AT+CGMM (3GPP TS 27.007) - Request model identification - doc 3.3 test exec
	AT_SIM5215_CGMR = AT_CGMR,				// AT+CGMR (3GPP TS 27.007) - Request TA revision identification of software release - doc 3.4 test exec
	AT_SIM5215_CGSN = AT_CGSN,				// AT+CGSN (3GPP TS 27.007) - Request product serial number identification - doc 3.5 test exec
	AT_SIM5215_CSCS = AT_CSCS,				// AT+CSCS (3GPP TS 27.007) - Select TE character set - doc 3.6 test read write exec
	AT_SIM5215_CIMI = AT_CIMI,				// AT+CIMI (3GPP TS 27.007) - Request international mobile subscriber identity - doc 3.7 test exec
	AT_SIM5215_GCAP = AT_GCAP,				// AT+GCAP (V.25ter) - Request complete TA capabilities list - doc 3.8 test exec
	AT_SIM5215_CATR = 10000,				// AT+CATR (Vendor) - Configure URC destination interface - doc 3.9 test read write
	AT_SIM5215_A_SLASH = AT_A_SLASH,		// A/ (V.25ter) - Repeat last command - doc 3.10 exec

	// Call Control Commands and Methods
	AT_SIM5215_CSTA = AT_CSTA,				// AT+CSTA (3GPP TS 27.007) - Select type of address - doc 4.1 test read write exec
	AT_SIM5215_CMOD = AT_CMOD,				// AT+CMOD (3GPP TS 27.007) - Call mode - doc 4.2 test read write exec
	AT_SIM5215_D = AT_D,					// ATD (V.25ter) - Dial command - doc 4.3 exec
	AT_SIM5215_D_MEM = AT_D_MEM,			// ATD><mem><N> (V.25ter) - Originate call from specified memory - doc 4.4 exec
	AT_SIM5215_D_CURMEM = AT_D_CURMEM,		// ATD><N> (V.25ter) - Originate call to phone number in current memory -  doc 4.5 exec
	AT_SIM5215_D_PHBOOK = AT_D_PHBOOK,		// ATD><STR> (V.25ter) - Originate call to phone number in memory which corresponds to field <STR> - doc 4.6 exec
	AT_SIM5215_A = AT_A,					// ATA (V.25ter) - Answer an incoming call - doc 4.7 exec
	AT_SIM5215_3PLUS = AT_3PLUS,			// +++ (V.25ter) - Switch from data mode or PPP online mode to command mode - doc 4.8 exec
	AT_SIM5215_O = AT_O,					// ATO (V.25ter) - Switch from command mode to data mode - doc 4.9 exec
	AT_SIM5215_CVHU = AT_CVHU,				// AT+CVHU (3GPP TS 27.007) - Voice hang up control - doc 4.10 test read write exec
	AT_SIM5215_H = AT_H,					// ATH (V.25ter) - Disconnect existing connection - doc 4.11 exec
	AT_SIM5215_CHUP = AT_CHUP,				// AT+CHUP (3GPP TS 27.007) - Hang up control - doc 4.12 test exec
	AT_SIM5215_CBST = AT_CBST,				// AT+CBST (3GPP TS 27.007) - Select bearer service type - doc 4.13 test read write exec
	AT_SIM5215_CRLP = AT_CRLP,				// AT+CRLP (3GPP TS 27.007) - Select radio link protocol parameter - doc 4.14 test read write exec
	AT_SIM5215_CR = AT_CR,					// AT+CR (3GPP TS 27.007) - Service reporting control - doc 4.15 test read write exec
	AT_SIM5215_CEER = AT_CEER,				// AT+CEER (3GPP TS 27.007) - Extended error report - doc 4.16 test exec
	AT_SIM5215_CRC = AT_CRC,				// AT+CRC (3GPP TS 27.007) - Set cellular result codes for incoming call indication - doc 4.17 test read write exec
	AT_SIM5215_VTS = AT_VTS,				// AT+VTS (3GPP TS 27.007) - DTMF and tone generation - doc 4.18 test write
	AT_SIM5215_CLVL = AT_CLVL,				// AT+CLVL (3GPP TS 27.007) - Loud speaker volume level - doc 4.19 test read write
	AT_SIM5215_VMUTE = 10001,				// AT+VMUTE (Vendor) - Speaker mute control - doc 4.20 test read write
	AT_SIM5215_CMUT = AT_CMUT,				// AT+CMUT (3GPP TS 27.007) - Microphone mute control - doc 4.21 test read write
	AT_SIM5215_AUTOANSWER = 10002,			// AT+AUTOANSWER (Vendor) - Automatic answer quickly - doc 4.22 read write
	AT_SIM5215_S0 = AT_S0,					// ATS0 (V.25ter) - Set number of rings before automatically answering the call - doc 4.23 read write
	AT_SIM5215_CALM = AT_CALM,				// AT+CALM (3GPP TS 27.007) - Alert sound mode - doc 4.24 test read write
	AT_SIM5215_CRSL = AT_CRSL,				// AT+CRSL (3GPP TS 27.007) - Ringer sound level - doc 4.25 test read write
	AT_SIM5215_CSDVC = 10003,				// AT+CSDVC (Vendor) - Switch voice channel device - doc 4.26 test read write
	AT_SIM5215_CPTONE = 10004,				// AT+CPTONE (Vendor) - Play tone - doc 4.27 test write
	AT_SIM5215_CPCM = 10005,				// AT+CPCM (Vendor) - External PCM codec mode configuration - doc 4.28 test read write
	AT_SIM5215_CPCMFMT = 10006,				// AT+CPCMFMT (Vendor) - Change the PCM format - doc 4.29 test read write
	AT_SIM5215_CPCMREG = 10007,				// AT+CPCMREG (Vendor) - Control PCM data transfer by diagnostics port - doc 4.30 test read write
	AT_SIM5215_VTD = AT_VTD,				// AT+VTD (3GPP TS 27.007) - Tone duration - doc 4.31 test read write

	// Video Call Related Commands
	AT_SIM5215_VPMAKE = 10008,				// AT+VPMAKE (Vendor) - Originate video call - doc 5.1 write
	AT_SIM5215_VPANSWER = 10009,			// AT+VPANSWER (Vendor) - Answer video call - doc 5.2 exec
	AT_SIM5215_VPEND = 10010,				// AT+VPEND (Vendor) - End video call - doc 5.3 exec
	AT_SIM5215_VPDTMF = 10011,				// AT+VPDTMF (Vendor) - Send DTMF tone during video call - doc 5.4 test write
	AT_SIM5215_VPSOURCE = 10012,			// AT+VPSOURCE (Vendor) - Select video TX source - doc 5.5 test write
	AT_SIM5215_VPRECORD = 10013,			// AT+VPRECORD (Vendor) - Record video during video call - doc 5.6 test write
	AT_SIM5215_VPLOOP = 10014,				// AT+VPLOOP (Vendor) - Loopback far-end video frame during video call - doc 5.7 test read write
	AT_SIM5215_VPSM = 10015,				// AT+VPSM (Vendor) - Switch video call to CSD mode - doc 5.8  test read write
	AT_SIM5215_VPQLTY = 10016,				// AT+VPQLTY (Vendor) - Set video quality - doc 5.9 test read write

	// SMS Related Commands
	AT_SIM5215_CSMS = AT_CSMS,				// AT+CSMS (3GPP TS 27.005) - Select message service - doc 6.2 test read write
	AT_SIM5215_CPMS = AT_CPMS,				// AT+CPMS (3GPP TS 27.005) - Preferred SMS message storage - doc 6.3 test read write
	AT_SIM5215_CMGF = AT_CMGF,				// AT+CMGF (3GPP TS 27.005) - Select SMS message format - doc 6.4 test read write exec
	AT_SIM5215_CSCA = AT_CSCA,				// AT+CSCA (3GPP TS 27.005) - SMS service center address - doc 6.5 test read write
	AT_SIM5215_CSCB = AT_CSCB,				// AT+CSCB (3GPP TS 27.005) - Select cell broadcast SMS messages - doc 6.6 test read write
	AT_SIM5215_CSDH = AT_CSDH,				// AT+CSDH (3GPP TS 27.005) - Show SMS text mode parameters - doc 6.7 test read write exec
	AT_SIM5215_CNMA = AT_CNMA,				// AT+CNMA (3GPP TS 27.005) - New message acknowledgement to ME/TA - doc 6.8 test write exec
	AT_SIM5215_CNMI = AT_CNMI,				// AT+CNMI (3GPP TS 27.005) - New SMS message indications - doc 6.9 test read write exec
	AT_SIM5215_CMGL = AT_CMGL,				// AT+CMGL (3GPP TS 27.005) - List SMS messages from preferred store - doc 6.10 test write
	AT_SIM5215_CMGR = AT_CMGR,				// AT+CMGR (3GPP TS 27.005) - Read SMS message - doc 6.11 test write
	AT_SIM5215_CMGS = AT_CMGS,				// AT+CMGS (3GPP TS 27.005) - Send SMS message - doc 6.12 test write
	AT_SIM5215_CMSS = AT_CMSS,				// AT+CMSS (3GPP TS 27.005) - Send SMS message from storage - doc 6.13 test write
	AT_SIM5215_CMGW = AT_CMGW,				// AT+CMGW (3GPP TS 27.005) - Write SMS message to memory - doc 6.14 test write
	AT_SIM5215_CMGD = AT_CMGD,				// AT+CMGD (3GPP TS 27.005) - Delete SMS message - doc 6.15 test write
	AT_SIM5215_CSMP = AT_CSMP,				// AT+CSMP (3GPP TS 27.005) - Set SMS text mode parameters - doc 6.16 test read write
	AT_SIM5215_CMGRO = 10017,				// AT+CMGRO (Vendor) - Read message only - doc 6.17 test write
	AT_SIM5215_CMGMT = 10018,				// AT+CMGMT (Vendor) - Change message status - doc 6.18 test write
	AT_SIM5215_CMVP = 10019,				// AT+CMVP (Vendor) - Set message valid period - doc 6.19 test read write
	AT_SIM5215_CMGRD = 10020,				// AT+CMGRD (Vendor) - Read and delete message - doc 6.20 test write
	AT_SIM5215_CMGSO = 10021,				// AT+CMGSO (Vendor) - Send message quickly - doc 6.21 test write
	AT_SIM5215_CMGWO = 10022,				// AT+CMGWO (Vendor) - Write message to memory quickly - doc 6.22 test write

	// Camera Related Commands
	AT_SIM5215_CCAMS = 10023,				// AT+CCAMS (Vendor) - Start camera - doc 7.1 exec
	AT_SIM5215_CCAME = 10024,				// AT+CCAME (Vendor) - Stop camera - doc 7.2 exec
	AT_SIM5215_CCAMSETD = 10025,			// AT+CCAMSETD (Vendor) - Set camera dimension - doc 7.3 write
	AT_SIM5215_CCAMSETF = 10026,			// AT+CCAMSETF (Vendor) - Set camera FPS - doc 7.4 write
	AT_SIM5215_CCAMSETR = 10027,			// AT+CCAMSETR (Vendor) - Set camera rotation - doc 7.5 write
	AT_SIM5215_CCAMSETN = 10028,			// AT+CCAMSETN (Vendor) - Set camera night shot mode - doc 7.6 write
	AT_SIM5215_CCAMSETWB = 10029,			// AT+CCAMSETWB (Vendor) - Set camera white balance - doc 7.7 write
	AT_SIM5215_CCAMSETB = 10030,			// AT+CCAMSETB (Vendor) - Set camera brightness - doc 7.8 write
	AT_SIM5215_CCAMSETZ = 10031,			// AT+CCAMSETZ (Vendor) - Set camera zoom - doc 7.9 test write
	AT_SIM5215_CCAMTP = 10032,				// AT+CCAMTP (Vendor) - Take picture - doc 7.10 exec
	AT_SIM5215_CCAMEP = 10033,				// AT+CCAMEP (Vendor) - Save picture - doc 7.11 exec
	AT_SIM5215_CCAMRS = 10034,				// AT+CCAMRS (Vendor) - Start video record - doc 7.12 exec
	AT_SIM5215_CCAMRP = 10035,				// AT+CCAMRP (Vendor) - Pause video record - doc 7.13 exec
	AT_SIM5215_CCAMRR = 10036,				// AT+CCAMRR (Vendor) - Resume video record - doc 7.14 exec
	AT_SIM5215_CCAMRE = 10037,				// AT+CCAMRE (Vendor) - Stop video record - doc 7.15 exec

	// Audio Application Commands
	AT_SIM5215_CQCPREC = 10038,				// AT+CQCPREC (Vendor) - Start recording sound clips - doc 8.1 write
	AT_SIM5215_CQCPPAUSE = 10039,			// AT+CQCPPAUSE (Vendor) - Pause sound record - doc 8.2 exec
	AT_SIM5215_CQCPRESUME = 10040,			// AT+CQCPRESUME (Vendor) - Resume sound record - doc 8.3 exec 
	AT_SIM5215_CQCPSTOP = 10041,			// AT+CQCPSTOP (Vendor) - Stop sound record - doc 8.4 exec
	AT_SIM5215_CCMXPLAY = 10042,			// AT+CCMXPLAY (Vendor) - Play audio file - doc 8.5 write
	AT_SIM5215_CCMXPAUSE = 10043,			// AT+CCMXPAUSE (Vendor) - Pause playing audio file - doc 8.6 exec
	AT_SIM5215_CCMXRESUME = 10044,			// AT+CCMXRESUME (Vendor) - Resume playing audio file - doc 8.7 exec
	AT_SIM5215_CCMXSTOP = 10045,			// AT+CCMXSTOP (Vendor) - Stop playing audio file - doc 8.8 exec

	// Network Service Related Commands
	AT_SIM5215_CREG = AT_CREG,				// AT+CREG (3GPP TS 27.007) - Network registration - doc 9.1 test read write exec
	AT_SIM5215_COPS = AT_COPS,				// AT+COPS (3GPP TS 27.007) - Operator selection - doc 9.2 test read write exec
	AT_SIM5215_CLCK = AT_CLCK,				// AT+CLCK (3GPP TS 27.007) - Facility lock - doc 9.3 test write
	AT_SIM5215_CPWD = AT_CPWD,				// AT+CPWD (3GPP TS 27.007) - Change password - doc 9.4 test write
	AT_SIM5215_CLIP = AT_CLIP,				// AT+CLIP (3GPP TS 27.007) - Calling line identification presentation - doc 9.5 test read write exec
	AT_SIM5215_CLIR = AT_CLIR,				// AT+CLIR (3GPP TS 27.007) - Calling line identification restriction - doc 9.6 test read write
	AT_SIM5215_COLP = AT_COLP,				// AT+COLP (3GPP TS 27.007) - Connected line identification presentation - doc 9.7 test read write exec
	AT_SIM5215_CCUG = AT_CCUG,				// AT+CCUG (3GPP TS 27.007) - Closed user group control - doc 9.8 test read write exec
	AT_SIM5215_CCFC = AT_CCFC,				// AT+CCFC (3GPP TS 27.007) - Call forwarding number and conditions control - doc 9.9 test write
	AT_SIM5215_CCWA = AT_CCWA,				// AT+CCWA (3GPP TS 27.007) - Call waiting control - doc 9.10 test read write exec
	AT_SIM5215_CHLD = AT_CHLD,				// AT+CHLD (3GPP TS 27.007) - Call hold and multiparty - doc 9.11 test write exec
	AT_SIM5215_CUSD = AT_CUSD,				// AT+CUSD (3GPP TS 27.007) - Unstructured supplementary service data - doc 9.12 test read write exec
	AT_SIM5215_CAOC = AT_CAOC,				// AT+CAOC (3GPP TS 27.007) - Advice of charge - doc 9.13 test read write exec
	AT_SIM5215_CSSN = AT_CSSN,				// AT+CSSN (3GPP TS 27.007) - Supplementary services notification - doc 9.14 test read write
	AT_SIM5215_CLCC = AT_CLCC,				// AT+CLCC (3GPP TS 27.007) - List current calls of ME - doc 9.15 test exec /* NOTICE read -> exec */
	AT_SIM5215_CPOL = AT_CPOL,				// AT+CPOL (3GPP TS 27.007) - Preferred operator list - doc 9.16 test read write
	AT_SIM5215_COPN = AT_COPN,				// AT+COPN (3GPP TS 27.007) - Read operator names - doc 9.17 test exec /* NOTICE read -> exec */
	AT_SIM5215_CNMP = 10046,				// AT+CNMP (Vendor) - Preferred mode selection - doc 9.18 test read write
	AT_SIM5215_CNBP = 10047,				// AT+CNBP (Vendor) - Preferred band selection - doc 9.19 test write
	AT_SIM5215_CNAOP = 10048,				// AT+CNAOP (Vendor) - Acquisitions order preference - doc 9.20 test read write
	AT_SIM5215_CNSDP = 10049,				// AT+CNSDP (Vendor) - Preferred service domain selection - doc 9.21 test read write
	AT_SIM5215_CPSI = 10050,				// AT+CPSI (Vendor) - Inquiring UE system information - doc 9.22 test read write
	AT_SIM5215_CNSMOD = 10051,				// AT+CNSMOD (Vendor) - Show network system mode - doc 9.23 test read write
	AT_SIM5215_CTZU = AT_CTZU,				// AT+CTZU (3GPP TS 27.007) - Automatic time and time zone update - doc 9.24 test read write
	AT_SIM5215_CTZR = AT_CTZR,				// AT+CTZR (3GPP TS 27.007) - Time and time Zone Reporting - doc 9.25 test read write exec
	AT_SIM5215_CCINFO = 10052,				// AT+CCINFO (Vendor) - Show cell system information - doc 9.26 test exec
	AT_SIM5215_CSCHN = 10053,				// AT+CSCHN (Vendor) - Show cell channel information - doc 9.27 test exec
	AT_SIM5215_CSPR = 10054,				// AT+CSPR (Vendor) - Show serving cell radio parameter - doc 9.28 test exec
	AT_SIM5215_CRUS = 10055,				// AT+CRUS (Vendor) - Show cell set system information - doc 9.29 test exec

	// Mobile Equipment Control and Status Commands
	AT_SIM5215_CMEE = AT_CMEE,				// AT+CMEE (3GPP TS 27.007) - Report mobile equipment error - doc 10.2 test read write exec
	AT_SIM5215_CPAS = AT_CPAS,				// AT+CPAS (3GPP TS 27.007) - Mobile equipment activity status - doc 10.3 test exec
	AT_SIM5215_CFUN = AT_CFUN,				// AT+CFUN (3GPP TS 27.007) - Set phone functionality - doc 10.4 test read write
	AT_SIM5215_CPIN = AT_CPIN,				// AT+CPIN (3GPP TS 27.007) - Enter PIN - doc 10.5 test read write
	AT_SIM5215_CSQ = AT_CSQ,				// AT+CSQ (3GPP TS 27.007) - Signal quality report - doc 10.6 test exec
	AT_SIM5215_AUTOCSQ = 10056,				// AT+AUTOCSQ (Vendor) - Set CSQ report - doc 10.7 test read write
	AT_SIM5215_CACM = AT_CACM,				// AT+CACM (3GPP TS 27.007) - Accumulated call meter(ACM) reset or query - doc 10.8 test read write exec
	AT_SIM5215_CAMM = AT_CAMM,				// AT+CAMM (3GPP TS 27.007) - Accumulated call meter maximum(ACMMAX) set or query - doc 10.9 test read write exec
	AT_SIM5215_CPUC = AT_CPUC,				// AT+CPUC (3GPP TS 27.007) - Price per unit currency table - doc 10.10 test read write
	AT_SIM5215_CPOF = 10057,				// AT+CPOF (Vendor) - Control phone to power down - doc 10.11 exec
	AT_SIM5215_CCLK = AT_CCLK,				// AT+CCLK (3GPP TS 27.007) - Real time clock - doc 10.12 test read write
	AT_SIM5215_CRFEN = 10058,				// AT+CRFEN (Vendor) - RF check at initialization - doc 10.13 test read write
	AT_SIM5215_CRESET = 10059,				// AT+CRESET (Vendor) - Reset ME - doc 10.14 test exec
	AT_SIM5215_SIMEI = 10060,				// AT+SIMEI (Vendor) - Set module IMEI - doc 10.15 test read write
	AT_SIM5215_CSIMLOCK = 10061,			// AT+CSIMLOCK (Vendor) - Request and change password - doc 10.16 test read write
	AT_SIM5215_DSWITCH = 10062,				// AT+DSWITCH (Vendor) - Change diagnostics port mode - doc 10.17 test read write
	AT_SIM5215_CPASSMGR = 10063,			// AT+CPASSMGR (Vendor) - Manages password - doc 10.18 write
	AT_SIM5215_CPLMNWLIST = 10064,			// AT+CPLMNWLIST (Vendor) - Manages PLMNs allowed by user - doc 10.19 read write

	// SIM Related Commands
	AT_SIM5215_CICCID = 10065,				// AT+CICCID (Vendor) - Read ICCID in SIM card - doc 11.1 test exec
	AT_SIM5215_CSIM = AT_CSIM,				// AT+CSIM (3GPP TS 27.007) - Generic SIM access - doc 11.2 test write
	AT_SIM5215_CRSM = AT_CRSM,				// AT+CRSM (3GPP TS 27.007) - Restricted SIM access - doc 11.3 test write
	AT_SIM5215_CSIMSEL = 10066,				// AT+CSIMSEL (Vendor) - Switch between two SIM card - doc 11.4 test read write
	AT_SIM5215_SPIC = 10067,				// AT+SPIC (Vendor) - Times remain to input SIM PIN/PUK - doc 11.5 test exec

	// Hardware Related Commands
	AT_SIM5215_CTXGAIN = 10068,				// AT+CTXGAIN (Vendor) - Set TX gain - doc 12.1 test read write
	AT_SIM5215_CRXGAIN = 10069,				// AT+CRXGAIN (Vendor) - Set RX gain - doc 12.2 test read write
	AT_SIM5215_CTXVOL = 10070,				// AT+CTXVOL (Vendor) - Set TX volume - doc 12.3 test read write
	AT_SIM5215_CRXVOL = 10071,				// AT+CRXVOL (Vendor) - Set RX volume - doc 12.4 test read write
	AT_SIM5215_CTXFTR = 10072,				// AT+CTXFTR (Vendor) - Set TX filter - doc 12.5 test read write
	AT_SIM5215_CRXFTR = 10073,				// AT+CRXFTR (Vendor) - Set RX filter - doc 12.6 test read write
	AT_SIM5215_CVALARM = 10074,				// AT+CVALARM (Vendor) - Low voltage Alarm - doc 12.7 test write
	AT_SIM5215_CRIIC = 10075,				// AT+CRIIC (Vendor) - Read values from register of IIC device - doc 12.8 test write
	AT_SIM5215_CWIIC = 10076,				// AT+CWIIC (Vendor) - Write values to register of IIC device - doc 12.9 test write
	AT_SIM5215_CVAUXS = 10077,				// AT+CVAUXS (Vendor) - Set state of the pin named VREG_AUX1 - doc 12.10 test read write
	AT_SIM5215_CVAUXV = 10078,				// AT+CVAUXV (Vendor) - Set voltage value of the pin named VREG_AUX1 - doc 12.11 test read write
	AT_SIM5215_CGPIO = 10079,				// AT+CGPIO (Vendor) - Set Trigger mode of interrupt GPIO - doc 12.12 write
	AT_SIM5215_CGDRT = 10080,				// AT+CGDRT (Vendor) - Set the direction of specified GPIO - doc 12.13 write
	AT_SIM5215_CGSETV = 10081,				// AT+CGSETV (Vendor) - Set the value of specified GPIO - doc 12.14 write
	AT_SIM5215_CGGETV = 10082,				// AT+CGGETV (Vendor) - Get the value of specified GPIO - doc 12.15 write
	AT_SIM5215_CADC = 10083,				// AT+CADC (Vendor) - Read ADC value - doc 12.16 test write exec
	AT_SIM5215_CMICAMP1 = 10084,			// AT+CMICAMP1 (Vendor) - Set value of micamp1 - doc 12.17 test read write
	AT_SIM5215_CVLVL = 10085,				// AT+CVLVL (Vendor) - Set value of sound level - doc 12.18 test read write
	AT_SIM5215_SIDET = 10086,				// AT+SIDET (Vendor) - Change the side tone gain level - doc 12.19 test read write
	AT_SIM5215_CRIRS = 10087,				// AT+CRIRS (Vendor) - Reset RI pin of serial port - doc 12.20 test write
	AT_SIM5215_CSUART = 10088,				// AT+CSUART (Vendor) - Switch UART line mode - doc 12.21 test read write
	AT_SIM5215_CDCDMD = 10089,				// AT+CDCDMD (Vendor) - Set DCD pin mode - doc 12.22 test read write
	AT_SIM5215_CDCDVL = 10090,				// AT+CDCDVL (Vendor) - Set DCD pin high-low in GPIO mode - doc 12.23 test read write
	AT_SIM5215_CCGSWT = 10091,				// AT+CCGSWT (Vendor) - Switch between camera interface and GPIO - doc 12.24 test read write
	AT_SIM5215_CPMVT = 10092,				// AT+CPMVT (Vendor) - Set threshold voltage - doc 12.25 test read write
	AT_SIM5215_CUSBSPD = 10093,				// AT+CUSBSPD (Vendor) - Switch USB high or full speed - doc 12.26 test read write
	AT_SIM5215_CCAMMD = 10094,				// AT+CCAMMD (Vendor) - Switch the AK8856 mode - doc 12.27 test read write

	// Phonebook Related Commands
	AT_SIM5215_CNUM = AT_CNUM,				// AT+CNUM (3GPP TS 27.007) - Subscriber number - doc 13.1 test exec
	AT_SIM5215_CPBS = AT_CPBS,				// AT+CPBS (3GPP TS 27.007) - Select phonebook memory storage - doc 13.2 test read write exec
	AT_SIM5215_CPBR = AT_CPBR,				// AT+CPBR (3GPP TS 27.007) - Read current phonebook entries - doc 13.3 test write
	AT_SIM5215_CPBF = AT_CPBF,				// AT+CPBF (3GPP TS 27.007) - Find phonebook entries - doc 13.4 test write
	AT_SIM5215_CPBW = AT_CPBW,				// AT+CPBW (3GPP TS 27.007) - Write phonebook entry - doc 13.5 test write
	AT_SIM5215_CEMNLIST = 10095,			// AT+CEMNLIST (Vendor) - Set the list of emergency number - doc 13.6 test read write

	// File System Related Commands
	AT_SIM5215_FSCD = 10096,				// AT+FSCD (Vendor) - Select directory as current directory - doc 14.1 test read write
	AT_SIM5215_FSMKDIR = 10097,				// AT+FSMKDIR (Vendor) - Make new directory in current directory - doc 14.2 test write
	AT_SIM5215_FSRMDIR = 10098,				// AT+FSRMDIR (Vendor) - Delete directory in current directory - doc 14.3 test write
	AT_SIM5215_FSLS = 10099,				// AT+FSLS (Vendor) - List directories/files in current directory - doc 14.4 test read write exec
	AT_SIM5215_FSDEL = 10100,				// AT+FSDEL (Vendor) - Delete file in current directory - doc 14.5 test write
	AT_SIM5215_FSRENAME = 10101,			// AT+FSRENAME (Vendor) - Rename file in current directory - doc 14.6 test write
	AT_SIM5215_FSATTRI = 10102,				// AT+FSATTRI (Vendor) - Request file attributes - doc 14.7 test write
	AT_SIM5215_FSMEM = 10103,				// AT+FSMEM (Vendor) - Check the size of available memory - doc 14.8 test exec
	AT_SIM5215_FSFMT = 10104,				// AT+FSFMT (Vendor) - Format the storage card - doc 14.9 test exec
	AT_SIM5215_FSLOCA = 10105,				// AT+FSLOCA (Vendor) - Select storage place - doc 14.10 test read write

	// File Transmission Related Commands
	AT_SIM5215_CTXFILE = 10106,				// AT+CTXFILE (Vendor) - Select file transmitted to PC host - doc 15.11 test write
	AT_SIM5215_CRXFILE = 10107,				// AT+CRXFILE (Vendor) - Set name of file received from PC host - doc 15.2 test write

	// V24-V25 Commands
	AT_SIM5215_IPR = AT_IPR,				// AT+IPR (V.25ter) - Set local baud rate temporarily - doc 16.1 test read write exec
	AT_SIM5215_IPREX = 10108,				// AT+IPREX (Vendor) - Set local baud rate permanently - doc 16.2 test read write exec
	AT_SIM5215_ICF = 10109,					// AT+ICF (Vendor) - Set control character framing - doc 16.3 test read write exec /* NOTICE maybe V.25ter command */
	AT_SIM5215_IFC = AT_IFC,				// AT+IFC (V.25ter) - Set local data flow control - doc 16.4 test read write exec
	AT_SIM5215_andC = AT_andC,				// AT&C (V.25ter) - Set DCD function mode - doc 16.5 exec
	AT_SIM5215_E = AT_E,					// ATE (V.25ter) - Set command echo mode - doc 16.6 exec
	AT_SIM5215_andV = AT_andV,				// AT&V (V.25ter) - Display current configuration - doc 16.7 exec

	// Commands for Packet Domain
	AT_SIM5215_CGDCONT = 10110,				// AT+CGDCONT (3GPP TS 27.007) - Define PDP Context - doc 17.1 test read write exec
	AT_SIM5215_CGQREQ = 10111,				// AT+CGQREQ (3GPP TS 27.007) - Quality of Service Profile (Requested) - doc 17.2 test read write exec
	AT_SIM5215_CGEQREQ = 10112,				// AT+CGEQREQ (3GPP TS 27.007) - 3G Quality of Service Profile (Requested) - doc 17.3 test read write exec
	AT_SIM5215_CGQMIN = 10113,				// AT+CGQMIN (3GPP TS 27.007) - Quality of Service Profile (Minimum acceptable) - doc 17.4 test read write exec
	AT_SIM5215_CGEQMIN = 10114,				// AT+CGEQMIN (3GPP TS 27.007) - 3G Quality of Service Profile (Minimum acceptable) - doc 17.5 test read write exec
	AT_SIM5215_CGATT = 10115,				// AT+CGATT (3GPP TS 27.007) - Packet Domain attach or detach - doc 17.6 test read write
	AT_SIM5215_CGACT = 10116,				// AT+CGACT (3GPP TS 27.007) - PDP context activate or deactivate - doc 17.7 test read write
	AT_SIM5215_CGDATA = 10117,				// AT+CGDATA (3GPP TS 27.007) - Enter data state - doc 17.8 test write
	AT_SIM5215_CGPADDR = 10118,				// AT+CGPADDR (3GPP TS 27.007) - Show PDP address - doc 17.9 test write exec
	AT_SIM5215_CGCLASS = 10119,				// AT+CGCLASS (3GPP TS 27.007) - GPRS mobile station class - doc 17.10 test read write exec
	AT_SIM5215_CGEREP = 10120,				// AT+CGEREP (3GPP TS 27.007) - GPRS event reporting - doc 17.11 test read write exec
	AT_SIM5215_CGREG = 10121,				// AT+CGREG (3GPP TS 27.007) - GPRS network registration status - doc 17.12 test read write exec
	AT_SIM5215_CGSMS = 10122,				// AT+CGSMS (3GPP TS 27.007) - Select service for MO SMS messages - doc 17.13 test read write
	AT_SIM5215_CGAUTH = 10123,				// AT+CGAUTH (Vendor) - Set type of authentication for PDP-IP connections of GPRS - doc 17.14 test read write exec

	// TCP/IP Related Commands
	AT_SIM5215_CGSOCKCONT = 10124,			// AT+CGSOCKCONT (Vendor) - Define socket PDP Context - doc 18.1 test read write exec
	AT_SIM5215_CSOCKSETPN = 10125,			// AT+CSOCKSETPN (Vendor) - Set active PDP context’s profile number - doc 18.2 test read write exec
	AT_SIM5215_CSOCKAUTH = 10126,			// AT+CSOCKAUTH (Vendor) - Set type of authentication for PDP-IP connections of socket - doc 18.3 test read write exec
	AT_SIM5215_IPADDR = 10127,				// AT+IPADDR (Vendor) - Inquire socket PDP address - doc 18.4 test exec
	AT_SIM5215_NETOPEN = 10128,				// AT+NETOPEN (Vendor) - Open socket - doc 18.5 test read write
	AT_SIM5215_TCPCONNECT = 10129,			// AT+TCPCONNECT (Vendor) - Establish TCP connection - doc 18.6 test write
	AT_SIM5215_TCPWRITE = 10130,			// AT+TCPWRITE (Vendor) - Send TCP data - doc 18.7 test write
	AT_SIM5215_UDPSEND = 10131,				// AT+UDPSEND (Vendor) - Send UDP data - doc 18.8 test write
	AT_SIM5215_SERVERSTART = 10132,			// AT+SERVERSTART (Vendor) - Startup TCP server - doc 18.9 test exec
	AT_SIM5215_LISTCLIENT = 10133,			// AT+LISTCLIENT (Vendor) - List all of clients’ information - doc 18.10 test write
	AT_SIM5215_CLOSECLIENT = 10134,			// AT+CLOSECLIENT (Vendor) - Disconnect specified client - doc 18.11 test write
	AT_SIM5215_ACTCLIENT = 10135,			// AT+ACTCLIENT (Vendor) - Activate specified client - doc 18.12 test write
	AT_SIM5215_NETCLOSE = 10136,			// AT+NETCLOSE (Vendor) - Close socket - doc 18.13 test exec
	AT_SIM5215_CIPHEAD = 10137,				// AT+CIPHEAD (Vendor) - Add an IP head when receiving data - doc 18.14 test read write exec
	AT_SIM5215_CIPSRIP = 10138,				// AT+CIPSRIP (Vendor) - Set whether display IP address and port of sender when receiving data - doc 18.15 test read write exec
	AT_SIM5215_CIPCCFG = 10139,				// AT+CIPCCFG (Vendor) - Configure parameters of socket - doc 18.16 test read write exec
	AT_SIM5215_CIPOPEN = 10140,				// AT+CIPOPEN (Vendor) - Establish connection in multi-client mode - doc 18.17 test read write
	AT_SIM5215_CIPSEND = 10141,				// AT+CIPSEND (Vendor) - Send data in multi-client mode - doc 18.18 test read write
	AT_SIM5215_CIPCLOSE = 10142,			// AT+CIPCLOSE (Vendor) - Close connection in Multi-client mode - doc 18.19 test read write

	// SIM Application Toolkit (SAT) Commands
	AT_SIM5215_STIN = 10143,				// AT+STIN (Vendor) - SAT Indication - doc 19.1 test read
	AT_SIM5215_STGI = 10144,				// AT+STGI (Vendor) - Get SAT information - doc 19.2 test write
	AT_SIM5215_STGR = 10145,				// AT+STGR (Vendor) - SAT respond - doc 19.3 test write
};

extern const struct at_command sim5215_at_com_list[];
extern size_t sim5215_at_com_list_length();

#endif //__SIM5215_H__

/******************************************************************************/
/* end of sim5215.h                                                            */
/******************************************************************************/
