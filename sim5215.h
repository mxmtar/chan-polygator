/******************************************************************************/
/* sim5215.h                                                                  */
/******************************************************************************/

#ifndef __SIM5215_H__
#define __SIM5215_H__

#include <sys/types.h>

#include "at.h"

//------------------------------------------------------------------------------
// at command id
enum {
	AT_SIM5215_UNKNOWN = AT_UNKNOWN,
	// V.25TER
	AT_SIM5215_A_SLASH = AT_A_SLASH,		// A/ - Repeat last command
	AT_SIM5215_A = AT_A,					// ATA - Answer an incoming call
	AT_SIM5215_D = AT_D,					// ATD - Mobile originated call to dial a number
	AT_SIM5215_D_SPECMEM = AT_D_SPECMEM,	// ATD><mem><N> - Originate call from specified memory
	AT_SIM5215_D_CURMEM = AT_D_CURMEM,		// ATD><N> - Originate call to phone number in current memory
	AT_SIM5215_D_PHBOOK = AT_D_PHBOOK,		// ATD><STR> - Originate call to phone number in memory which corresponds to field <STR>
	AT_SIM5215_DL = AT_DL,					// ATDL - Redial last telephone number used
	AT_SIM5215_E = AT_E,					// ATE - Set command echo mode
	AT_SIM5215_H = AT_H,					// ATH - Disconnect existing connection
	AT_SIM5215_I = AT_I,					// ATI - Display product identification information
	AT_SIM5215_L = AT_L,					// ATL - Set monitor speaker loudness
	AT_SIM5215_M = AT_M,					// ATM - Set monitor speaker mode
	AT_SIM5215_3PLUS = AT_3PLUS,			// +++ - Switch from data mode or PPP online mode to command mode
	AT_SIM5215_O = AT_O,					// ATO - Switch from command mode to data mode
	AT_SIM5215_P = AT_P,					// ATP - Select pulse dialling
	AT_SIM5215_Q = AT_Q,					// ATQ - Set result code presentation mode
	AT_SIM5215_S0 = AT_S0,					// ATS0 - Set number of rings before automatically answering the call
	AT_SIM5215_S3 = AT_S3,					// ATS3 - Set command line termination character
	AT_SIM5215_S4 = AT_S4,					// ATS4 - Set response formatting character
	AT_SIM5215_S5 = AT_S5,					// ATS5 - Set command line editing character
	AT_SIM5215_S6 = AT_S6,					// ATS6 - Set pause before blind dialling
	AT_SIM5215_S7 = AT_S7,					// ATS7 - Set number of seconds to wait for connection completion
	AT_SIM5215_S8 = AT_S8,					// ATS8 - Set number of seconds to wait when comma dial modifier used
	AT_SIM5215_S10 = AT_S10,				// ATS10 - Set disconnect delay after indicating the absence of data carrier
	AT_SIM5215_T = AT_T,					// ATT - Select tone dialling
	AT_SIM5215_V = AT_V,					// ATV - Set result code format mode
	AT_SIM5215_X = AT_X,					// ATX - Set connect result code format and monitor call progress
	AT_SIM5215_Z = AT_Z,					// ATZ - Set all current parameters to user defined profile
	AT_SIM5215_andC = AT_andC,				// AT&C - Set DCD function mode
	AT_SIM5215_andD = AT_andD,				// AT&D - Set DTR function mode
	AT_SIM5215_andF = AT_andF,				// AT&F - Set all current parameters to manufacturer defaults
	AT_SIM5215_andV = AT_andV,				// AT&V - Display current configuration
	AT_SIM5215_andW = AT_andW,				// AT&W - Store current parameter to user defined profile
	AT_SIM5215_GCAP = AT_GCAP,				// AT+GCAP - Request complete TA capabilities list
	AT_SIM5215_GMI = AT_GMI,				// AT+GMI - Request manufacturer identification
	AT_SIM5215_GMM = AT_GMM,				// AT+GMM - Request TA model identification
	AT_SIM5215_GMR = AT_GMR,				// AT+GMR - Request TA revision indentification of software release
	AT_SIM5215_GOI = AT_GOI,				// AT+GOI - Request global object identification
	AT_SIM5215_GSN = AT_GSN,				// AT+GSN - Request ta serial number identification (IMEI)
	AT_SIM5215_ICF = AT_ICF,				// AT+ICF - Set TE-TA control character framing
	AT_SIM5215_IFC = AT_IFC,				// AT+IFC - Set TE-TA local data flow control
	AT_SIM5215_ILRR = AT_ILRR,				// AT+ILRR - Set TE-TA local rate reporting mode
	AT_SIM5215_IPR = AT_IPR,				// AT+IPR - Set TE-TA fixed local rate

	// 07.07
	AT_SIM5215_CACM = AT_CACM,				// AT+CACM - Accumulated call meter(ACM) reset or query
	AT_SIM5215_CAMM = AT_CAMM,				// AT+CAMM - Accumulated call meter maximum(ACMMAX) set or query
	AT_SIM5215_CAOC = AT_CAOC,				// AT+CAOC - Advice of charge
	AT_SIM5215_CBST = AT_CBST,				// AT+CBST - Select bearer service type
	AT_SIM5215_CCFC = AT_CCFC,				// AT+CCFC - Call forwarding number and conditions control
	AT_SIM5215_CCWA = AT_CCWA,				// AT+CCWA - Call waiting control
	AT_SIM5215_CEER = AT_CEER,				// AT+CEER - Extended error report
	AT_SIM5215_CGMI = AT_CGMI,				// AT+CGMI - Request manufacturer identification
	AT_SIM5215_CGMM = AT_CGMM,				// AT+CGMM - Request model identification
	AT_SIM5215_CGMR = AT_CGMR,				// AT+CGMR - Request TA revision identification of software release
	AT_SIM5215_CGSN = AT_CGSN,				// AT+CGSN - Request product serial number identification (identical with +GSN)
	AT_SIM5215_CSCS = AT_CSCS,				// AT+CSCS - Select TE character set
	AT_SIM5215_CSTA = AT_CSTA,				// AT+CSTA - Select type of address
	AT_SIM5215_CHLD = AT_CHLD,				// AT+CHLD - Call hold and multiparty
	AT_SIM5215_CIMI = AT_CIMI,				// AT+CIMI - Request international mobile subscriber identity
	AT_SIM5215_CKPD = AT_CKPD,				// AT+CKPD - Keypad control
	AT_SIM5215_CLCC = AT_CLCC,				// AT+CLCC - List current calls of ME
	AT_SIM5215_CLCK = AT_CLCK,				// AT+CLCK - Facility lock
	AT_SIM5215_CLIP = AT_CLIP,				// AT+CLIP - Calling line identification presentation
	AT_SIM5215_CLIR = AT_CLIR,				// AT+CLIR - Calling line identification restriction
	AT_SIM5215_CMEE = AT_CMEE,				// AT+CMEE - Report mobile equipment error
	AT_SIM5215_COLP = AT_COLP,				// AT+COLP - Connected line identification presentation
	AT_SIM5215_COPS = AT_COPS,				// AT+COPS - Operator selection
	AT_SIM5215_CPAS = AT_CPAS,				// AT+CPAS - Mobile equipment activity status
	AT_SIM5215_CPBF = AT_CPBF,				// AT+CPBF - Find phonebook entries
	AT_SIM5215_CPBR = AT_CPBR,				// AT+CPBR - Read current phonebook entries
	AT_SIM5215_CPBS = AT_CPBS,				// AT+CPBS - Select phonebook memory storage
	AT_SIM5215_CPBW = AT_CPBW,				// AT+CPBW - Write phonebook entry
	AT_SIM5215_CPIN = AT_CPIN,				// AT+CPIN - Enter PIN
	AT_SIM5215_CPWD = AT_CPWD,				// AT+CPWD - Change password
	AT_SIM5215_CR = AT_CR,					// AT+CR - Service reporting control
	AT_SIM5215_CRC = AT_CRC,				// AT+CRC - Set cellular result codes for incoming call indication
	AT_SIM5215_CREG = AT_CREG,				// AT+CREG - Network registration
	AT_SIM5215_CRLP = AT_CRLP,				// AT+CRLP - Select radio link protocol parameter
	AT_SIM5215_CRSM = AT_CRSM,				// AT+CRSM - Restricted SIM access
	AT_SIM5215_CSQ = AT_CSQ,				// AT+CSQ - Signal quality report
	AT_SIM5215_FCLASS = AT_FCLASS,			// AT+FCLASS - Fax: select, read or test service class
	AT_SIM5215_FMI = AT_FMI,				// AT+FMI - Fax: report manufactured ID
	AT_SIM5215_FMM = AT_FMM,				// AT+FMM - Fax: report model ID
	AT_SIM5215_FMR = AT_FMR,				// AT+FMR - Fax: report revision ID
	AT_SIM5215_VTD = AT_VTD,				// AT+VTD - Tone duration
	AT_SIM5215_VTS = AT_VTS,				// AT+VTS - DTMF and tone generation
	AT_SIM5215_CMUX = AT_CMUX,				// AT+CMUX - Multiplexer control
	AT_SIM5215_CNUM = AT_CNUM,				// AT+CNUM - Subscriber number
	AT_SIM5215_CPOL = AT_CPOL,				// AT+CPOL - Preferred operator list
	AT_SIM5215_COPN = AT_COPN,				// AT+COPN - Read operator names
	AT_SIM5215_CFUN = AT_CFUN,				// AT+CFUN - Set phone functionality
	AT_SIM5215_CCLK = AT_CCLK,				// AT+CCLK - Clock
	AT_SIM5215_CSIM = AT_CSIM,				// AT+CSIM - Generic SIM access
	AT_SIM5215_CALM = AT_CALM,				// AT+CALM - Alert sound mode
	AT_SIM5215_CALS,						// AT+CALS - Alert sound select
	AT_SIM5215_CRSL = AT_CRSL,				// AT+CRSL - Ringer sound level
	AT_SIM5215_CLVL = AT_CLVL,				// AT+CLVL - Loud speaker volume level
	AT_SIM5215_CMUT = AT_CMUT,				// AT+CMUT - Mute control
	AT_SIM5215_CPUC = AT_CPUC,				// AT+CPUC - Price per unit currency table
	AT_SIM5215_CCWE = AT_CCWE,				// AT+CCWE - Call meter maximum event
	AT_SIM5215_CBC = AT_CBC,				// AT+CBC - Battery charge
	AT_SIM5215_CUSD = AT_CUSD,				// AT+CUSD - Unstructured supplementary service data
	AT_SIM5215_CSSN = AT_CSSN,				// AT+CSSN - Supplementary services notification
	AT_SIM5215_CCUG = AT_CCUG,				// AT+CCUG - Closed user group control

	// 07.05
	AT_SIM5215_CMGD = AT_CMGD,				// AT+CMGD - Delete SMS message
	AT_SIM5215_CMGF = AT_CMGF,				// AT+CMGF - Select SMS message format
	AT_SIM5215_CMGL = AT_CMGL,				// AT+CMGL - List SMS messages from preferred store
	AT_SIM5215_CMGR = AT_CMGR,				// AT+CMGR - Read SMS message
	AT_SIM5215_CMGS = AT_CMGS,				// AT+CMGS - Send SMS message
	AT_SIM5215_CMGW = AT_CMGW,				// AT+CMGW - Write SMS message to memory
	AT_SIM5215_CMSS = AT_CMSS,				// AT+CMSS - Send SMS message from storage
	AT_SIM5215_CNMI = AT_CNMI,				// AT+CNMI - New SMS message indications
	AT_SIM5215_CPMS = AT_CPMS,				// AT+CPMS - Preferred SMS message storage
	AT_SIM5215_CSCA = AT_CSCA,				// AT+CSCA - SMS service center address
	AT_SIM5215_CSCB = AT_CSCB,				// AT+CSCB - Select cell broadcast SMS messages
	AT_SIM5215_CSDH = AT_CSDH,				// AT+CSDH - Show SMS text mode parameters
	AT_SIM5215_CSMP = AT_CSMP,				// AT+CSMP - Set SMS text mode parameters
	AT_SIM5215_CSMS = AT_CSMS,				// AT+CSMS - Select message service
	AT_SIM5215_CNMA,						// AT+CNMA - New message acknowledgement to ME/TA

	AT_SIM5215_SIDET,						// AT+SIDET - Change the side tone gain level
	AT_SIM5215_CPOF,						// AT+CPOF - Control phone to power down
	AT_SIM5215_CADC,						// AT+CADC - Read adc
	AT_SIM5215_CMOD,						// AT+CMOD - Configrue alternating mode calls
	AT_SIM5215_CICCID,						// AT+CICCID - Read ICCID in SIM card

	AT_SIM5215_GCAP,						// AT+GCAP - Request overall capabilities
	AT_SIM5215_CATR,						// AT+CATR - Configure URC destination interface
	AT_SIM5215_CVHU,						// AT+CVHU - Voice hang up control
	AT_SIM5215_CHUP,						// AT+CHUP - Hang up control
	AT_SIM5215_VMUTE,						// AT+VMUTE - Speaker mute control
	AT_SIM5215_AUTOANSWER,					// AT+AUTOANSWER - Automatic answer quickly
	AT_SIM5215_CSDVC,						// AT+CSDVC - Switch voice channel device
	AT_SIM5215_CPTONE,						// AT+CPTONE - Play tone
	AT_SIM5215_CPCM,						// AT+CPCM - External PCM codec mode configuration
	AT_SIM5215_CPCMFNT,						// AT+CPCMFMT - Change the PCM format
	AT_SIM5215_CPCMREG,						//	 AT+CPCMREG - Control PCM data transfer by diagnostics port

	AT_SIM5215_CNMP,						// AT+CNMP - Preferred mode selection
	AT_SIM5215_CNBP,						// AT+CNBP - Preferred band selection
	AT_SIM5215_CNAOP,						// AT+CNAOP - Acquisitions order preference
	AT_SIM5215_CNSDP,						// AT+CNSDP - Preferred service domain selection
	AT_SIM5215_CPSI,						// AT+CPSI - Inquiring UE system information
	AT_SIM5215_CNSMOD,						// AT+CNSMOD - Show network system mode
	AT_SIM5215_CTZU,						// AT+CTZU - Automatic time and time zone update
	AT_SIM5215_CTZR,						// AT+CTZR - Time and time Zone Reporting
	AT_SIM5215_CCINFO,						// AT+CCINFO - Show cell system information
	AT_SIM5215_CSCHN,						// AT+CSCHN - Show cell channel information
	AT_SIM5215_CSPR,						// AT+CSPR - Show serving cell radio parameter
	AT_SIM5215_CRUS,						// AT+CRUS - Show cell set system information
	AT_SIM5215_AUTOCSQ,						// AT+AUTOCSQ - Set CSQ report
	AT_SIM5215_CRFEN,						// AT+CRFEN - RF check at initialization
	AT_SIM5215_CRESET,						// AT+CRESET - Reset ME
	AT_SIM5215_SIMEI,						// AT+SIMEI - Set module IMEI
	AT_SIM5215_CSIMLOCK,					// AT+CSIMLOCK - Request and change password
	AT_SIM5215_DSWITCH,						// AT+DSWITCH - Change diagnostics port mode
	AT_SIM5215_CPASSMGR,					// AT+CPASSMGR - manages password
	AT_SIM5215_CPLMNWLIST,					// AT+CPLMNWLIST - Manages PLMNs allowed by user
	AT_SIM5215_CSIMSEL,						// AT+CSIMSEL - Switch between two SIM card
	AT_SIM5215_SPIC,						// AT+SPIC - Times remain to input SIM PIN/PUK
	AT_SIM5215_CTXGAIN,						// AT+CTXGAIN - Set TX gain
	AT_SIM5215_CRXGAIN,						// AT+CRXGAIN - Set RX gain
	AT_SIM5215_CTXVOL,						// AT+CTXVOL - Set TX volume
	AT_SIM5215_CRXVOL,						// AT+CRXVOL - Set RX volume
	AT_SIM5215_CTXFTR,						// AT+CTXFTR - Set TX filter
	AT_SIM5215_CRXFTR,						// AT+CRXFTR - Set RX filter
	AT_SIM5215_CVALARM,						// AT+CVALARM - Low voltage Alarm
	AT_SIM5215_CRIIC,						// AT+CRIIC - Read values from register of IIC device
	AT_SIM5215_CWIIC,						// AT+CWIIC - Write values to register of IIC device
	AT_SIM5215_CVAUXS,						// AT+CVAUXS - Set state of the pin named VREG_AUX1
	AT_SIM5215_CVAUXV,						// AT+CVAUXV - Set voltage value of the pin named VREG_AUX1
	AT_SIM5215_CGPIO,						// AT+CGPIO - Set Trigger mode of interrupt GPIO
	AT_SIM5215_CGDRT,						// AT+CGDRT - Set the direction of specified GPIO
	AT_SIM5215_CGSETV,						// AT+CGSETV - Set the value of specified GPIO
	AT_SIM5215_CGGETV,						// AT+CGGETV - Get the value of specified GPIO
	AT_SIM5215_CMICAMP1,					// AT+CMICAMP1 - Set value of micamp1
	AT_SIM5215_CVLVL,						// AT+CVLVL - Set value of sound level
	AT_SIM5215_CRIRS,						// AT+CRIRS - Reset RI pin of serial port
	AT_SIM5215_CSUART,						// AT+CSUART - Switch UART line mode
	AT_SIM5215_CDCDMD,						// AT+CDCDMD - Set DCD pin mode
	AT_SIM5215_CDCDVL,						// AT+CDCDVL - Set DCD pin high-low in GPIO mode
	AT_SIM5215_CCGSWT,						// AT+CCGSWT - Switch between camera interface and GPIO
	AT_SIM5215_CPMVT,						// AT+CPMVT - Set threshold voltage
	AT_SIM5215_CUSBSPD,						// AT+CUSBSPD - Switch USB high or full speed
	AT_SIM5215_CCAMMD,						// AT+CCAMMD - Switch the AK8856 mode
	AT_SIM5215_CEMNLIST,					// AT+CEMNLIST - Set the list of emergency number
	AT_SIM5215_FSCD,						// AT+FSCD - Select directory as current directory
	AT_SIM5215_FSMKDIR,						// AT+FSMKDIR - Make new directory in current directory
	AT_SIM5215_FSRMDIR,						// AT+FSRMDIR - Delete directory in current directory
	AT_SIM5215_FSLS,						// AT+FSLS - List directories/files in current directory
	AT_SIM5215_FSDEL,						// AT+FSDEL - Delete file in current directory
	AT_SIM5215_FSRENAME,					// AT+FSRENAME - Rename file in current directory
	AT_SIM5215_FSATTRI,						// AT+FSATTRI - Request file attributes
	AT_SIM5215_FSMEM,						// AT+FSMEM - Check the size of available memory
	AT_SIM5215_FSFMT,						// AT+FSFMT - Format the storage card
	AT_SIM5215_FSLOCA,						// AT+FSLOCA - Select storage place
	AT_SIM5215_CTXFILE,						// AT+CTXFILE - Select file transmitted to PC host
	AT_SIM5215_CRXFILE,						// AT+CRXFILE - Set name of file received from PC host
	AT_SIM5215_IPREX,						// AT+IPREX - Set local baud rate permanently

	AT_SIM5215_VPMAKE,						// AT+VPMAKE - Originate video call
	AT_SIM5215_VPANSWER,					// AT+VPANSWER - Answer video call
	AT_SIM5215_VPEND,						// AT+VPEND - End video call
	AT_SIM5215_VPDTMF,						// AT+VPDTMF - Send DTMF tone during video call
	AT_SIM5215_VPSOURCE,					// AT+VPSOURCE - Select video TX source
	AT_SIM5215_VPRECORD,					// AT+VPRECORD - Record video during video call
	AT_SIM5215_VPLOOP,						// AT+VPLOOP - Loopback far-end video frame during video call
	AT_SIM5215_VPSM,						// AT+VPSM - Switch video call to CSD mode
	AT_SIM5215_VPQLTY,						// AT+VPQLTY - Set video quality

	AT_SIM5215_CMGRO,						// AT+CMGRO - Read message only
	AT_SIM5215_CMGMT,						// AT+CMGMT - Change message status
	AT_SIM5215_CMVP,						// AT+CMVP - Set message valid period
	AT_SIM5215_CMGRD,						// AT+CMGRD - Read and delete message
	AT_SIM5215_CMGSO,						// AT+CMGSO - Send message quickly
	AT_SIM5215_CMGWO,						// AT+CMGWO - Write message to memory quickly

	// Camera Related Commands
	AT_SIM5215_CCAMS,						// AT+CCAMS - Start camera
	AT_SIM5215_CCAME,						// AT+CCAME - Stop camera
	AT_SIM5215_CCAMSETD,					// AT+CCAMSETD - Set camera dimension
	AT_SIM5215_CCAMSETF,					// AT+CCAMSETF - Set camera FPS
	AT_SIM5215_CCAMSETR,					// AT+CCAMSETR - Set camera rotation
	AT_SIM5215_CCAMSETN,					// AT+CCAMSETN - Set camera night shot mode
	AT_SIM5215_CCAMSETWB,					// AT+CCAMSETWB - Set camera white balance
	AT_SIM5215_CCAMSETB,					// AT+CCAMSETB - Set camera brightness
	AT_SIM5215_CCAMSETZ,					// AT+CCAMSETZ - Set camera zoom
	AT_SIM5215_CCAMTP,						// AT+CCAMTP - Take picture
	AT_SIM5215_CCAMEP,						// AT+CCAMEP - Save picture
	AT_SIM5215_CCAMRS,						// AT+CCAMRS - Start video record
	AT_SIM5215_CCAMRP,						// AT+CCAMRP - Pause video record
	AT_SIM5215_CCAMRR,						// AT+CCAMRR - Resume video record
	AT_SIM5215_CCAMRE,						// AT+CCAMRE - Stop video record

	// Audio Application Commands
	AT_SIM5215_CQCPREC,						// AT+CQCPREC - Start recording sound clips
	AT_SIM5215_CQCPPAUSE,					// AT+CQCPPAUSE - Pause sound record
	AT_SIM5215_CQCPRESUME,					// AT+CQCPRESUME - Resume sound record
	AT_SIM5215_CQCPSTOP,					// AT+CQCPSTOP - Stop sound record
	AT_SIM5215_CCMXPLAY,					// AT+CCMXPLAY - Play audio file
	AT_SIM5215_CCMXPAUSE,					// AT+CCMXPAUSE - Pause playing audio file
	AT_SIM5215_CCMXRESUME,					// AT+CCMXRESUME - Resume playing audio file
	AT_SIM5215_CCMXSTOP,					// AT+CCMXSTOP - Stop playing audio file

	// Commands for Packet Domain 3GPP TS 27.007
	AT_SIM5215_CGDCONT,						// AT+CGDCONT Define PDP Context
	AT_SIM5215_CGQREQ,						// AT+CGQREQ - Quality of Service Profile (Requested)
	AT_SIM5215_CGEQREQ,						// AT+CGEQREQ - 3G Quality of Service Profile (Requested)
	AT_SIM5215_CGQMIN,						// AT+CGQMIN - Quality of Service Profile (Minimum acceptable)
	AT_SIM5215_CGEQMIN,						// AT+CGEQMIN - 3G Quality of Service Profile (Minimum acceptable)
	AT_SIM5215_CGATT,						// AT+CGATT Packet Domain attach or detach
	AT_SIM5215_CGACT,						// AT+CGACT - PDP context activate or deactivate
	AT_SIM5215_CGDATA,						// AT+CGDATA - Enter data state
	AT_SIM5215_CGPADDR,						// AT+CGPADDR - Show PDP address
	AT_SIM5215_CGCLASS,						// AT+CGCLASS - GPRS mobile station class
	AT_SIM5215_CGEREP,						// AT+CGEREP - GPRS event reporting
	AT_SIM5215_CGREG,						// AT+CGREG - GPRS network registration status
	AT_SIM5215_CGSMS,						// AT+CGSMS - Select service for MO SMS messages
	AT_SIM5215_CGAUTH,						// AT+CGAUTH - Set type of authentication for PDP-IP connections of GPRS

	// TCP/IP Related Commands
	AT_SIM5215_CGSOCKCONT,					// AT+CGSOCKCONT - Define socket PDP Context
	AT_SIM5215_CSOCKSETPN,					// AT+CSOCKSETPN - Set active PDP context’s profile number
	AT_SIM5215_CSOCKAUTH,					// AT+CSOCKAUTH - Set type of authentication for PDP-IP connections of socket
	AT_SIM5215_IPADDR,						// AT+IPADDR - Inquire socket PDP address
	AT_SIM5215_NETOPEN,						// AT+NETOPEN - Open socket
	AT_SIM5215_TCPCONNECT,					// AT+TCPCONNECT - Establish TCP connection
	AT_SIM5215_TCPWRITE,					// AT+TCPWRITE - Send TCP data
	AT_SIM5215_UDPSEND,						// AT+UDPSEND - Send UDP data
	AT_SIM5215_SERVERSTART,					// AT+SERVERSTART - Startup TCP server
	AT_SIM5215_LISTCLIENT,					// AT+LISTCLIENT - List all of clients’ information
	AT_SIM5215_CLOSECLIENT,					// AT+CLOSECLIENT - Disconnect specified client
	AT_SIM5215_ACTCLIENT,					// AT+ACTCLIENT Activate specified client
	AT_SIM5215_NETCLOSE,					// AT+NETCLOSE - Close socket
	AT_SIM5215_CIPHEAD,						// AT+CIPHEAD - Add an IP head when receiving data
	AT_SIM5215_CIPSRIP,						// AT+CIPSRIP - Set whether display IP address and port of sender when receiving data
	AT_SIM5215_CIPCCFG,						// AT+CIPCCFG - Configure parameters of socket
	AT_SIM5215_CIPOPEN,						// AT+CIPOPEN - Establish connection in multi-client mode
	AT_SIM5215_CIPSEND,						// AT+CIPSEND - Send data in multi-client mode
	AT_SIM5215_CIPCLOSE,					// AT+CIPCLOSE - Close connection in Multi-client mode

	// SIM Application Toolkit (SAT) Commands
	AT_SIM5215_STIN,						// AT+STIN - SAT Indication
	AT_SIM5215_STGI,						// AT+STGI - Get SAT information
	AT_SIM5215_STGR,						// AT+STGR - SAT respond

	AT_SIM5215_MAXNUM,
};

extern const struct at_command sim5215_at_com_list[];

#endif //__SIM5215_H__

/******************************************************************************/
/* end of sim5215.h                                                            */
/******************************************************************************/
