/******************************************************************************/
/* at.c                                                                       */
/******************************************************************************/

#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "at.h"

//------------------------------------------------------------------------------
const struct at_command_operation at_com_oper_list[AT_OPER_COUNT] = {
	{AT_OPER_EXEC, ""},
	{AT_OPER_TEST, "=?"},
	{AT_OPER_READ, "?"},
	{AT_OPER_WRITE, "="},
};
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// get_at_com_by_id()
//------------------------------------------------------------------------------
const struct at_command *get_at_com_by_id(int id, const struct at_command *list, size_t maxnum)
{
	size_t i;

	for (i = 0; i < maxnum; i++) {
		if (list[i].id == id) {
			return &list[i];
		}
	}

	return NULL;
}
//------------------------------------------------------------------------------
// end of get_at_com_by_id()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// is_at_com_done()
//------------------------------------------------------------------------------
int is_at_com_done(const char *response)
{
	if ((response) && (strlen(response)) && (!strcmp(response, "OK") || strstr(response, "ERROR"))) {
		return 1;
	} else {
		return 0;
	}
}
//------------------------------------------------------------------------------
// end of is_at_com_done()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// is_at_com_response()
//------------------------------------------------------------------------------
int is_at_com_response(const struct at_command *at, const char *response)
{
	int i;

	if (!at) {
		return 0;
	}
	if (!response) {
		return 0;
	}
	// ok
	if (strstr(response, "OK")) {
		return 1;
	}
	// error
	if (strstr(response, "ERROR")) {
		return 1;
	}
	// specific
	for (i = 0; i < MAX_AT_CMD_RESP; i++) {
		if (strlen(at->response[i])) {
			if (strstr(response, at->response[i])) {
				return 1;
			}
		}
	}
	if ((at->check_fun) && (at->check_fun(response))) {
		return 1;
	}
	// is not AT command response
	return 0;
}
//------------------------------------------------------------------------------
// end of is_at_com_response()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// get_at_com_oper_by_id()
//------------------------------------------------------------------------------
char *get_at_com_oper_by_id(u_int32_t oper)
{
	int i;

	for (i = 0; i < AT_OPER_COUNT; i++) {
		if (at_com_oper_list[i].id == oper) {
			return (char *)at_com_oper_list[i].str;
		}
	}

	return NULL;
}
//------------------------------------------------------------------------------
// end of get_at_com_oper_by_id()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// reg_status_print()
//------------------------------------------------------------------------------
char *reg_status_print(int stat)
{
	switch (stat) {
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_NOTREG_NOSEARCH:
			return "not registered, ME is not searching operator to register";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_REG_HOME_NET:
			return "registered, home network";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_NOTREG_SEARCH:
			return "not registered, ME is searching operator to register";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_REG_DENIED:
			return "registration denied";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_UNKNOWN:
			return "unknown";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_REG_ROAMING:
			return "registered, roaming";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			return "invalid";
			break;
	}
}
//------------------------------------------------------------------------------
// end of reg_status_print()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// reg_status_print_short()
//------------------------------------------------------------------------------
char *reg_status_print_short(int stat)
{
	switch (stat) {
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_NOTREG_NOSEARCH:
			return "not search";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_REG_HOME_NET:
			return "home net";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_NOTREG_SEARCH:
			return "searching";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_REG_DENIED:
			return "denied";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_UNKNOWN:
			return "unknown";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case REG_STAT_REG_ROAMING:
			return "roaming";
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			return "invalid";
			break;
	}
}
//------------------------------------------------------------------------------
// end of reg_status_print_short()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// rssi_print()
//------------------------------------------------------------------------------
char *rssi_print(char *obuf, int rssi)
{
	if (!obuf) {
		return "obuf error";
	}

	if (rssi == 0) {
		sprintf(obuf, "-113 dBm or less");
		return obuf;
	} else if ((rssi >= 1) && (rssi <= 30)) {
		sprintf(obuf, "%d dBm", rssi*2 - 113);
		return obuf;
	} else if (rssi == 31) {
		sprintf(obuf, "-51 dBm or greater");
		return obuf;
	} else if (rssi == 99) {
		sprintf(obuf, "not known or not detectable");
		return obuf;
	} else {
		sprintf(obuf, "rssi error value");
		return obuf;
	}
}
//------------------------------------------------------------------------------
// end of rssi_print()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// rssi_print_short()
//------------------------------------------------------------------------------
char *rssi_print_short(char *obuf, int rssi)
{
	if (!obuf)
		return "error";

	if (rssi == 0) {
		sprintf(obuf, "-113");
		return obuf;
	} else if ((rssi >= 1) && (rssi <= 30)) {
		sprintf(obuf, "%d", rssi*2 - 113);
		return obuf;
	} else if (rssi == 31) {
		sprintf(obuf, "-51");
		return obuf;
	} else if (rssi == 99) {
		sprintf(obuf, "unknown");
		return obuf;
	} else {
		sprintf(obuf, "error");
		return obuf;
	}
}
//------------------------------------------------------------------------------
// end of rssi_print_short()
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
// ber_print()
//------------------------------------------------------------------------------
char *ber_print(int ber)
{
	switch (ber) {
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 0: return "0.14 %";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 1: return "0.28 %";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 2: return "0.57 %";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 3: return "1.13 %";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 4: return "2.26 %";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 5: return "4.53 %";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 6: return "9.05 %";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 7: return "18.10 %";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 99: return "not known or not detectable";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default: return "ber error value";
	}
}
//------------------------------------------------------------------------------
// end of ber_print()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// ber_print_short()
//------------------------------------------------------------------------------
char *ber_print_short(int ber)
{
	switch (ber)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 0: return "0.14";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 1: return "0.28";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 2: return "0.57";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 3: return "1.13";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 4: return "2.26";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 5: return "4.53";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 6: return "9.05";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 7: return "18.10";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case 99: return "unknown";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default: return "error";
	}
}
//------------------------------------------------------------------------------
// end of ber_print_short()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// cme_error_print()
//------------------------------------------------------------------------------
char *cme_error_print(int ec)
{
	switch (ec) {
		case CME_ERROR_PHONE_FAILURE:									return "phone failure";
		case CME_ERROR_NO_CONNECTION_TO_PHONE:							return "no connection to phone";
		case CME_ERROR_PHONE_ADAPTOR_LINK_RESERVED:						return "phone-adaptor link reserved";
		case CME_ERROR_OPERATION_NOT_ALLOWED:							return "operation not allowed";
		case CME_ERROR_OPERATION_NOT_SUPPORTED:							return "operation not supported";
		case CME_ERROR_PH_SIM_PIN_REQUIRED:								return "PH-SIM PIN required";
		case CME_ERROR_PH_FSIM_PIN_REQUIRED:							return "PH-FSIM PIN required";
		case CME_ERROR_PH_FSIM_PUK_REQUIRED:							return "PH-FSIM PUK required";
		case CME_ERROR_SIM_NOT_INSERTED:								return "SIM not inserted";
		case CME_ERROR_SIM_PIN_REQUIRED:								return "SIM PIN required";
		case CME_ERROR_SIM_PUK_REQUIRED:								return "SIM PUK required";
		case CME_ERROR_SIM_FAILURE:										return "SIM failure";
		case CME_ERROR_SIM_BUSY:										return "SIM busy";
		case CME_ERROR_SIM_WRONG:										return "SIM wrong";
		case CME_ERROR_INCORRECT_PASSWORD:								return "incorrect password";
		case CME_ERROR_SIM_PIN2_REQUIRED:								return "SIM PIN2 required";
		case CME_ERROR_SIM_PUK2_REQUIRED:								return "SIM PUK2 required";
		case CME_ERROR_MEMORY_FULL:										return "memory full";
		case CME_ERROR_INVALID_INDEX:									return "invalid index";
		case CME_ERROR_NOT_FOUND:										return "not found";
		case CME_ERROR_MEMORY_FAILURE:									return "memory failure";
		case CME_ERROR_TEXT_STRING_TOO_LONG:							return "text string too long";
		case CME_ERROR_INVALID_CHARACTERS_IN_TEXT_STRING:				return "invalid characters in text string";
		case CME_ERROR_DIAL_STRING_TOO_LONG:							return "dial string too long";
		case CME_ERROR_INVALID_CHARACTERS_IN_DIAL_STRING:				return "invalid characters in dial string";
		case CME_ERROR_NO_NETWORK_SERVICE:								return "no network service";
		case CME_ERROR_NETWORK_TIMEOUT:									return "network timeout";
		case CME_ERROR_NETWORK_NOT_ALLOWED_EMERGENCY_CALLS_ONLY:		return "network not allowed - emergency calls only";
		case CME_ERROR_NETWORK_PERSONALIZATION_PIN_REQUIRED:			return "network personalization PIN required";
		case CME_ERROR_NETWORK_PERSONALIZATION_PUK_REQUIRED:			return "network personalization PUK required";
		case CME_ERROR_NETWORK_SUBSET_PERSONALIZATION_PIN_REQUIRED:		return "network subset personalization PIN required";
		case CME_ERROR_NETWORK_SUBSET_PERSONALIZATION_PUK_REQUIRED:		return "network subset personalization PUK required";
		case CME_ERROR_SERVICE_PROVIDER_PERSONALIZATION_PIN_REQUIRED:	return "service provider personalization PIN required";
		case CME_ERROR_SERVICE_PROVIDER_PERSONALIZATION_PUK_REQUIRED:	return "service provider personalization PUK required";
		case CME_ERROR_CORPORATE_PERSONALIZATION_PIN_REQUIRED:			return "corporate personalization PIN required";
		case CME_ERROR_CORPORATE_PERSONALIZATION_PUK_REQUIRED:			return "corporate personalization PUK required";
		case CME_ERROR_UNKNOWN:											return "unknown";
		case CME_ERROR_ILLEGAL_MS:										return "illegal MS";
		case CME_ERROR_ILLEGAL_ME:										return "illegal ME";
		case CME_ERROR_GPRS_SERVICES_NOT_ALLOWED:						return "GPRS services not allowed";
		case CME_ERROR_PLMN_NOT_ALLOWED:								return "PLMN not allowed";
		case CME_ERROR_LOCATION_AREA_NOT_ALLOWED:						return "location area not allowed";
		case CME_ERROR_ROAMING_NOT_ALLOWED_IN_THIS_LOCATION_AREA:		return "roaming not allowed in this location area";
		case CME_ERROR_SERVICE_OPTION_NOT_SUPPORTED:					return "service option not supported";
		case CME_ERROR_REQUESTED_SERVICE_OPTION_NOT_SUBSCRIBED:			return "requested service option not subscribed";
		case CME_ERROR_SERVICE_OPTION_TEMPORARILY_OUT_OF_ORDER:			return "service option temporarily out of order";
		case CME_ERROR_UNSPECIFIED_GPRS_ERROR:							return "unspecified GPRS error";
		case CME_ERROR_PDP_AUTHENTICATION_FAILURE:						return "pdp authentication failure";
		case CME_ERROR_INVALID_MOBILE_CLASS:							return "invalid_mobile_class";
		default:														return "unrecognized cme error";
	}
}
//------------------------------------------------------------------------------
// end of cme_error_print()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// end of cms_error_print()
//------------------------------------------------------------------------------
char *cms_error_print(int ec)
{
	switch (ec) {
		case CMS_ERROR_ME_FAILURE:				return "ME failure";
		case CMS_ERROR_SMS_ME_RESERVED:			return "SMS ME reserved";
		case CMS_ERROR_OPER_NOT_ALLOWED:		return "operation not allowed";
		case CMS_ERROR_OPER_NOT_SUPPORTED:		return "operation not supported";
		case CMS_ERROR_INVALID_PDU_MODE:		return "invalid PDU mode";
		case CMS_ERROR_INVALID_TEXT_MODE:		return "invalid text mode";
		case CMS_ERROR_SIM_NOT_INSERTED:		return "SIM not inserted";
		case CMS_ERROR_SIM_PIN_NECESSARY:		return "SIM pin necessary";
		case CMS_ERROR_PH_SIM_PIN_NECESSARY:	return "PH SIM pin necessary";
		case CMS_ERROR_SIM_FAILURE:				return "SIM failure";
		case CMS_ERROR_SIM_BUSY:				return "SIM busy";
		case CMS_ERROR_SIM_WRONG:				return "SIM wrong";
		case CMS_ERROR_SIM_PUK_REQUIRED:		return "SIM PUK required";
		case CMS_ERROR_SIM_PIN2_REQUIRED:		return "SIM PIN2 required";
		case CMS_ERROR_SIM_PUK2_REQUIRED:		return "SIM PUK2 required";
		case CMS_ERROR_MEMORY_FAILURE:			return "memory failure";
		case CMS_ERROR_INVALID_MEMORY_INDEX:	return "invalid memory index";
		case CMS_ERROR_MEMORY_FULL:				return "memory full";
		case CMS_ERROR_SMSC_ADDRESS_UNKNOWN:	return "SMSC address unknown";
		case CMS_ERROR_NO_NETWORK:				return "no network";
		case CMS_ERROR_NETWORK_TIMEOUT:			return "network timeout";
		case CMS_ERROR_UNKNOWN:					return "unknown";
		case CMS_ERROR_SIM_NOT_READY:			return "SIM not ready";
		case CMS_ERROR_UNREAD_SIM_RECORDS:		return "unread records on SIM";
		case CMS_ERROR_CB_ERROR_UNKNOWN:		return "CB error unknown";
		case CMS_ERROR_PS_BUSY:					return "PS busy";
		case CMS_ERROR_SM_BL_NOT_READY:			return "SM BL not ready";
		case CMS_ERROR_INVAL_CHARS_IN_PDU:		return "Invalid (non-hex) chars in PDU";
		case CMS_ERROR_INCORRECT_PDU_LENGTH:	return "Incorrect PDU length";
		case CMS_ERROR_INVALID_MTI:				return "Invalid MTI";
		case CMS_ERROR_INVAL_CHARS_IN_ADDR:		return "Invalid (non-hex) chars in address";
		case CMS_ERROR_INVALID_ADDRESS:			return "Invalid address (no digits read)";
		case CMS_ERROR_INCORRECT_PDU_UDL_L:		return "Incorrect PDU length (UDL)";
		case CMS_ERROR_INCORRECT_SCA_LENGTH:	return "Incorrect SCA length";
		case CMS_ERROR_INVALID_FIRST_OCTET:		return "Invalid First Octet (should be 2 or 34)";
		case CMS_ERROR_INVALID_COMMAND_TYPE:	return "Invalid Command Type";
		case CMS_ERROR_SRR_BIT_NOT_SET:			return "SRR bit not set";
		case CMS_ERROR_SRR_BIT_SET:				return "SRR bit set";
		case CMS_ERROR_INVALID_UDH_IE:			return "Invalid User Data Header IE";
		default:								return "unrecognized cms error";
	}
}
//------------------------------------------------------------------------------
// end of cms_error_print()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_ccwa_write_parse()
//------------------------------------------------------------------------------
int at_gen_ccwa_write_parse(const char *fld, int fld_len, struct at_gen_ccwa_write *ccwa)
{
	char *sp;
	char *tp;
	char *ep;

#define MAX_CCWA_WRITE_PARAM 2
	struct parsing_param params[MAX_CCWA_WRITE_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!ccwa) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;
	// init params
	for(param_cnt=0; param_cnt<MAX_CCWA_WRITE_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}
	// init at_gen_ccwa_write
	ccwa->status = -1;
	ccwa->class = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt < MAX_CCWA_WRITE_PARAM)){
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
	// status (mandatory)
	if(params[0].len > 0){
		tp = params[0].buf;
		while(params[0].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		ccwa->status = atoi(params[0].buf);
		}
	else
		return -1;

	// call class (mandatory)
	if(params[1].len > 0){
		tp = params[1].buf;
		while(params[1].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		ccwa->class = atoi(params[1].buf);
		}
	else
		return -1;

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_gen_ccwa_write_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_cops_read_parse()
//------------------------------------------------------------------------------
int at_gen_cops_read_parse(const char *fld, int fld_len, struct at_gen_cops_read *cops){

	char *sp;
	char *tp;
	char *ep;

#define MAX_COPS_READ_PARAM 3
	struct parsing_param params[MAX_COPS_READ_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!cops) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_COPS_READ_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_gen_cops_read
	cops->mode = -1;
	cops->format = -1;
	cops->oper = NULL;
	cops->oper_len = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt < MAX_COPS_READ_PARAM)){
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

	// mode (mandatory)
	if((param_cnt >= 1) && (params[0].type == PRM_TYPE_INTEGER)){
		// check for digit
		if(params[0].len > 0){
			tp = params[0].buf;
			while(params[0].len--){
				if(!isdigit(*tp++))
					return -1;
				}
			}
		cops->mode = atoi(params[0].buf);
		}
	else
		return -1;

	// format (optional)
	if(param_cnt >= 2){
		if(params[1].type == PRM_TYPE_INTEGER){
			// check for digit
			if(params[1].len > 0){
				tp = params[1].buf;
				while(params[1].len--){
					if(!isdigit(*tp++))
						return -1;
					}
				}
			cops->format = atoi(params[1].buf);
			}
		else
			return -1;
		}
	// oper (optional)
	if(param_cnt >= 3){
		if(params[2].type == PRM_TYPE_STRING){
			//
			cops->oper = params[2].buf;
			cops->oper_len = params[2].len;
			}
		else
			return -1;
		}

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_gen_cops_read_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_cusd_write_parse()
//------------------------------------------------------------------------------
int at_gen_cusd_write_parse(const char *fld, int fld_len, struct at_gen_cusd_write *cusd)
{
	char *sp;
	char *tp;
	char *ep;

#define MAX_CUSD_WRITE_PARAM 3
	struct parsing_param params[MAX_CUSD_WRITE_PARAM];
	int param_cnt;

	// check params
	if (!fld) {
		return -1;
	}
	if ((fld_len <= 0) || (fld_len > 1024)) {
		return -1;
	}
	if (!cusd) {
		return -1;
	}
	// init ptr
	if (!(sp = strchr(fld, ' '))) {
		return -1;
	}
	tp = ++sp;
	ep = (char *)fld + fld_len;
	// init params
	for (param_cnt = 0; param_cnt < MAX_CUSD_WRITE_PARAM; param_cnt++) {
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
	}
	// init at_gen_cusd_write
	cusd->n = -1;
	cusd->str = NULL;
	cusd->str_len = -1;
	cusd->dcs = -1;
	// search params delimiters
	param_cnt = 0;
	while ((tp < ep) && (param_cnt < MAX_CUSD_WRITE_PARAM)) {
		// get param type
		if (*tp == 0x20) {
			tp++;
			continue;
		} else if (*tp == '"') {
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
		if (!(tp = strchr(sp, ','))) {
			tp = ep;
		}
		*tp = '\0';
		// set param len
		if (params[param_cnt].type == PRM_TYPE_STRING) {
			params[param_cnt].len = tp - sp - 1;
			*(tp-1) = '\0';
		} else {
			params[param_cnt].len = tp - sp;
		}
		param_cnt++;
		tp++;
	}
	// n (mandatory)
	if ((param_cnt >= 1) && (params[0].type == PRM_TYPE_INTEGER)) {
		// check for digit
		if (params[0].len > 0) {
			tp = params[0].buf;
			while (params[0].len--) {
				if(!isdigit(*tp++)) {
					return -1;
				}
			}
		}
		cusd->n = atoi(params[0].buf);
	} else {
		return -1;
	}
	// str (optional)
	if (param_cnt >= 2) {
		if (params[1].type == PRM_TYPE_STRING) {
			cusd->str = params[1].buf;
			cusd->str_len = params[1].len;
		} else {
			return -1;
		}
	}
	// dcs (optional)
	if (param_cnt >= 3) {
		if (params[2].type == PRM_TYPE_INTEGER) {
			// check for digit
			if (params[2].len > 0) {
				tp = params[2].buf;
				while (params[2].len--) {
					if (!isdigit(*tp++)) {
						return -1;
					}
				}
			}
			cusd->dcs = atoi(params[2].buf);
		} else {
			return -1;
		}
	}
	return param_cnt;
}
//------------------------------------------------------------------------------
// end of at_gen_cusd_write_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_cmgr_write_parse()
//------------------------------------------------------------------------------
int at_gen_cmgr_write_parse(const char *fld, int fld_len, struct at_gen_cmgr_write *cmgr){

	char *sp;
	char *tp;
	char *ep;

#define MAX_CMGR_WRITE_PARAM 3
	struct parsing_param params[MAX_CMGR_WRITE_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 512)) return -1;

	if(!cmgr) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_CMGR_WRITE_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_gen_cmgr_write
	cmgr->stat = -1;
	cmgr->alpha = NULL;
	cmgr->alpha_len = -1;
	cmgr->length = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt < MAX_CMGR_WRITE_PARAM)){
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

	// stat (mandatory)
	if((param_cnt >= 1) && (params[0].type == PRM_TYPE_INTEGER)){
		// check for digit
		if(params[0].len > 0){
			tp = params[0].buf;
			while(params[0].len--){
				if(!isdigit(*tp++))
					return -1;
				}
			}
		cmgr->stat = atoi(params[0].buf);
		}
	else
		return -1;

	// alpha
	if(param_cnt >= 2){
		cmgr->alpha = params[1].buf;
		cmgr->alpha_len = params[1].len;
		}
	else
		return -1;

	// length (mandatory)
	if(param_cnt >= 3){
		if(params[2].type == PRM_TYPE_INTEGER){
			// check for digit
			if(params[2].len > 0){
				tp = params[2].buf;
				while(params[2].len--){
					if(!isdigit(*tp++))
						return -1;
					}
				}
			cmgr->length = atoi(params[2].buf);
			}
		else
			return -1;
		}
	else
		return -1;

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_gen_cmgr_write_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_csq_exec_parse()
//------------------------------------------------------------------------------
int at_gen_csq_exec_parse(const char *fld, int fld_len, struct at_gen_csq_exec *csq){

	char *sp;
	char *tp;
	char *ep;

#define MAX_CSQ_EXEC_PARAM 2
	struct parsing_param params[MAX_CSQ_EXEC_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!csq) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_CSQ_EXEC_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_gen_csq_exec
	csq->rssi = -1;
	csq->ber = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt < MAX_CSQ_EXEC_PARAM)){
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
	// rssi
	if(params[0].len > 0){
		tp = params[0].buf;
		while(params[0].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		csq->rssi = atoi(params[0].buf);
		}
	else
		return -1;

	// ber
	if(params[1].len > 0){
		tp = params[1].buf;
		while(params[1].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		csq->ber = atoi(params[1].buf);
		}
	else
		return -1;

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_gen_csq_exec_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_csca_read_parse()
//------------------------------------------------------------------------------
int at_gen_csca_read_parse(const char *fld, int fld_len, struct at_gen_csca_read *csca){

	char *sp;
	char *tp;
	char *ep;

#define MAX_CSCA_READ_PARAM 2
	struct parsing_param params[MAX_CSCA_READ_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!csca) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_CSCA_READ_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_gen_csca_read
	csca->sca = NULL;
	csca->sca_len = -1;
	csca->tosca = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt < MAX_CSCA_READ_PARAM)){
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

	// processing params
	// sca
	if((param_cnt >= 1) && (params[0].type == PRM_TYPE_STRING)){
		csca->sca = params[0].buf;
		csca->sca_len = params[0].len;
		}
	else
		return -1;

	// tosca
	if((param_cnt >= 2) && (params[1].len > 0)){
		tp = params[1].buf;
		while(params[1].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		csca->tosca = atoi(params[1].buf);
		}
	else
		return -1;

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_gen_csca_read_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_clip_unsol_parse()
//------------------------------------------------------------------------------
int at_gen_clip_unsol_parse(const char *fld, int fld_len, struct at_gen_clip_unsol *clip){

	char *sp;
	char *tp;
	char *ep;

#define MAX_CLIP_UNSOL_PARAM 6
	struct parsing_param params[MAX_CLIP_UNSOL_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!clip) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_CLIP_UNSOL_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_gen_clip_unsol
	clip->number = NULL;
	clip->number_len = -1;
	clip->type = -1;
	clip->alphaid = NULL;
	clip->alphaid_len = -1;
	clip->cli_validity = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt < MAX_CLIP_UNSOL_PARAM)){
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

	// processing params
	// number
	if((param_cnt >= 1) && (params[0].type == PRM_TYPE_STRING)){
		clip->number = params[0].buf;
		clip->number_len = params[0].len;
		}
	else
		return -1;

	// type
	if((param_cnt >= 2) && (params[1].len > 0)){
		tp = params[1].buf;
		while(params[1].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		clip->type = atoi(params[1].buf);
		}

	// alphaid
	if((param_cnt >= 5) && (params[4].type == PRM_TYPE_STRING)){
		clip->alphaid = params[4].buf;
		clip->alphaid_len = params[4].len;
		}

	// CLI validity
	if((param_cnt >= 6) && (params[5].len > 0)){
		tp = params[5].buf;
		while(params[5].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		clip->cli_validity = atoi(params[5].buf);
		}

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_gen_clip_unsol_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_cnum_exec_parse()
//------------------------------------------------------------------------------
int at_gen_cnum_exec_parse(const char *fld, int fld_len, struct at_gen_cnum_exec *cnum)
{
	char *sp;
	char *tp;
	char *ep;

#define MAX_CNUM_EXEC_PARAM 6
	struct parsing_param params[MAX_CNUM_EXEC_PARAM];
	int param_cnt;

	// check params
	if (!fld) return -1;

	if ((fld_len <= 0) || (fld_len > 256)) return -1;

	if (!cnum) return -1;

	// init params
	for (param_cnt=0; param_cnt<MAX_CNUM_EXEC_PARAM; param_cnt++) {
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
	}

	// init at_gen_cnum_exec
	cnum->alpha = NULL;
	cnum->alpha_len = -1;
	cnum->number = NULL;
	cnum->number_len = -1;
	cnum->type = -1;
	cnum->speed = -1;
	cnum->service = -1;
	cnum->itc = -1;


	// init ptr
	if (!(sp = strchr(fld, ' '))) {
		return -1;
	}
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// search params delimiters
	param_cnt = 0;
	while ((param_cnt < MAX_CNUM_EXEC_PARAM)) {
		// get param type
		if (*tp == '"') {
			params[param_cnt].type = PRM_TYPE_STRING;
			params[param_cnt].buf = ++tp;
		} else if (isdigit(*tp)) {
			params[param_cnt].type = PRM_TYPE_INTEGER;
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
		param_cnt++;
		if (tp == ep) break;
		tp++;
	}

	if ((param_cnt >= 2) &&
			((params[0].type == PRM_TYPE_STRING) || (params[0].type == PRM_TYPE_UNKNOWN)) &&
				(params[1].type == PRM_TYPE_INTEGER)) {

		// check if alpha (optional) not present
		// get number
		cnum->number = params[0].buf;
		cnum->number_len = params[0].len;
		// get type
		if (params[1].len > 0) {
			tp = params[1].buf;
			while (params[1].len--) {
				if (!isdigit(*tp++)) {
					return -1;
				}
			}
			cnum->type = atoi(params[1].buf);
		}
		// check for speed service pair
		if (param_cnt == 3) {
			return -1;
		}
		// check for speed service
		if ((param_cnt >= 4) &&
			(params[2].type == PRM_TYPE_INTEGER) &&
				(params[3].type == PRM_TYPE_INTEGER)) {
			// get speed
			if (params[2].len > 0) {
				tp = params[2].buf;
				while (params[2].len--) {
					if (!isdigit(*tp++)) {
						return -1;
					}
				}
				cnum->speed = atoi(params[2].buf);
			}
			// get service
			if (params[3].len > 0) {
				tp = params[3].buf;
				while (params[3].len--) {
					if (!isdigit(*tp++)) {
						return -1;
					}
				}
				cnum->service = atoi(params[3].buf);
			}
			// check for itc
			if ((param_cnt == 5) &&
				(params[4].type == PRM_TYPE_INTEGER)) {
				// get itc
				if (params[4].len > 0) {
					tp = params[4].buf;
					while (params[4].len--) {
						if (!isdigit(*tp++)) {
							return -1;
						}
					}
					cnum->itc = atoi(params[4].buf);
				}
			}
		}
	} else if ((param_cnt >= 3) &&
				((params[0].type == PRM_TYPE_STRING) || (params[0].type == PRM_TYPE_UNKNOWN)) &&
					(params[1].type == PRM_TYPE_STRING) &&
						(params[2].type == PRM_TYPE_INTEGER)) {

		// check if alpha (optional) present
		// get alpha
		cnum->alpha = params[0].buf;
		cnum->alpha_len = params[0].len;
		// get number
		cnum->number = params[1].buf;
		cnum->number_len = params[1].len;
		// get type
		if (params[2].len > 0) {
			tp = params[2].buf;
			while (params[2].len--) {
				if (!isdigit(*tp++)) {
					return -1;
				}
			}
			cnum->type = atoi(params[2].buf);
		}
		// check for speed service pair
		if (param_cnt == 4) {
			return -1;
		}
		// check for speed service
		if ((param_cnt >= 5) &&
			(params[3].type == PRM_TYPE_INTEGER) &&
				(params[4].type == PRM_TYPE_INTEGER)) {
			// get speed
			if (params[3].len > 0) {
				tp = params[3].buf;
				while (params[3].len--) {
					if (!isdigit(*tp++)) {
						return -1;
					}
				}
				cnum->speed = atoi(params[3].buf);
			}
			// get service
			if (params[4].len > 0) {
				tp = params[4].buf;
				while (params[4].len--) {
					if (!isdigit(*tp++)) {
						return -1;
					}
				}
				cnum->service = atoi(params[4].buf);
			}
			// check for itc
			if ((param_cnt == 6) &&
				(params[5].type == PRM_TYPE_INTEGER)) {
				// get itc
				if (params[5].len > 0){
					tp = params[5].buf;
					while (params[5].len--) {
						if (!isdigit(*tp++)) {
							return -1;
						}
					}
					cnum->itc = atoi(params[5].buf);
				}
			}
		}
	} else {
		return -1;
	}

	return param_cnt;
}
//------------------------------------------------------------------------------
// end of at_gen_cnum_exec_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_clcc_exec_parse()
//------------------------------------------------------------------------------
int at_gen_clcc_exec_parse(const char *fld, int fld_len, struct at_gen_clcc_exec *clcc){

	char *sp;
	char *tp;
	char *ep;

#define MAX_CLCC_EXEC_PARAM 7
	struct parsing_param params[MAX_CLCC_EXEC_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!clcc) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_CLCC_EXEC_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init sim300_clcc_exec
	clcc->id = -1;
	clcc->dir = -1;
	clcc->stat = -1;
	clcc->mode = -1;
	clcc->mpty = -1;
	clcc->number = NULL;
	clcc->number_len = -1;
	clcc->type = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt <MAX_CLCC_EXEC_PARAM)){
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
	// id
	if((param_cnt >= 1) && (params[0].len > 0)){
		tp = params[0].buf;
		while(params[0].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		clcc->id = atoi(params[0].buf);
		}
	else
		return -1;

	// dir
	if((param_cnt >= 2) && (params[1].len > 0)){
		tp = params[1].buf;
		while(params[1].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		clcc->dir = atoi(params[1].buf);
		}
	else
		return -1;

	// stat
	if((param_cnt >= 3) && (params[2].len > 0)){
		tp = params[2].buf;
		while(params[2].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		clcc->stat = atoi(params[2].buf);
		}
	else
		return -1;

	// mode
	if((param_cnt >= 4) && (params[3].len > 0)){
		tp = params[3].buf;
		while(params[3].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		clcc->mode = atoi(params[3].buf);
		}
	else
		return -1;

	// mpty
	if((param_cnt >= 5) && (params[4].len > 0)){
		tp = params[4].buf;
		while(params[4].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		clcc->mpty = atoi(params[4].buf);
		}
	else
		return -1;

	// number
	if(param_cnt >= 6){
	  if(params[5].type == PRM_TYPE_STRING){
			clcc->number = params[5].buf;
			clcc->number_len = params[5].len;
			}
		else
			return -1;
		}

	// type
	if((param_cnt >= 7) && (params[6].len > 0)){
		tp = params[6].buf;
		while(params[6].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		clcc->type = atoi(params[6].buf);
		}

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_gen_clcc_exec_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_creg_read_parse()
//------------------------------------------------------------------------------
int at_gen_creg_read_parse(const char *fld, int fld_len, struct at_gen_creg_read *creg){

	char *sp;
	char *tp;
	char *ep;

#define MAX_CREG_READ_PARAM 4
	struct parsing_param params[MAX_CREG_READ_PARAM];
	int param_cnt;

	// check params
	if(!fld) return -1;

	if((fld_len <= 0) || (fld_len > 256)) return -1;

	if(!creg) return -1;

	// init ptr
	if(!(sp = strchr(fld, ' ')))
		return -1;
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for(param_cnt=0; param_cnt<MAX_CREG_READ_PARAM; param_cnt++){
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
		}

	// init at_gen_creg_read
	creg->n = -1;
	creg->stat = -1;
	creg->lac = NULL;
	creg->lac_len = -1;
	creg->ci = NULL;
	creg->ci_len = -1;

	// search params delimiters
	param_cnt = 0;
	while((tp < ep) && (param_cnt <MAX_CREG_READ_PARAM)){
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

	// processing params
	// n (mandatory)
	if((param_cnt >= 1) && (params[0].len > 0)){
		tp = params[0].buf;
		while(params[0].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		creg->n = atoi(params[0].buf);
		}
	else
		return -1;

	// stat (mandatory)
	if((param_cnt >= 2) && (params[1].len > 0)){
		tp = params[1].buf;
		while(params[1].len--){
			if(!isdigit(*tp++))
				return -1;
			}
		creg->stat = atoi(params[1].buf);
		}
	else
		return -1;

	// lac
	if(param_cnt >= 3){
	  if(params[2].type == PRM_TYPE_STRING){
			creg->lac = params[2].buf;
			creg->lac_len = params[2].len;
			}
		else
			return -1;
		}

	// ci
	if(param_cnt >= 4){
	  if(params[3].type == PRM_TYPE_STRING){
			creg->ci = params[3].buf;
			creg->ci_len = params[3].len;
			}
		else
			return -1;
		}

	return param_cnt;
	}
//------------------------------------------------------------------------------
// end of at_gen_creg_read_parse()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// at_gen_clir_read_parse()
//------------------------------------------------------------------------------
int at_gen_clir_read_parse(const char *fld, int fld_len, struct at_gen_clir_read *clir)
{
	char *sp;
	char *tp;
	char *ep;

#define MAX_CLIR_READ_PARAM 2
	struct parsing_param params[MAX_CLIR_READ_PARAM];
	int param_cnt;

	// check params
	if (!fld) {
		return -1;
	}
	if ((fld_len <= 0) || (fld_len > 256)) {
		return -1;
	}
	if (!clir) {
		return -1;
	}
	// init ptr
	if (!(sp = strchr(fld, ' '))) {
		return -1;
	}
	tp = ++sp;
	ep = (char *)fld + fld_len;

	// init params
	for (param_cnt = 0; param_cnt < MAX_CLIR_READ_PARAM; param_cnt++) {
		params[param_cnt].type = PRM_TYPE_UNKNOWN;
		params[param_cnt].buf = NULL;
		params[param_cnt].len = -1;
	}
	// init at_gen_clir_read
	clir->n = -1;
	clir->m = -1;
	// search params delimiters
	param_cnt = 0;
	while ((tp < ep) && (param_cnt < MAX_CLIR_READ_PARAM)) {
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
		if (!(tp = strchr(sp, ','))) {
			tp = ep;
		}
		*tp = '\0';
		// set param len
		if (params[param_cnt].type == PRM_TYPE_STRING) {
			params[param_cnt].len = tp - sp - 1;
			*(tp-1) = '\0';
		} else {
			params[param_cnt].len = tp - sp;
		}
		param_cnt++;
		tp++;
	}
	// processing integer params
	// n
	if (params[0].len > 0) {
		tp = params[0].buf;
		while (params[0].len--) {
			if(!isdigit(*tp++)) {
				return -1;
			}
		}
		clir->n = atoi(params[0].buf);
	} else {
		return -1;
	}
	// m
	if (params[1].len > 0) {
		tp = params[1].buf;
		while (params[1].len--) {
			if (!isdigit(*tp++)) {
				return -1;
			}
		}
		clir->m = atoi(params[1].buf);
	} else {
		return -1;
	}

	return param_cnt;
}
//------------------------------------------------------------------------------
// end of at_gen_clir_read_parse()
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of at.c                                                                */
/******************************************************************************/
