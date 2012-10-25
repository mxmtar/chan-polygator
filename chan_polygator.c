/******************************************************************************/
/* chan_polygator.c                                                           */
/******************************************************************************/

#include "autoconfig.h"

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include <asterisk.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/un.h>

#include <unistd.h>
#include <termios.h>

#include <dirent.h>
#include <fcntl.h>

#include <sqlite3.h>

#define AST_MODULE "Polygator"

#include "asterisk/causes.h"
#include "asterisk/channel.h"
#include "asterisk/cli.h"
#include "asterisk/frame.h"
#include "asterisk/md5.h"
#include "asterisk/module.h"
#include "asterisk/musiconhold.h"
#include "asterisk/paths.h"
#include "asterisk/pbx.h"
#include "asterisk/version.h"

#include "polygator/polygator-base.h"

#include "libvinetic.h"

#include "address.h"
#include "imei.h"
#include "rtp.h"
#include "sms.h"
#include "strutil.h"
#include "x_timer.h"

#include "at.h"
#include "m10.h"
#include "sim300.h"
#include "sim900.h"

#if SQLITE_VERSION_NUMBER < 3003009
	#define sqlite3_prepare_fun(a,b,c,d,e) sqlite3_prepare(a,b,c,d,e)
#else
	#define sqlite3_prepare_fun(a,b,c,d,e) sqlite3_prepare_v2(a,b,c,d,e)
#endif

#define PG_REG_TRY_COUNT_DEFAULT 5
#define PG_MAX_DTMF_LEN 256
#define PG_VOICE_BUF_LEN 256

enum {
	PG_CALLWAIT_STATE_UNKNOWN = -1,
	PG_CALLWAIT_STATE_DISABLE = 0,
	PG_CALLWAIT_STATE_ENABLE = 1,
	PG_CALLWAIT_STATE_QUERY = 2,
};

enum {
	PG_AT_SUBCMD_CCWA_GET = 1,
	PG_AT_SUBCMD_CCWA_SET = 2,
};

enum {
	PG_AT_SUBCMD_CUSD_GET_BALANCE = 1,
	PG_AT_SUBCMD_CUSD_USER,
};

enum {
	PG_AT_SUBCMD_CMGR_USER = 1,
};

enum {
	PG_CLIR_STATE_UNKNOWN = -1,
	PG_CLIR_STATE_SUBSCRIPTION = 0,
	PG_CLIR_STATE_INVOCATION = 1,
	PG_CLIR_STATE_SUPPRESSION = 2,
	PG_CLIR_STATE_QUERY = 3,
};

enum {
	PG_CLIR_STATUS_NOT_PROVISIONED = 0,
	PG_CLIR_STATUS_PERM_MODE_PROVISIONED = 1,
	PG_CLIR_STATUS_UNKNOWN = 2,
	PG_CLIR_STATUS_TEMP_MODE_RESTRICTED = 3,
	PG_CLIR_STATUS_TEMP_MODE_ALLOWED = 4,
};


#define PG_DEFAULT_AT_RESPONSE_TIMEOUT 2
struct pg_at_cmd {
	struct at_command *at;
	int id;
	u_int32_t oper;
	u_int32_t sub_cmd;
	char cmd_buf[256];
	int cmd_len;
	int timeout;
	struct x_timer timer;
	size_t attempt;
	int show;
	AST_LIST_ENTRY(pg_at_cmd) entry;
};

enum {
	PG_VINETIC_STATE_INIT = 1,
	PG_VINETIC_STATE_IDLE,
	PG_VINETIC_STATE_RUN,
};

struct pg_channel_gsm;
struct pg_vinetic;
struct pg_channel_rtp;

struct pg_board {
	// board private lock
	ast_mutex_t lock;

	char *type;
	char *name;
	char *path;

	// board channel list
	AST_LIST_HEAD_NOLOCK(channel_gsm_list, pg_channel_gsm) channel_gsm_list;
	// board vinetic list
	AST_LIST_HEAD_NOLOCK(vinetic_list, pg_vinetic) vinetic_list;

	// entry for general board list
	AST_LIST_ENTRY(pg_board) pg_general_board_list_entry;
};

struct pg_vinetic {
	// vinetic private lock
	ast_mutex_t lock;
	pthread_t thread;

	unsigned int position_on_board;
	struct pg_board *board;

	char *name;
	char *path;

	// vinetic firmware
	char *firmware;
	char *almab;
	char *almcd;
	char *cram;

	int run;
	int state;

	int patch_alm_gsm[4];

#if ASTERISK_VERSION_NUM >= 100000
	struct ast_format_cap *capabilities;
#elif ASTERISK_VERSION_NUM >= 10800
	format_t capabilities;
#else
	int capabilities;
#endif

	struct vinetic_context context;

	// board RTP channel list
	AST_LIST_HEAD_NOLOCK(channel_rtp_list, pg_channel_rtp) channel_rtp_list;

	// entry for board vinetic list
	AST_LIST_ENTRY(pg_vinetic) pg_board_vinetic_list_entry;
};

struct pg_channel_rtp {
	// RTP channel private lock
	ast_mutex_t lock;

	char *name;
	char *path;
	int fd;

	unsigned int position_on_vinetic;
	struct pg_vinetic *vinetic;

	int busy;

	u_int32_t loc_ssrc;
	u_int32_t rem_ssrc;
	u_int32_t recv_ssrc;
	u_int32_t loc_timestamp;
	u_int32_t rem_timestamp;
	u_int32_t recv_timestamp;
	u_int16_t loc_seq_num;
	u_int16_t rem_seq_num;
	u_int16_t recv_seq_num;

	int event_is_now_recv;

#if ASTERISK_VERSION_NUM >= 100000
	struct ast_format format;
#elif ASTERISK_VERSION_NUM >= 10800
	format_t format;
#else
	int format;
#endif
	int payload_type;
	int encoder_algorithm;
	int encoder_packet_time;

	int event_payload_type;

	struct ast_frame frame;
	char voice_recv_buf[PG_VOICE_BUF_LEN + AST_FRIENDLY_OFFSET];

	size_t send_sid_count;
	size_t send_drop_count;
	size_t send_frame_count;
	size_t recv_frame_count;

	char dtmfbuf[PG_MAX_DTMF_LEN];
	char *dtmfptr;

	// entry for board vinetic list
	AST_LIST_ENTRY(pg_channel_rtp) pg_vinetic_channel_rtp_list_entry;
};

enum {
	PG_CALL_GSM_STATE_UNKNOWN = 0,
	PG_CALL_GSM_STATE_NULL,
	PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING,
	PG_CALL_GSM_STATE_CALL_DELIVERED,
	PG_CALL_GSM_STATE_CALL_PRESENT,
	PG_CALL_GSM_STATE_CALL_RECEIVED,
	PG_CALL_GSM_STATE_ACTIVE,
	PG_CALL_GSM_STATE_LOCAL_HOLD,
	PG_CALL_GSM_STATE_REMOTE_HOLD,
	PG_CALL_GSM_STATE_RELEASE_INDICATION,
	PG_CALL_GSM_STATE_OVERLAP_RECEIVING,
};

enum {
	PG_CALL_GSM_MSG_UNKNOWN = 0,
	PG_CALL_GSM_MSG_SETUP_REQ,
	PG_CALL_GSM_MSG_PROCEEDING_IND,
	PG_CALL_GSM_MSG_ALERTING_IND,
	PG_CALL_GSM_MSG_SETUP_CONFIRM,
	PG_CALL_GSM_MSG_RELEASE_REQ,
	PG_CALL_GSM_MSG_RELEASE_IND,
	PG_CALL_GSM_MSG_SETUP_IND,
	PG_CALL_GSM_MSG_INFO_IND,
	PG_CALL_GSM_MSG_SETUP_RESPONSE,
	PG_CALL_GSM_MSG_HOLD_REQ,
	PG_CALL_GSM_MSG_UNHOLD_REQ,
	PG_CALL_GSM_MSG_HOLD_IND,
	PG_CALL_GSM_MSG_UNHOLD_IND,
};

enum {
	PG_CALL_GSM_DIRECTION_UNKNOWN = 0,
	PG_CALL_GSM_DIRECTION_OUTGOING,
	PG_CALL_GSM_DIRECTION_INCOMING,
};

enum {
	PG_CALL_GSM_HANGUP_SIDE_UNKNOWN = 0,
	PG_CALL_GSM_HANGUP_SIDE_CORE,
	PG_CALL_GSM_HANGUP_SIDE_NETWORK,
};

enum {
	PG_CALL_GSM_INCOMING_TYPE_UNKNOWN = -1,
	PG_CALL_GSM_INCOMING_TYPE_DENY = 0,
	PG_CALL_GSM_INCOMING_TYPE_DTMF = 1,
	PG_CALL_GSM_INCOMING_TYPE_SPEC = 2,
	PG_CALL_GSM_INCOMING_TYPE_DYN = 3,
};

enum {
	PG_CALL_GSM_OUTGOING_TYPE_UNKNOWN = -1,
	PG_CALL_GSM_OUTGOING_TYPE_DENY = 0,
	PG_CALL_GSM_OUTGOING_TYPE_ALLOW = 1,
};

struct pg_call_gsm {
	int line;
	u_int32_t hash;
	int clcc_stat;
	int clcc_mpty;
	int state;
	int direction;
	int contest;
	struct address called_name;
	struct address calling_name;
	struct pg_channel_gsm *channel_gsm;
	struct pg_channel_rtp *channel_rtp;
	struct ast_channel *owner;
	struct pg_call_gsm_timers {
		struct x_timer dial;
		struct x_timer proceeding;
	} timers;
	struct timeval start_time;
	struct timeval answer_time;
	int hangup_side;
	int hangup_cause;
	AST_LIST_ENTRY(pg_call_gsm) entry;
};

enum {
	PG_CHANNEL_GSM_STATE_DISABLE = 1,
	PG_CHANNEL_GSM_STATE_WAIT_RDY,
	PG_CHANNEL_GSM_STATE_WAIT_CFUN,
	PG_CHANNEL_GSM_STATE_CHECK_PIN,
	PG_CHANNEL_GSM_STATE_WAIT_CALL_READY,
	PG_CHANNEL_GSM_STATE_INIT,
	PG_CHANNEL_GSM_STATE_RUN,
	PG_CHANNEL_GSM_STATE_SUSPEND,
	PG_CHANNEL_GSM_STATE_WAIT_SUSPEND,
	PG_CHANNEL_GSM_STATE_WAIT_VIO_DOWN,
	PG_CHANNEL_GSM_STATE_TEST_FUN,
	PG_CHANNEL_GSM_STATE_SERVICE,
};

struct pg_trunk_gsm_channel_gsm_fold {
	char *name;
	struct pg_channel_gsm *channel_gsm;
	// entry for trunk list
	AST_LIST_ENTRY(pg_trunk_gsm_channel_gsm_fold) pg_trunk_gsm_channel_gsm_fold_trunk_list_entry;
	AST_LIST_ENTRY(pg_trunk_gsm_channel_gsm_fold) pg_trunk_gsm_channel_gsm_fold_channel_list_entry;
};

struct pg_channel_gsm {
	// GSM channel private lock
	ast_mutex_t lock;
	pthread_t thread;

	char *device;
	char *tty_path;
	int tty_fd;

	char *sim_path;

	unsigned int position_on_board;
	struct pg_board *board;

	unsigned int gsm_module_type;	// type of GSM module ("SIM300", "M10", "SIM900", "SIM5215")
	unsigned int vinetic_number;
	int vinetic_alm_slot;
	int vinetic_pcm_slot;

	char *alias;

	// configuration
	struct pg_channel_gsm_config {
		int enable;
		int sms_notify_enable;
		int baudrate;
		int incoming_type;
		int outgoing_type;
		char gsm_call_context[AST_MAX_CONTEXT];
		char gsm_call_extension[AST_MAX_EXTENSION];
		int trunkonly;
		int conference_allowed;
		time_t dcrttl;
		int progress;
		char language[MAX_LANGUAGE];
		char mohinterpret[MAX_MUSICCLASS];
		char sms_notify_context[AST_MAX_CONTEXT];
		char sms_notify_extension[AST_MAX_EXTENSION];
		struct timeval sms_send_interval;
		int sms_send_attempt;
		int sms_max_part;
		int gainin;
		int gainout;
		unsigned int gain1;
		unsigned int gain2;
		unsigned int gainx;
		unsigned int gainr;
		int ali_nelec;
		int ali_nelec_tm;
		int ali_nelec_oldc;
		int ali_nelec_as;
		int ali_nelec_nlp;
		int ali_nelec_nlpm;
		int clir;
		int callwait;
		int reg_try_count;
		char *balance_request;
	} config;

	struct pg_channel_gsm_init {
		unsigned int ready:1;
		unsigned int clip:1;
		unsigned int chfa:1;
		unsigned int colp:1;
		unsigned int clvl:1;
		unsigned int cmic:1;
		unsigned int creg:1;
		unsigned int cscs:1;
		unsigned int cmee:1;
		unsigned int cclk:1;
		unsigned int cmgf:1;
		unsigned int echo:1;
		unsigned int cnmi:1;
		unsigned int ceer:1;
		unsigned int fallback:1;
		unsigned int cfun:1;
		unsigned int init:1;	// status bit -- set after startup initialization
	} init;

	// runtime flags
	struct pg_channel_gsm_flags {
		unsigned int power:1;
		unsigned int enable:1;
		unsigned int shutdown:1;
		unsigned int shutdown_now:1;
		unsigned int restart:1;
		unsigned int restart_now:1;
		unsigned int suspend_now:1;
		unsigned int resume_now:1;
		unsigned int init:1;
		unsigned int balance_req:1;
		unsigned int sim_change:1;
		unsigned int sim_test:1;
		unsigned int sim_inserted:1;
		unsigned int sim_startup:1;
		unsigned int pin_required:1;
		unsigned int puk_required:1;
		unsigned int pin_accepted:1;
		unsigned int func_test_done:1;
		unsigned int func_test_run:1;
		unsigned int sms_table_needed:1;
		unsigned int dcr_table_needed:1;
		unsigned int main_tty:1;
	} flags;

	// timers
	struct pg_channel_gsm_timers {
		struct x_timer waitrdy;
		struct x_timer waitsuspend;
		struct x_timer testfun;
		struct x_timer testfunsend;
		struct x_timer callready;
		struct x_timer runquartersecond;
		struct x_timer runhalfsecond;
		struct x_timer runonesecond;
		struct x_timer runfivesecond;
		struct x_timer runhalfminute;
		struct x_timer runoneminute;
		struct x_timer waitviodown;
		struct x_timer testviodown;
		struct x_timer smssend;
		struct x_timer simpoll;
		struct x_timer pinwait;
		struct x_timer registering;
	} timers;

	// debug
	struct pg_channel_gsm_debug {
		unsigned int at:1;
		unsigned int receiver:1;
		char *at_debug_path;
		FILE *at_debug_fp;
		char *receiver_debug_path;
		FILE *receiver_debug_fp;
	} debug;

	// Runtime data
	int power_sequence_number;
	int reg_stat; /* Registration status */
	int reg_stat_old; /* Registration status */
	int state;
	int vio;
	int baudrate;
	int baudrate_test;
	AST_LIST_HEAD_NOLOCK(cmd_queue, pg_at_cmd) cmd_queue; /* AT-command queue */
	struct pg_at_cmd *at_cmd; /* AT-command queue */
	int cmd_done; /* AT-command queue */
	int pdu_len;
	int pdu_send_id;
	char pdu_send_buf[512];
	int pdu_send_len;
	int pdu_send_attempt;
	int pdu_cmt_wait;
	int pdu_cds_wait;
	int sms_ref;
	int callwait;
	int at_pipe[2]; /* AT */
	int ussd_pipe[2]; /* USSD */
	int ussd_sub_cmd; /* USSD */
	char *ussd; /* USSD */
	char *balance; /* USSD */
	char *iccid;
	char *iccid_ban;
	char *imsi;
	char *operator_code;
	char *operator_name;
	struct address subscriber_number;
	struct address smsc_number;
	int gainin;
	int gainout;
	int clir;
	int clir_status;
	int rssi;
	int ber;
	char *model;
	char *firmware;
	char imei[16];
	int reg_try_count;
	char *pin;
	char *puk;
	int dtmf_is_started;

	time_t last_call_time_incoming;
	time_t last_call_time_outgoing;
	time_t total_call_time_incoming;
	time_t total_call_time_outgoing;

	int64_t out_total_call_count;
	int64_t out_answered_call_count;
	time_t out_active_call_duration;

	time_t acd;
	int asr;

	AST_LIST_HEAD_NOLOCK(call_list, pg_call_gsm) call_list;
	struct pg_channel_rtp *channel_rtp;
	size_t channel_rtp_usage;

	// trunk list
	AST_LIST_HEAD_NOLOCK(trunk_list, pg_trunk_gsm_channel_gsm_fold) trunk_list;

	// entry for board channel list
	AST_LIST_ENTRY(pg_channel_gsm) pg_board_channel_gsm_list_entry;
	// entry for general channel list
	AST_LIST_ENTRY(pg_channel_gsm) pg_general_channel_gsm_list_entry;
};

struct pg_trunk_gsm {
	char *name;
	struct pg_trunk_gsm_channel_gsm_fold *channel_gsm_last;
	AST_LIST_HEAD_NOLOCK(trunk_channel_gsm_list, pg_trunk_gsm_channel_gsm_fold) channel_gsm_list;
	AST_LIST_ENTRY(pg_trunk_gsm) pg_general_trunk_gsm_list_entry;
};

//------------------------------------------------------------------------------
// mmax()
#define mmax(arg0, arg1) ((arg0 > arg1) ? arg0 : arg1)
// end of mmax()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// mmin()
#define mmin(arg0, arg1) ((arg0 < arg1) ? arg0 : arg1)
// end of mmin()
//------------------------------------------------------------------------------

#if ASTERISK_VERSION_NUM < 10800
	typedef cli_fn cli_fn_type;
#else
	typedef char*(*cli_fn_type)(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
#endif

#if ASTERISK_VERSION_NUM >= 100000
static struct ast_channel *pg_gsm_requester(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause);
#elif ASTERISK_VERSION_NUM >= 10800
static struct ast_channel *pg_gsm_requester(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause);
#else
static struct ast_channel *pg_gsm_requester(const char *type, int format, void *data, int *cause);
#endif
static int pg_gsm_call(struct ast_channel *ast_ch, char *dest, int timeout);
static int pg_gsm_hangup(struct ast_channel *ast_ch);
static int pg_gsm_answer(struct ast_channel *ast);
static int pg_gsm_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int pg_gsm_write(struct ast_channel *ast_ch, struct ast_frame *frame);
static struct ast_frame * pg_gsm_read(struct ast_channel *ast_ch);
static int pg_gsm_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int pg_gsm_dtmf_start(struct ast_channel *ast_ch, char digit);
static int pg_gsm_dtmf_end(struct ast_channel *ast_ch, char digit, unsigned int duration);

static struct timeval pg_start_time = {0, 0};

#if ASTERISK_VERSION_NUM >= 100000
static struct ast_channel_tech pg_gsm_tech = {
#else
static struct ast_channel_tech pg_gsm_tech = {
#endif
	.type = "PGGSM",
	.description = "Polygator GSM",
	.requester = pg_gsm_requester,
	.call = pg_gsm_call,
	.hangup = pg_gsm_hangup,
	.answer = pg_gsm_answer,
	.indicate = pg_gsm_indicate,
	.write = pg_gsm_write,
	.read = pg_gsm_read,
	.fixup = pg_gsm_fixup,
	.send_digit_begin = pg_gsm_dtmf_start,
	.send_digit_end = pg_gsm_dtmf_end,
};

static void pg_cleanup(void);
static void pg_atexit(void);

static int pg_atexit_registered = 0;
static int pg_cli_registered = 0;
static int pg_gsm_tech_neededed = 0;
static int pg_gsm_tech_registered = 0;

struct pg_generic_param {
	int id;
	char name[AST_MAX_CMD_LEN];
};
#define PG_GENERIC_PARAM(_prm, _prmid) \
	{.id = _prmid, .name = _prm}

#define PG_GENERIC_PARAMS_COUNT(_prms) \
	sizeof(_prms) / sizeof(_prms[0])

struct pg_cli_action {
	char name[AST_MAX_CMD_LEN];
	cli_fn_type handler;
};
#define PG_CLI_ACTION(_act, _fn) \
	{.name = _act, .handler = _fn}

#define PG_CLI_ACTION_HANDLERS_COUNT(_handlers) \
	sizeof(_handlers) / sizeof(_handlers[0])

static char *pg_cli_show_modinfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_boards(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_trunks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_calls(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_gsm_call_stat_out(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_netinfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_devinfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_board(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_vinetic(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_channel_gsm(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_config_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_trunk_gsm_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char pg_cli_channel_gsm_actions_usage[256];
static char pg_cli_config_actions_usage[256];
static char pg_cli_trunk_gsm_actions_usage[256];
static struct ast_cli_entry pg_cli[] = {
	AST_CLI_DEFINE(pg_cli_show_modinfo, "Show PG module information"),
	AST_CLI_DEFINE(pg_cli_show_boards, "Show PG boards information summary"),
	AST_CLI_DEFINE(pg_cli_show_channels, "Show PG channels information summary"),
	AST_CLI_DEFINE(pg_cli_show_trunks, "Show PG trunks information summary"),
	AST_CLI_DEFINE(pg_cli_show_calls, "Show PG calls information summary"),
	AST_CLI_DEFINE(pg_cli_show_gsm_call_stat_out, "Show PG outgoing GSM calls statistic"),
	AST_CLI_DEFINE(pg_cli_show_netinfo, "Show PG GSM network information summary"),
	AST_CLI_DEFINE(pg_cli_show_devinfo, "Show PG device information summary"),
	AST_CLI_DEFINE(pg_cli_show_board, "Show PG board information"),
	AST_CLI_DEFINE(pg_cli_show_vinetic, "Show VINETIC information"),
	AST_CLI_DEFINE(pg_cli_show_channel_gsm, "Show PG GSM channel information"),
	AST_CLI_DEFINE(pg_cli_config_actions, "Save Polygator configuration"),
	AST_CLI_DEFINE(pg_cli_channel_gsm_actions, "Perform actions on GSM channels"),
	AST_CLI_DEFINE(pg_cli_trunk_gsm_actions, "Perform actions on GSM trunks"),
};

// polygator CLI GSM channel actions
static char *pg_cli_channel_gsm_action_power(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_action_enable_disable(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_action_suspend_resume(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_action_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_action_param(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_action_ussd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_action_at(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_action_dcr(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static struct pg_cli_action pg_cli_channel_gsm_action_handlers[] = {
	PG_CLI_ACTION("power", pg_cli_channel_gsm_action_power),
	PG_CLI_ACTION("enable", pg_cli_channel_gsm_action_enable_disable),
	PG_CLI_ACTION("disable", pg_cli_channel_gsm_action_enable_disable),
	PG_CLI_ACTION("restart", pg_cli_channel_gsm_action_enable_disable),
	PG_CLI_ACTION("suspend", pg_cli_channel_gsm_action_suspend_resume),
	PG_CLI_ACTION("resume", pg_cli_channel_gsm_action_suspend_resume),
	PG_CLI_ACTION("get", pg_cli_channel_gsm_action_param),
	PG_CLI_ACTION("set", pg_cli_channel_gsm_action_param),
	PG_CLI_ACTION("query", pg_cli_channel_gsm_action_param),
	PG_CLI_ACTION("delete", pg_cli_channel_gsm_action_param),
	PG_CLI_ACTION("debug", pg_cli_channel_gsm_action_debug),
	PG_CLI_ACTION("ussd", pg_cli_channel_gsm_action_ussd),
	PG_CLI_ACTION("at", pg_cli_channel_gsm_action_at),
	PG_CLI_ACTION("dcr", pg_cli_channel_gsm_action_dcr),
};

enum {
	PG_CHANNEL_GSM_PARAM_OP_UNKNOWN = 0,
	PG_CHANNEL_GSM_PARAM_OP_SET		= (1 << 0),
	PG_CHANNEL_GSM_PARAM_OP_GET		= (1 << 1),
	PG_CHANNEL_GSM_PARAM_OP_QUERY	= (1 << 2),
	PG_CHANNEL_GSM_PARAM_OP_DELETE	= (1 << 3),
};

enum {
	PG_CHANNEL_GSM_PARAM_UNKNOWN = 0,
	PG_CHANNEL_GSM_PARAM_ALIAS,
	PG_CHANNEL_GSM_PARAM_PIN,
	PG_CHANNEL_GSM_PARAM_PUK,
	PG_CHANNEL_GSM_PARAM_IMEI,
	PG_CHANNEL_GSM_PARAM_NUMBER,
	PG_CHANNEL_GSM_PARAM_PROGRESS,
	PG_CHANNEL_GSM_PARAM_INCOMING,
	PG_CHANNEL_GSM_PARAM_INCOMINGTO,
	PG_CHANNEL_GSM_PARAM_OUTGOING,
	PG_CHANNEL_GSM_PARAM_CONTEXT,
	PG_CHANNEL_GSM_PARAM_REGATTEMPT,
	PG_CHANNEL_GSM_PARAM_CALLWAIT,
	PG_CHANNEL_GSM_PARAM_CLIR,
	PG_CHANNEL_GSM_PARAM_DCRTTL,
	PG_CHANNEL_GSM_PARAM_LANGUAGE,
	PG_CHANNEL_GSM_PARAM_MOHINTERPRET,
	PG_CHANNEL_GSM_PARAM_BAUDRATE,
	PG_CHANNEL_GSM_PARAM_GAININ,
	PG_CHANNEL_GSM_PARAM_GAINOUT,
	PG_CHANNEL_GSM_PARAM_GAIN1,
	PG_CHANNEL_GSM_PARAM_GAIN2,
	PG_CHANNEL_GSM_PARAM_GAINX,
	PG_CHANNEL_GSM_PARAM_GAINR,
	PG_CHANNEL_GSM_PARAM_MODULETYPE,
	PG_CHANNEL_GSM_PARAM_SMSSENDINTERVAL,
	PG_CHANNEL_GSM_PARAM_SMSSENDATTEMPT,
	PG_CHANNEL_GSM_PARAM_SMSMAXPARTCOUNT,
	PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_ENABLE,
	PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_CONTEXT,
	PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_EXTENSION,
	PG_CHANNEL_GSM_PARAM_TRUNK,
	PG_CHANNEL_GSM_PARAM_TRUNKONLY,
	PG_CHANNEL_GSM_PARAM_CONFALLOW,
	PG_CHANNEL_GSM_PARAM_NELEC,
	PG_CHANNEL_GSM_PARAM_NELEC_TM,
	PG_CHANNEL_GSM_PARAM_NELEC_OLDC,
	PG_CHANNEL_GSM_PARAM_NELEC_AS,
	PG_CHANNEL_GSM_PARAM_NELEC_NLP,
	PG_CHANNEL_GSM_PARAM_NELEC_NLPM,
};
struct pg_cgannel_gsm_param {
	int id;
	char name[AST_MAX_CMD_LEN];
	unsigned int ops;
};
#define PG_CHANNEL_GSM_PARAM(_prm, _prmid, _ops) \
	{.id = _prmid, .name = _prm, .ops = _ops}

#define PG_CHANNEL_GSM_PARAMS_COUNT(_prms) \
	sizeof(_prms) / sizeof(_prms[0])

static struct pg_cgannel_gsm_param pg_channel_gsm_params[] = {
	PG_CHANNEL_GSM_PARAM("alias", PG_CHANNEL_GSM_PARAM_ALIAS, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("pin", PG_CHANNEL_GSM_PARAM_PIN, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("puk", PG_CHANNEL_GSM_PARAM_PUK, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("imei", PG_CHANNEL_GSM_PARAM_IMEI, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("number", PG_CHANNEL_GSM_PARAM_NUMBER, PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("progress", PG_CHANNEL_GSM_PARAM_PROGRESS, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("incoming", PG_CHANNEL_GSM_PARAM_INCOMING, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("incomingto", PG_CHANNEL_GSM_PARAM_INCOMINGTO, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("outgoing", PG_CHANNEL_GSM_PARAM_OUTGOING, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("context", PG_CHANNEL_GSM_PARAM_CONTEXT, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("regattempt", PG_CHANNEL_GSM_PARAM_REGATTEMPT, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("callwait", PG_CHANNEL_GSM_PARAM_CALLWAIT, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("clir", PG_CHANNEL_GSM_PARAM_CLIR, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("dcrttl", PG_CHANNEL_GSM_PARAM_DCRTTL, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("language", PG_CHANNEL_GSM_PARAM_LANGUAGE, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("mohinterpret", PG_CHANNEL_GSM_PARAM_MOHINTERPRET, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("baudrate", PG_CHANNEL_GSM_PARAM_BAUDRATE, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("gainin", PG_CHANNEL_GSM_PARAM_GAININ, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("gainout", PG_CHANNEL_GSM_PARAM_GAINOUT, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("gain1", PG_CHANNEL_GSM_PARAM_GAIN1, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("gain2", PG_CHANNEL_GSM_PARAM_GAIN2, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("gainx", PG_CHANNEL_GSM_PARAM_GAINX, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("gainr", PG_CHANNEL_GSM_PARAM_GAINR, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("module", PG_CHANNEL_GSM_PARAM_MODULETYPE, PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_QUERY),
	PG_CHANNEL_GSM_PARAM("sms.snd.intrv", PG_CHANNEL_GSM_PARAM_SMSSENDINTERVAL, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("sms.snd.atmpt", PG_CHANNEL_GSM_PARAM_SMSSENDATTEMPT, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("sms.max.part", PG_CHANNEL_GSM_PARAM_SMSMAXPARTCOUNT, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("sms.ntf.en", PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_ENABLE, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("sms.ntf.ctx", PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_CONTEXT, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("sms.ntf.exten", PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_EXTENSION, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("trunk", PG_CHANNEL_GSM_PARAM_TRUNK, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET|PG_CHANNEL_GSM_PARAM_OP_DELETE),
	PG_CHANNEL_GSM_PARAM("trunkonly", PG_CHANNEL_GSM_PARAM_TRUNKONLY, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("confallow", PG_CHANNEL_GSM_PARAM_CONFALLOW, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("ali.nelec", PG_CHANNEL_GSM_PARAM_NELEC, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("ali.nelec.tm", PG_CHANNEL_GSM_PARAM_NELEC_TM, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("ali.nelec.oldc", PG_CHANNEL_GSM_PARAM_NELEC_OLDC, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("ali.nelec.as", PG_CHANNEL_GSM_PARAM_NELEC_AS, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("ali.nelec.nlp", PG_CHANNEL_GSM_PARAM_NELEC_NLP, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
	PG_CHANNEL_GSM_PARAM("ali.nelec.nlpm", PG_CHANNEL_GSM_PARAM_NELEC_NLPM, PG_CHANNEL_GSM_PARAM_OP_SET|PG_CHANNEL_GSM_PARAM_OP_GET),
};
enum {
	PG_CHANNEL_GSM_DEBUG_UNKNOWN = 0,
	PG_CHANNEL_GSM_DEBUG_AT,
	PG_CHANNEL_GSM_DEBUG_RECEIVER,
};
// polygator channel GSM debug
static struct pg_generic_param pg_channel_gsm_debugs[] = {
	PG_GENERIC_PARAM("at", PG_CHANNEL_GSM_DEBUG_AT),
	PG_GENERIC_PARAM("receiver", PG_CHANNEL_GSM_DEBUG_RECEIVER),
};

// polygator gsm incoming call types
static struct pg_generic_param pg_call_gsm_incoming_types[] = {
	PG_GENERIC_PARAM("deny", PG_CALL_GSM_INCOMING_TYPE_DENY),
	PG_GENERIC_PARAM("dtmf", PG_CALL_GSM_INCOMING_TYPE_DTMF),
	PG_GENERIC_PARAM("spec", PG_CALL_GSM_INCOMING_TYPE_SPEC),
	PG_GENERIC_PARAM("dyn", PG_CALL_GSM_INCOMING_TYPE_DYN),
};
// polygator gsm outgoing call types
static struct pg_generic_param pg_call_gsm_outgoing_types[] = {
	PG_GENERIC_PARAM("deny", PG_CALL_GSM_OUTGOING_TYPE_DENY),
	PG_GENERIC_PARAM("allow", PG_CALL_GSM_OUTGOING_TYPE_ALLOW),
};
// polygator callwait states
static struct pg_generic_param pg_callwait_states[] = {
	PG_GENERIC_PARAM("disable", PG_CALLWAIT_STATE_DISABLE),
	PG_GENERIC_PARAM("enable", PG_CALLWAIT_STATE_ENABLE),
	PG_GENERIC_PARAM("query", PG_CALLWAIT_STATE_QUERY),
};
// polygator clir states
static struct pg_generic_param pg_clir_states[] = {
	PG_GENERIC_PARAM("subscription", PG_CLIR_STATE_SUBSCRIPTION),
	PG_GENERIC_PARAM("invocation", PG_CLIR_STATE_INVOCATION),
	PG_GENERIC_PARAM("suppression", PG_CLIR_STATE_SUPPRESSION),
	PG_GENERIC_PARAM("query", PG_CLIR_STATE_QUERY),
};
// polygator GSM module types
static struct pg_generic_param pg_gsm_module_types[] = {
	PG_GENERIC_PARAM("SIM300", POLYGATOR_MODULE_TYPE_SIM300),
	PG_GENERIC_PARAM("SIM900", POLYGATOR_MODULE_TYPE_SIM900),
	PG_GENERIC_PARAM("M10", POLYGATOR_MODULE_TYPE_M10),
	PG_GENERIC_PARAM("SIM5215", POLYGATOR_MODULE_TYPE_SIM5215),
};
// polygator gsm call progress types
enum {
	PG_GSM_CALL_PROGRESS_TYPE_UNKNOWN = 0,
	PG_GSM_CALL_PROGRESS_TYPE_PROCEEDING,
	PG_GSM_CALL_PROGRESS_TYPE_ALERTING,
	PG_GSM_CALL_PROGRESS_TYPE_ANSWER,
};
static struct pg_generic_param pg_call_gsm_progress_types[] = {
	PG_GENERIC_PARAM("proceeding", PG_GSM_CALL_PROGRESS_TYPE_PROCEEDING),
	PG_GENERIC_PARAM("alerting", PG_GSM_CALL_PROGRESS_TYPE_ALERTING),
	PG_GENERIC_PARAM("answer", PG_GSM_CALL_PROGRESS_TYPE_ANSWER),
};

static struct pg_generic_param pg_vinetic_ali_nelec_nlpms[] = {
	PG_GENERIC_PARAM("limit", VIN_NLPM_LIMIT),
	PG_GENERIC_PARAM("sign", VIN_NLPM_SIGN_NOISE),
	PG_GENERIC_PARAM("white", VIN_NLPM_WHITE_NOISE),
};

static struct timeval waitrdy_timeout = {20, 0};
static struct timeval waitsuspend_timeout = {30, 0};
static struct timeval testfun_timeout = {10, 0};
static struct timeval testfunsend_timeout = {1, 0};
static struct timeval callready_timeout = {300, 0};
static struct timeval runquartersecond_timeout = {0, 250000};
static struct timeval runhalfsecond_timeout = {0, 500000};
static struct timeval runonesecond_timeout = {1, 0};
static struct timeval runfivesecond_timeout = {5, 0};
static struct timeval halfminute_timeout = {30, 0};
static struct timeval runoneminute_timeout = {60, 0};
static struct timeval waitviodown_timeout = {120, 0};
static struct timeval testviodown_timeout = {1, 0};
static struct timeval onesec_timeout = {1, 0};
static struct timeval zero_timeout = {0, 1000};
static struct timeval simpoll_timeout = {2, 0};
static struct timeval pinwait_timeout = {8, 0};
static struct timeval registering_timeout = {60, 0};

static struct timeval proceeding_timeout = {5, 0};

static char pg_config_file[] = "polygator.conf";

static int pg_at_response_timeout = 2;

AST_MUTEX_DEFINE_STATIC(pg_lock);

static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_board_list, pg_board);
static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_channel_gsm_list, pg_channel_gsm);
static struct pg_channel_gsm *pg_channel_gsm_last = NULL;

static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_trunk_gsm_list, pg_trunk_gsm);

static u_int32_t channel_id = 0;

static sqlite3* pg_gen_db = NULL;
AST_MUTEX_DEFINE_STATIC(pg_gen_db_lock);
static sqlite3* pg_cdr_db = NULL;
AST_MUTEX_DEFINE_STATIC(pg_cdr_db_lock);
static sqlite3* pg_sms_db = NULL;
AST_MUTEX_DEFINE_STATIC(pg_sms_db_lock);
static sqlite3* pg_sim_db = NULL;
AST_MUTEX_DEFINE_STATIC(pg_sim_db_lock);

//------------------------------------------------------------------------------
// pg_cli_generating_prepare()
// get from asterisk-1.6.0.x main/cli.c parse_args()
//------------------------------------------------------------------------------
static int pg_cli_generating_prepare(char *s, int *argc, char *argv[])
{
	char *cur;
	int x = 0;
	int quoted = 0;
	int escaped = 0;
	int whitespace = 1;

	if (s == NULL)	/* invalid, though! */
		return -1;

	cur = s;
	/* scan the original string copying into cur when needed */
	for (; *s ; s++)
	{
		if (x >= AST_MAX_ARGS - 1) {
			ast_log(LOG_WARNING, "Too many arguments, truncating at %s\n", s);
			break;
		}
		if (*s == '"' && !escaped) {
			quoted = !quoted;
			if (quoted && whitespace) {
				/* start a quoted string from previous whitespace: new argument */
				argv[x++] = cur;
				whitespace = 0;
			}
		} else if ((*s == ' ' || *s == '\t') && !(quoted || escaped)) {
			/* If we are not already in whitespace, and not in a quoted string or
			   processing an escape sequence, and just entered whitespace, then
			   finalize the previous argument and remember that we are in whitespace
			*/
			if (!whitespace) {
				*cur++ = '\0';
				whitespace = 1;
			}
		} else if (*s == '\\' && !escaped) {
			escaped = 1;
		} else {
			if (whitespace) {
				/* we leave whitespace, and are not quoted. So it's a new argument */
				argv[x++] = cur;
				whitespace = 0;
			}
			*cur++ = *s;
			escaped = 0;
		}
	}
	/* Null terminate */
	*cur++ = '\0';
	/* XXX put a NULL in the last argument, because some functions that take
	 * the array may want a null-terminated array.
	 * argc still reflects the number of non-NULL entries.
	 */
	argv[x] = NULL;
	*argc = x;
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_cli_generating_prepare()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_second_to_dhms()
//------------------------------------------------------------------------------
static char * pg_second_to_dhms(char *buf, time_t sec)
{
	char *res = buf;
	int d,m,h,s;
	//
	d = 0;
	m = 0;
	h = 0;
	s = 0;

	// get days
	d = sec / (60*60*24);
	sec = sec % (60*60*24);
	// get hours
	h = sec / (60*60);
	sec = sec % (60*60);
	// get minutes
	m = sec / (60);
	sec = sec % (60);
	// get seconds
	s = sec;

	sprintf(buf, "%03d:%02d:%02d:%02d", d, h, m, s);

	return res;
}
//------------------------------------------------------------------------------
// end of pg_second_to_dhms()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_sim_db_table_create()
//------------------------------------------------------------------------------
static void pg_sim_db_table_create(ast_mutex_t *lock)
{
	char *str0, *str1;
	int res;
	int row;
	sqlite3_stmt *sql0, *sql1;

	ast_mutex_lock(&pg_sim_db_lock);

	// create table for SIM card PIN storage
	str0 = sqlite3_mprintf("SELECT COUNT(id) FROM 'simcards';");
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sim_db, str0, strlen(str0), &sql0, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
					break;
				}
			}
			sqlite3_finalize(sql0);
			break;
		} else if (res == SQLITE_BUSY) {
			usleep(1);
			continue;
		} else if ((res == SQLITE_ERROR) && (strstr(sqlite3_errmsg(pg_sim_db), "no such table"))) {
			str1 = sqlite3_mprintf("CREATE TABLE 'simcards' ("
										"id INTEGER PRIMARY KEY, "
										"iccid TEXT, "
										"pin TEXT, "
										"puk TEXT);");
			while (1)
			{
				res = sqlite3_prepare_fun(pg_sim_db, str1, strlen(str1), &sql1, NULL);
				if (res == SQLITE_OK) {
					row = 0;
					while (1)
					{
						res = sqlite3_step(sql1);
						if (res == SQLITE_ROW)
							row++;
						else if (res == SQLITE_DONE)
							break;
						else if (res == SQLITE_BUSY) {
							if (lock) ast_mutex_unlock(lock);
							usleep(1000);
							if (lock) ast_mutex_lock(lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
							break;
						}
					}
					sqlite3_finalize(sql1);
					break;
				} else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
					break;
				}
			}
			sqlite3_free(str1);
			break;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
			break;
		}
	}
	sqlite3_free(str0);

	ast_mutex_unlock(&pg_sim_db_lock);
}
//------------------------------------------------------------------------------
// end of pg_sim_db_table_create()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_pin_by_iccid()
//------------------------------------------------------------------------------
static char *pg_get_pin_by_iccid(const char *iccid, ast_mutex_t *lock)
{
	char *pin;
	char *str;
	int res;
	sqlite3_stmt *sql;

	pin = NULL;

	ast_mutex_lock(&pg_sim_db_lock);

	str = sqlite3_mprintf("SELECT pin FROM 'simcards' WHERE iccid='%q';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sim_db, str, strlen(str), &sql, NULL);
		if (res == SQLITE_OK) {
			while (1)
			{
				res = sqlite3_step(sql);
				if (res == SQLITE_ROW) {
					pin = ast_strdup((char *)sqlite3_column_text(sql, 0));
					if (pin) break;
				} else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
					break;
				}
			}
			sqlite3_finalize(sql);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
			break;
		}
	}
	sqlite3_free(str);

	ast_mutex_unlock(&pg_sim_db_lock);

	return pin;
}
//------------------------------------------------------------------------------
// end of pg_get_pin_by_iccid()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_puk_by_iccid()
//------------------------------------------------------------------------------
static char *pg_get_puk_by_iccid(const char *iccid, ast_mutex_t *lock)
{
	char *puk;
	char *str;
	int res;
	sqlite3_stmt *sql;

	puk = NULL;

	ast_mutex_lock(&pg_sim_db_lock);

	str = sqlite3_mprintf("SELECT puk FROM 'simcards' WHERE iccid='%q';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sim_db, str, strlen(str), &sql, NULL);
		if (res == SQLITE_OK) {
			while (1)
			{
				res = sqlite3_step(sql);
				if (res == SQLITE_ROW) {
					puk = ast_strdup((char *)sqlite3_column_text(sql, 0));
					if (puk) break;
				} else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
					break;
				}
			}
			sqlite3_finalize(sql);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
			break;
		}
	}
	sqlite3_free(str);

	ast_mutex_unlock(&pg_sim_db_lock);

	return puk;
}
//------------------------------------------------------------------------------
// end of pg_get_puk_by_iccid()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_set_pin_by_iccid()
//------------------------------------------------------------------------------
static void pg_set_pin_by_iccid(const char *iccid, const char *pin, ast_mutex_t *lock)
{
	char *str;
	int res;
	int row;
	sqlite3_stmt *sql;

	ast_mutex_lock(&pg_sim_db_lock);

	// check for entry
	row = 0;
	str = sqlite3_mprintf("SELECT pin FROM 'simcards' WHERE iccid='%q';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sim_db, str, strlen(str), &sql, NULL);
		if (res == SQLITE_OK) {
			while (1)
			{
				res = sqlite3_step(sql);
				if (res == SQLITE_ROW) {
					row++;
				} else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
					break;
				}
			}
			sqlite3_finalize(sql);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
			break;
		}
	}
	sqlite3_free(str);

	if (row) {
		// update entry
		str = sqlite3_mprintf("UPDATE 'simcards' SET pin='%q' WHERE iccid='%q';", pin, iccid);
		while (1)
		{
			res = sqlite3_prepare_fun(pg_sim_db, str, strlen(str), &sql, NULL);
			if (res == SQLITE_OK) {
				while (1)
				{
					res = sqlite3_step(sql);
					if (res == SQLITE_ROW)
						row++;
					else if (res == SQLITE_DONE)
						break;
					else if (res == SQLITE_BUSY) {
						if (lock) ast_mutex_unlock(lock);
						usleep(1000);
						if (lock) ast_mutex_lock(lock);
						continue;
					} else {
						ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
						break;
					}
				}
				sqlite3_finalize(sql);
				break;
			} else if (res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
			} else {
				ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
				break;
			}
		}
		sqlite3_free(str);
	} else {
		// insert new entry
		str = sqlite3_mprintf("INSERT INTO 'simcards' (iccid, pin) VALUES ('%q','%q');", iccid, pin);
		while (1)
		{
			res = sqlite3_prepare_fun(pg_sim_db, str, strlen(str), &sql, NULL);
			if (res == SQLITE_OK) {
				while (1)
				{
					res = sqlite3_step(sql);
					if (res == SQLITE_ROW)
						row++;
					else if (res == SQLITE_DONE)
						break;
					else if (res == SQLITE_BUSY) {
						if (lock) ast_mutex_unlock(lock);
						usleep(1000);
						if (lock) ast_mutex_lock(lock);
						continue;
					} else {
						ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
						break;
					}
				}
				sqlite3_finalize(sql);
				break;
			}
			else if (res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
			} else {
				ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
				break;
			}
		}
		sqlite3_free(str);
	}

	ast_mutex_unlock(&pg_sim_db_lock);
}
//------------------------------------------------------------------------------
// end of pg_set_pin_by_iccid()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_set_puk_by_iccid()
//------------------------------------------------------------------------------
static void pg_set_puk_by_iccid(const char *iccid, const char *puk, ast_mutex_t *lock)
{
	char *str;
	int res;
	int row;
	sqlite3_stmt *sql;

	ast_mutex_lock(&pg_sim_db_lock);

	// check for entry
	row = 0;
	str = sqlite3_mprintf("SELECT puk FROM 'simcards' WHERE iccid='%q';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sim_db, str, strlen(str), &sql, NULL);
		if (res == SQLITE_OK) {
			while (1)
			{
				res = sqlite3_step(sql);
				if (res == SQLITE_ROW) {
					row++;
				} else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
					break;
				}
			}
			sqlite3_finalize(sql);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
			break;
		}
	}
	sqlite3_free(str);

	if (row) {
		// update entry
		str = sqlite3_mprintf("UPDATE 'simcards' SET puk='%q' WHERE iccid='%q';", puk, iccid);
		while (1)
		{
			res = sqlite3_prepare_fun(pg_sim_db, str, strlen(str), &sql, NULL);
			if (res == SQLITE_OK) {
				while (1)
				{
					res = sqlite3_step(sql);
					if (res == SQLITE_ROW)
						row++;
					else if (res == SQLITE_DONE)
						break;
					else if (res == SQLITE_BUSY) {
						if (lock) ast_mutex_unlock(lock);
						usleep(1000);
						if (lock) ast_mutex_lock(lock);
						continue;
					} else {
						ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
						break;
					}
				}
				sqlite3_finalize(sql);
				break;
			} else if (res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
			} else {
				ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
				break;
			}
		}
		sqlite3_free(str);
	} else {
		// insert new entry
		str = sqlite3_mprintf("INSERT INTO 'simcards' (iccid, puk) VALUES ('%q','%q');", iccid, puk);
		while (1)
		{
			res = sqlite3_prepare_fun(pg_sim_db, str, strlen(str), &sql, NULL);
			if (res == SQLITE_OK) {
				while (1)
				{
					res = sqlite3_step(sql);
					if (res == SQLITE_ROW)
						row++;
					else if (res == SQLITE_DONE)
						break;
					else if (res == SQLITE_BUSY) {
						if (lock) ast_mutex_unlock(lock);
						usleep(1000);
						if (lock) ast_mutex_lock(lock);
						continue;
					} else {
						ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
						break;
					}
				}
				sqlite3_finalize(sql);
				break;
			}
			else if (res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
			} else {
				ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sim_db));
				break;
			}
		}
		sqlite3_free(str);
	}

	ast_mutex_unlock(&pg_sim_db_lock);
}
//------------------------------------------------------------------------------
// end of pg_set_puk_by_iccid()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_sms_db_table_create()
//------------------------------------------------------------------------------
static void pg_sms_db_table_create(const char *iccid, ast_mutex_t *lock)
{
	char *str0, *str1;
	int res;
	int row;
	sqlite3_stmt *sql0, *sql1;

	ast_mutex_lock(&pg_sms_db_lock);

	// create table for inbox SMS
	str0 = sqlite3_mprintf("SELECT COUNT(msgno) FROM '%q-inbox';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_finalize(sql0);
			break;
		} else if (res == SQLITE_BUSY) {
			usleep(1);
			continue;
		} else if ((res == SQLITE_ERROR) && (strstr(sqlite3_errmsg(pg_sms_db), "no such table"))) {
			str1 = sqlite3_mprintf("CREATE TABLE '%q-inbox' ("
										"msgno INTEGER PRIMARY KEY, "
										"pdu TEXT, "
										"msgid INTEGER, "
										"status INTEGER, "
										"scatype INTEGER, "
										"scaname TEXT, "
										"oatype INTEGER, "
										"oaname TEXT, "
										"dcs INTEGER, "
										"sent INTEGER, "
										"received INTEGER, "
										"partid INTEGER, "
										"partof INTEGER, "
										"part INTEGER, "
										"content TEXT"
										");", iccid);
			while (1)
			{
				res = sqlite3_prepare_fun(pg_sms_db, str1, strlen(str1), &sql1, NULL);
				if (res == SQLITE_OK) {
					row = 0;
					while (1)
					{
						res = sqlite3_step(sql1);
						if (res == SQLITE_ROW)
							row++;
						else if (res == SQLITE_DONE)
							break;
						else if (res == SQLITE_BUSY) {
							if (lock) ast_mutex_unlock(lock);
							usleep(1000);
							if (lock) ast_mutex_lock(lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_finalize(sql1);
					break;
				} else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_free(str1);
			break;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
			break;
		}
	}
	sqlite3_free(str0);

	// create table for outbox SMS
	str0 = sqlite3_mprintf("SELECT COUNT(msgno) FROM '%q-outbox';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_finalize(sql0);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else if ((res == SQLITE_ERROR) && (strstr(sqlite3_errmsg(pg_sms_db), "no such table"))) {
			str1 = sqlite3_mprintf("CREATE TABLE '%q-outbox' ("
									"msgno INTEGER PRIMARY KEY, "
									"destination TEXT, "
									"content TEXT, "
									"flash INTEGER, "
									"enqueued INTEGER, "
									"hash VARCHAR(32) UNIQUE"
									");", iccid);
			while (1)
			{
				res = sqlite3_prepare_fun(pg_sms_db, str1, strlen(str1), &sql1, NULL);
				if (res == SQLITE_OK) {
					row = 0;
					while (1)
					{
						res = sqlite3_step(sql1);
						if (res == SQLITE_ROW)
							row++;
						else if (res == SQLITE_DONE)
							break;
						else if (res == SQLITE_BUSY) {
							if (lock) ast_mutex_unlock(lock);
							usleep(1000);
							if (lock) ast_mutex_lock(lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_finalize(sql1);
					break;
				} else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_free(str1);
			break;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
			break;
		}
	}
	sqlite3_free(str0);

	// create table for preparing message PDU
	str0 = sqlite3_mprintf("SELECT COUNT(msgno) FROM '%q-preparing';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
					}
				}
			sqlite3_finalize(sql0);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else if ((res == SQLITE_ERROR) && (strstr(sqlite3_errmsg(pg_sms_db), "no such table"))) {
			str1 = sqlite3_mprintf("CREATE TABLE '%q-preparing' ("
										"msgno INTEGER PRIMARY KEY, "
										"owner TEXT, "
										"msgid INTEGER, "
										"status INTEGER, "
										"scatype INTEGER, "
										"scaname TEXT, "
										"datype INTEGER, "
										"daname TEXT, "
										"dcs INTEGER, "
										"partid INTEGER, "
										"partof INTEGER, "
										"part INTEGER, "
										"submitpdulen INTEGER, "
										"submitpdu TEXT, "
										"attempt INTEGER, "
										"hash VARCHAR(32), "
										"flash INTEGER, "
										"content TEXT"
										");", iccid);
			while (1)
			{
				res = sqlite3_prepare_fun(pg_sms_db, str1, strlen(str1), &sql1, NULL);
				if (res == SQLITE_OK) {
					row = 0;
					while (1)
					{
						res = sqlite3_step(sql1);
						if (res == SQLITE_ROW)
							row++;
						else if (res == SQLITE_DONE)
							break;
						else if (res == SQLITE_BUSY) {
							if (lock) ast_mutex_unlock(lock);
							usleep(1000);
							if (lock) ast_mutex_lock(lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_finalize(sql1);
					break;
				} else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_free(str1);
			break;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
			break;
			}
	}
	sqlite3_free(str0);

	// create table for sent SMS
	str0 = sqlite3_mprintf("SELECT COUNT(msgno) FROM '%q-sent';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_finalize(sql0);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else if ((res == SQLITE_ERROR) && (strstr(sqlite3_errmsg(pg_sms_db), "no such table"))) {
			str1 = sqlite3_mprintf("CREATE TABLE '%q-sent' ("
										"msgno INTEGER PRIMARY KEY, "
										"owner TEXT, "
										"msgid INTEGER, "
										"status INTEGER, "
										"mr INTEGER, "
										"scatype INTEGER, "
										"scaname TEXT, "
										"datype INTEGER, "
										"daname TEXT, "
										"dcs INTEGER, "
										"sent INTEGER, "
										"received INTEGER, "
										"partid INTEGER, "
										"partof INTEGER, "
										"part INTEGER, "
										"submitpdulen INTEGER, "
										"submitpdu TEXT, "
										"stareppdulen INTEGER, "
										"stareppdu TEXT, "
										"attempt INTEGER, "
										"hash VARCHAR(32), "
										"flash INTEGER, "
										"content TEXT"
										");", iccid);
			while (1)
			{
				res = sqlite3_prepare_fun(pg_sms_db, str1, strlen(str1), &sql1, NULL);
				if (res == SQLITE_OK) {
					row = 0;
					while (1)
					{
						res = sqlite3_step(sql1);
						if (res == SQLITE_ROW)
							row++;
						else if (res == SQLITE_DONE)
							break;
						else if (res == SQLITE_BUSY) {
							if (lock) ast_mutex_unlock(lock);
							usleep(1000);
							if (lock) ast_mutex_lock(lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_finalize(sql1);
					break;
				} else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_free(str1);
			break;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
			break;
		}
	}
	sqlite3_free(str0);

	// create table for discard SMS
	str0 = sqlite3_mprintf("SELECT COUNT(id) FROM '%q-discard';", iccid);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_finalize(sql0);
			break;
		} else if (res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
		} else if ((res == SQLITE_ERROR) && (strstr(sqlite3_errmsg(pg_sms_db), "no such table"))) {
			str1 = sqlite3_mprintf("CREATE TABLE '%q-discard' ("
										"id INTEGER PRIMARY KEY, "
										"destination TEXT, "
										"content TEXT, "
										"flash INTEGER, "
										"cause TEXT, "
										"timestamp INTEGER, "
										"hash VARCHAR(32)"
										");", iccid);
			while (1)
			{
				res = sqlite3_prepare_fun(pg_sms_db, str1, strlen(str1), &sql1, NULL);
				if (res == SQLITE_OK) {
					row = 0;
					while (1)
					{
						res = sqlite3_step(sql1);
						if (res == SQLITE_ROW)
							row++;
						else if (res == SQLITE_DONE)
							break;
						else if (res == SQLITE_BUSY) {
							if (lock) ast_mutex_unlock(lock);
							usleep(1000);
							if (lock) ast_mutex_lock(lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_finalize(sql1);
					break;
				} else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
					break;
				}
			}
			sqlite3_free(str1);
			break;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_sms_db));
			break;
		}
	}
	sqlite3_free(str0);

	ast_mutex_unlock(&pg_sms_db_lock);
}
//------------------------------------------------------------------------------
// end of pg_sms_db_table_create()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_dcr_table_create()
//------------------------------------------------------------------------------
static void pg_dcr_table_create(const char *imsi, ast_mutex_t *lock)
{
	char *str0, *str1;
	int res;
	int row;
	sqlite3_stmt *sql0, *sql1;

	ast_mutex_lock(&pg_gen_db_lock);

	// create table for dynamic CLIP routing
	str0 = sqlite3_mprintf("SELECT COUNT(id) FROM '%q-dcr';", imsi);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_gen_db, str0, strlen(str0), &sql0, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
					break;
				}
			}
			sqlite3_finalize(sql0);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else if ((res == SQLITE_ERROR) && (strstr(sqlite3_errmsg(pg_gen_db), "no such table"))) {
			str1 = sqlite3_mprintf("CREATE TABLE '%q-dcr' ("
										"id INTEGER PRIMARY KEY, "
										"fromtype INTEGER, "
										"fromname TEXT, "
										"totype INTEGER, "
										"toname TEXT, "
										"timestamp INTEGER);", imsi);
			while (1)
			{
				res = sqlite3_prepare_fun(pg_gen_db, str1, strlen(str1), &sql1, NULL);
				if (res == SQLITE_OK) {
					row = 0;
					while (1)
					{
						res = sqlite3_step(sql1);
						if (res == SQLITE_ROW)
							row++;
						else if (res == SQLITE_DONE)
							break;
						else if (res == SQLITE_BUSY) {
							if (lock) ast_mutex_unlock(lock);
							usleep(1000);
							if (lock) ast_mutex_lock(lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
							break;
						}
					}
					sqlite3_finalize(sql1);
					break;
				} else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
					break;
				}
			}
			sqlite3_free(str1);
			break;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
			break;
		}
	}
	sqlite3_free(str0);

	ast_mutex_unlock(&pg_gen_db_lock);
}
//------------------------------------------------------------------------------
// end of pg_dcr_table_create()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_dcr_table_update()
//------------------------------------------------------------------------------
static void pg_dcr_table_update(const char *imsi, struct address *from, struct address *to, ast_mutex_t *lock)
{
	char *str;
	int res;
	int row;
	sqlite3_stmt *sql;
	struct timeval tv;

	ast_mutex_lock(&pg_gen_db_lock);

	// update dynamic clip routing table
	// delete entry with the same called name = from
	str = sqlite3_mprintf("DELETE FROM '%q-dcr' WHERE fromtype=%d AND fromname='%q';", imsi, from->type.full, from->value);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_gen_db, str, strlen(str), &sql, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
					break;
				}
			}
			sqlite3_finalize(sql);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
			break;
		}
	}
	sqlite3_free(str);
	// insert new entry
	gettimeofday(&tv, NULL);
	str = sqlite3_mprintf("INSERT INTO '%q-dcr' (fromtype, fromname, totype, toname, timestamp) VALUES (%d,'%q',%d,'%q',%ld);",
							imsi, from->type.full, from->value,
							to->type.full, to->value, tv.tv_sec);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_gen_db, str, strlen(str), &sql, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
					break;
				}
			}
			sqlite3_finalize(sql);
			break;
		} else if(res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
			break;
		}
	}
	sqlite3_free(str);

	ast_mutex_unlock(&pg_gen_db_lock);
}
//------------------------------------------------------------------------------
// end of pg_dcr_table_update()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_dcr_table_get_match_record()
//------------------------------------------------------------------------------
static struct address *pg_dcr_table_get_match_record(const char *imsi, struct address *from, time_t ttl, struct address *to, ast_mutex_t *lock)
{
	char *str0, *str1;
	int res;
	int row;
	sqlite3_stmt *sql0, *sql1;
	struct timeval tv;
	struct address testaddr;
	struct address *resaddr = NULL;

	if (!imsi)
		return NULL;

	ast_mutex_lock(&pg_gen_db_lock);

	gettimeofday(&tv, NULL);
	str0 = sqlite3_mprintf("SELECT fromtype,fromname,totype,toname FROM '%q-dcr' WHERE timestamp>%ld ORDER BY timestamp DESC;", imsi, (long int)(tv.tv_sec - ttl));
	res = sqlite3_prepare_fun(pg_gen_db, str0, strlen(str0), &sql0, NULL);
	if (res == SQLITE_OK) {
		row = 0;
		while (1)
		{
			res = sqlite3_step(sql0);
			if (res == SQLITE_ROW) {
				row++;
				testaddr.type.full = sqlite3_column_int(sql0, 0);
				ast_copy_string(testaddr.value, (char *)sqlite3_column_text(sql0, 1), MAX_ADDRESS_LENGTH);
				if (is_address_equal(&testaddr, from)) {
					to->type.full = sqlite3_column_int(sql0, 2);
					ast_copy_string(to->value, (char *)sqlite3_column_text(sql0, 3), MAX_ADDRESS_LENGTH);
					resaddr = to;
					break;
				}
			} else if (res == SQLITE_DONE)
				break;
			else if(res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
			} else {
				ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
				break;
			}
		}
		sqlite3_finalize(sql0);
		if (row) {
			// update timestamp
			str1 = sqlite3_mprintf("UPDATE '%q-dcr' SET timestamp=%ld WHERE fromtype=%d AND fromname='%q';", imsi, tv.tv_sec, from->type.full, from->value);
			res = sqlite3_prepare_fun(pg_gen_db, str1, strlen(str1), &sql1, NULL);
			if (res == SQLITE_OK) {
				row = 0;
				while (1)
				{
					res = sqlite3_step(sql1);
					if (res == SQLITE_ROW) {
						row++;
					} else if (res == SQLITE_DONE)
						break;
					else if (res == SQLITE_BUSY) {
						if (lock) ast_mutex_unlock(lock);
						usleep(1000);
						if (lock) ast_mutex_lock(lock);
						continue;
					} else {
						ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
						break;
					}
				}
				sqlite3_finalize(sql1);
			} else
				ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
			sqlite3_free(str1);
		}
	} else
		ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
	sqlite3_free(str0);
#if 0
	if (!resaddr) {
		// search in permanent routing table
		str0 = sqlite3_mprintf("SELECT fromtype,fromname,totype,toname FROM 'permanent-dcr';", iccid);
		res = sqlite3_prepare_fun(gendb, str0, strlen(str0), &sql0, NULL);
		sqlite3_free(str);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW) {
					row++;
					testaddr.type.full = sqlite3_column_int(sql0, 0);
					ast_copy_string(testaddr.value, (char *)sqlite3_column_text(sql, 1), MAX_ADDRESS_LENGTH);
					if (is_address_equal(&testaddr, from)) {
						to->type.full = sqlite3_column_int(sql0, 2);
						ast_copy_string(to->value, (char *)sqlite3_column_text(sql0, 3), MAX_ADDRESS_LENGTH);
						resaddr = to;
						break;
					}
				} else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
					break;
				}
			}
			sqlite3_finalize(sql0);
		} else
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
		sqlite3_free(str0);

	}
#endif

	ast_mutex_unlock(&pg_gen_db_lock);

	return resaddr;
}
//------------------------------------------------------------------------------
// end of pg_dcr_table_get_match_record()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cdr_table_create()
//------------------------------------------------------------------------------
static void pg_cdr_table_create(ast_mutex_t *lock)
{
	char *str0, *str1;
	int res;
	int row;
	sqlite3_stmt *sql0, *sql1;

	ast_mutex_lock(&pg_cdr_db_lock);

	// create table for CDR
	str0 = sqlite3_mprintf("SELECT COUNT(id) FROM 'cdr';");
	while (1)
	{
		res = sqlite3_prepare_fun(pg_cdr_db, str0, strlen(str0), &sql0, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql0);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
					break;
				}
			}
			sqlite3_finalize(sql0);
			break;
		} else if (res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else if ((res == SQLITE_ERROR) && (strstr(sqlite3_errmsg(pg_cdr_db), "no such table"))) {
			str1 = sqlite3_mprintf("CREATE TABLE 'cdr' ("
										"id INTEGER PRIMARY KEY, "
										"channel TEXT, "
										"imsi TEXT, "
										"callingtype INTEGER, "
										"callingname TEXT, "
										"calledtype INTEGER, "
										"calledname TEXT, "
										"direction INTEGER, "
										"starttime INTEGER, "
										"answertime INTEGER, "
										"endtime INTEGER, "
										"hungupside INTEGER, "
										"hungupcause INTEGER);");
			while (1)
			{
				res = sqlite3_prepare_fun(pg_cdr_db, str1, strlen(str1), &sql1, NULL);
				if (res == SQLITE_OK) {
					row = 0;
					while (1)
					{
						res = sqlite3_step(sql1);
						if (res == SQLITE_ROW)
							row++;
						else if (res == SQLITE_DONE)
							break;
						else if (res == SQLITE_BUSY) {
							if (lock) ast_mutex_unlock(lock);
							usleep(1000);
							if (lock) ast_mutex_lock(lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
							break;
						}
					}
					sqlite3_finalize(sql1);
					break;
				} else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
					break;
				}
			}
			sqlite3_free(str1);
			break;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
			break;
		}
	}
	sqlite3_free(str0);

	ast_mutex_unlock(&pg_cdr_db_lock);
}
//------------------------------------------------------------------------------
// end of pg_cdr_table_create()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cdr_table_insert_record()
//------------------------------------------------------------------------------
static void pg_cdr_table_insert_record(const char *channel, const char *imsi, struct pg_call_gsm *call, ast_mutex_t *lock)
{
	char *str;
	int res;
	int row;
	sqlite3_stmt *sql;
	struct timeval tv;

	ast_mutex_lock(&pg_cdr_db_lock);

	// insert new entry
	gettimeofday(&tv, NULL);
	str = sqlite3_mprintf("INSERT INTO 'cdr' (channel, imsi, callingtype, callingname, calledtype, calledname, "
						"direction, starttime, answertime, endtime, hungupside, hungupcause) "
						"VALUES "
						"('%q','%q',%d,'%q',%d,'%q',"
						"%d,%ld,%ld,%ld,%d,%d);",
						channel, imsi, call->calling_name.type.full, call->calling_name.value, call->called_name.type.full, call->called_name.value,
						call->direction, call->start_time.tv_sec, call->answer_time.tv_sec, tv.tv_sec, call->hangup_side, call->hangup_cause);
	while (1)
	{
		res = sqlite3_prepare_fun(pg_cdr_db, str, strlen(str), &sql, NULL);
		if (res == SQLITE_OK) {
			row = 0;
			while (1)
			{
				res = sqlite3_step(sql);
				if (res == SQLITE_ROW)
					row++;
				else if (res == SQLITE_DONE)
					break;
				else if (res == SQLITE_BUSY) {
					if (lock) ast_mutex_unlock(lock);
					usleep(1000);
					if (lock) ast_mutex_lock(lock);
					continue;
				} else {
					ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
					break;
				}
			}
			sqlite3_finalize(sql);
			break;
		} else if(res == SQLITE_BUSY) {
			if (lock) ast_mutex_unlock(lock);
			usleep(1000);
			if (lock) ast_mutex_lock(lock);
			continue;
		} else {
			ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
			break;
		}
	}
	sqlite3_free(str);

	ast_mutex_unlock(&pg_cdr_db_lock);
}
//------------------------------------------------------------------------------
// end of pg_cdr_table_insert_record()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cdr_table_get_out_total_call_count()
//------------------------------------------------------------------------------
static int64_t pg_cdr_table_get_out_total_call_count(const char *key, const char *value, time_t begin, time_t end, ast_mutex_t *lock)
{
	char *str0;
	int res;
	int row;
	sqlite3_stmt *sql0;

	int64_t count;

	if (!key || !value)
		return 0;

	ast_mutex_lock(&pg_cdr_db_lock);

	count = 0;
	str0 = sqlite3_mprintf("SELECT COUNT(id) FROM 'cdr' WHERE %q='%q' AND direction=%d AND starttime>%ld AND starttime<%ld;", key, value, PG_CALL_GSM_DIRECTION_OUTGOING, begin, end);
	res = sqlite3_prepare_fun(pg_cdr_db, str0, strlen(str0), &sql0, NULL);
	if (res == SQLITE_OK) {
		row = 0;
		while (1)
		{
			res = sqlite3_step(sql0);
			if (res == SQLITE_ROW) {
				row++;
				count = sqlite3_column_int64(sql0, 0);
				break;
			} else if (res == SQLITE_DONE)
				break;
			else if(res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
			} else {
				ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
				break;
			}
		}
		sqlite3_finalize(sql0);
	} else
		ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
	sqlite3_free(str0);

	ast_mutex_unlock(&pg_cdr_db_lock);

	return count;
}
//------------------------------------------------------------------------------
// end of pg_cdr_table_get_out_total_call_count()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cdr_table_get_out_answered_call_count()
//------------------------------------------------------------------------------
static int64_t pg_cdr_table_get_out_answered_call_count(const char *key, const char *value, time_t begin, time_t end, ast_mutex_t *lock)
{
	char *str0;
	int res;
	int row;
	sqlite3_stmt *sql0;

	int64_t count;

	if (!key || !value)
		return 0;

	ast_mutex_lock(&pg_cdr_db_lock);

	count = 0;
	str0 = sqlite3_mprintf("SELECT COUNT(id) FROM 'cdr' WHERE %q='%q' AND direction=%d AND answertime>0 AND starttime>%ld AND starttime<%ld;", key, value, PG_CALL_GSM_DIRECTION_OUTGOING, begin, end);
	res = sqlite3_prepare_fun(pg_cdr_db, str0, strlen(str0), &sql0, NULL);
	if (res == SQLITE_OK) {
		row = 0;
		while (1)
		{
			res = sqlite3_step(sql0);
			if (res == SQLITE_ROW) {
				row++;
				count = sqlite3_column_int64(sql0, 0);
				break;
			} else if (res == SQLITE_DONE)
				break;
			else if(res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
			} else {
				ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
				break;
			}
		}
		sqlite3_finalize(sql0);
	} else
		ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
	sqlite3_free(str0);

	ast_mutex_unlock(&pg_cdr_db_lock);

	return count;
}
//------------------------------------------------------------------------------
// end of pg_cdr_table_get_out_answered_call_count()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cdr_table_get_out_active_call_duration()
//------------------------------------------------------------------------------
static time_t pg_cdr_table_get_out_active_call_duration(const char *key, const char *value, time_t begin, time_t end, ast_mutex_t *lock)
{
	char *str0;
	int res;
	int row;
	sqlite3_stmt *sql0;

	time_t answer_time;
	time_t end_time;
	time_t duration;

	if (!key || !value)
		return 0;

	ast_mutex_lock(&pg_cdr_db_lock);

	duration = 0;
	str0 = sqlite3_mprintf("SELECT answertime, endtime FROM 'cdr' WHERE %q='%q' AND direction=%d AND answertime>0 AND starttime>%ld AND starttime<%ld;", key, value, PG_CALL_GSM_DIRECTION_OUTGOING, begin, end);
	res = sqlite3_prepare_fun(pg_cdr_db, str0, strlen(str0), &sql0, NULL);
	if (res == SQLITE_OK) {
		row = 0;
		while (1)
		{
			res = sqlite3_step(sql0);
			if (res == SQLITE_ROW) {
				row++;
				answer_time = sqlite3_column_int64(sql0, 0);
				end_time = sqlite3_column_int64(sql0, 1);
				duration += end_time - answer_time;
			} else if (res == SQLITE_DONE)
				break;
			else if(res == SQLITE_BUSY) {
				if (lock) ast_mutex_unlock(lock);
				usleep(1000);
				if (lock) ast_mutex_lock(lock);
				continue;
			} else {
				ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
				break;
			}
		}
		sqlite3_finalize(sql0);
	} else
		ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_cdr_db));
	sqlite3_free(str0);

	ast_mutex_unlock(&pg_cdr_db_lock);

	return duration;
}
//------------------------------------------------------------------------------
// end of pg_cdr_table_get_out_active_call_duration()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_board_by_name()
//------------------------------------------------------------------------------
static struct pg_board *pg_get_board_by_name(const char *name)
{
	struct pg_board *brd = NULL;
	// check for name present
	if (name) {
		// traverse board list for matching entry name
		AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
		{
			// compare name strings
			if (!strcmp(name, brd->name)) break;
		}
	}
	return brd;
}
//------------------------------------------------------------------------------
// end of pg_get_board_by_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_channel_gsm_by_name()
//------------------------------------------------------------------------------
static struct pg_channel_gsm *pg_get_channel_gsm_by_name(const char *name)
{
	struct pg_channel_gsm *ch_gsm = NULL;
	// check for name present
	if (name) {
		// traverse channel gsm list for matching entry name
		AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
		{
			ast_mutex_lock(&ch_gsm->lock);
			// compare name strings
			if (!strcmp(name, ch_gsm->alias)) {
				ast_mutex_unlock(&ch_gsm->lock);
				break;
			}
			ast_mutex_unlock(&ch_gsm->lock);
		}
	}
	return ch_gsm;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_gsm_by_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_channel_gsm_power_sequence_number()
//------------------------------------------------------------------------------
static int pg_get_channel_gsm_power_sequence_number(void)
{
	struct pg_channel_gsm *ch_gsm;
	int seq = 1;

	// traverse channel gsm list
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->power_sequence_number > 0) seq++;
		ast_mutex_unlock(&ch_gsm->lock);
	}

	return seq;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_gsm_power_sequence_number()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_channel_gsm_from_trunk_by_name()
//------------------------------------------------------------------------------
static struct pg_channel_gsm *pg_get_channel_gsm_from_trunk_by_name(struct pg_trunk_gsm *trunk, const char *name)
{
	struct pg_channel_gsm *ch_gsm = NULL;
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold;
	// check for name present
	if (trunk && name) {
		// traverse channel gsm list for matching entry name
		AST_LIST_TRAVERSE(&trunk->channel_gsm_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry)
		{
			// compare name strings
			if (!strcmp(name, ch_gsm_fold->channel_gsm->alias)) {
				ch_gsm = ch_gsm_fold->channel_gsm;
				break;
			}
		}
	}
	return ch_gsm;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_gsm_from_trunk_by_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_channel_gsm_fold_from_trunk_by_name()
//------------------------------------------------------------------------------
static struct pg_trunk_gsm_channel_gsm_fold *pg_get_channel_gsm_fold_from_trunk_by_name(struct pg_trunk_gsm *trunk, const char *name)
{
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold = NULL;
	// check for name present
	if (trunk && name) {
		// traverse channel gsm list for matching entry name
		AST_LIST_TRAVERSE(&trunk->channel_gsm_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry)
		{
			// compare name strings
			if (!strcmp(name, ch_gsm_fold->channel_gsm->alias))
				break;
		}
	}
	return ch_gsm_fold;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_gsm_fold_from_trunk_by_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_trunk_gsm_by_name()
//------------------------------------------------------------------------------
static struct pg_trunk_gsm *pg_get_trunk_gsm_by_name(const char *name)
{
	struct pg_trunk_gsm *tr_gsm = NULL;
	// check for name present
	if (name) {
		// check for trunk gsm list is not empty
		if ((tr_gsm = pg_general_trunk_gsm_list.first)) {
			// traverse trunk gsm list for matching entry name
			tr_gsm = NULL;
			AST_LIST_TRAVERSE(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry)
			{
				// compare name strings
				if (!strcmp(name, tr_gsm->name)) break;
			}
		}
	}
	return tr_gsm;
}
//------------------------------------------------------------------------------
// end of pg_get_trunk_gsm_by_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_module_type_get()
//------------------------------------------------------------------------------
static int pg_gsm_module_type_get(const char *module_type)
{
	size_t i;
	int res = POLYGATOR_MODULE_TYPE_UNKNOWN;
	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_gsm_module_types); i++)
	{
		if (!strcasecmp(module_type, pg_gsm_module_types[i].name))
			return pg_gsm_module_types[i].id;
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_gsm_module_type_get()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_module_type_to_string()
//------------------------------------------------------------------------------
static char *pg_gsm_module_type_to_string(unsigned int type)
{
	size_t i;
	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_gsm_module_types); i++)
	{
		if (type == pg_gsm_module_types[i].id)
			return pg_gsm_module_types[i].name;
	}
	return "unknown";
}
//------------------------------------------------------------------------------
// end of pg_gsm_module_type_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cahnnel_gsm_state_to_string()
//------------------------------------------------------------------------------
static char *pg_cahnnel_gsm_state_to_string(int state)
{
	switch (state)
	{
		case PG_CHANNEL_GSM_STATE_DISABLE: return "disable";
		case PG_CHANNEL_GSM_STATE_WAIT_RDY: return "wait for ready";
		case PG_CHANNEL_GSM_STATE_WAIT_CFUN: return "wait for cfun";
		case PG_CHANNEL_GSM_STATE_CHECK_PIN: return "check pin";
		case PG_CHANNEL_GSM_STATE_WAIT_CALL_READY: return "wait for call ready";
		case PG_CHANNEL_GSM_STATE_INIT: return "init";
		case PG_CHANNEL_GSM_STATE_RUN: return "run";
		case PG_CHANNEL_GSM_STATE_SUSPEND: return "suspend";
		case PG_CHANNEL_GSM_STATE_WAIT_SUSPEND: return "wait for suspend";
		case PG_CHANNEL_GSM_STATE_WAIT_VIO_DOWN: return "wait for vio down";
		case PG_CHANNEL_GSM_STATE_TEST_FUN: return "test fuctionality";
		case PG_CHANNEL_GSM_STATE_SERVICE: return "service";
		default: return "unknown";
	}
}
//------------------------------------------------------------------------------
// end of pg_cahnnel_gsm_state_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_callwait_state()
//------------------------------------------------------------------------------
static int pg_get_callwait_state(const char *callwait)
{
	size_t i;
	int cwt = PG_CALLWAIT_STATE_UNKNOWN;
	if (callwait) {
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_callwait_states); i++)
		{
			if (!strcmp(callwait, pg_callwait_states[i].name)) {
				cwt = pg_callwait_states[i].id;
				break;
			}
		}
	}
	return cwt;
}
//------------------------------------------------------------------------------
// end of pg_get_callwait_state()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_callwait_state_to_string()
//------------------------------------------------------------------------------
static char *pg_callwait_state_to_string(int state)
{
	size_t i;
	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_callwait_states); i++)
	{
		if (state == pg_callwait_states[i].id)
			return pg_callwait_states[i].name;
	}
	return "unknown";
}
//------------------------------------------------------------------------------
// end of pg_callwait_state_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_clir_state()
//------------------------------------------------------------------------------
static int pg_get_clir_state(const char *clir)
{
	size_t i;
	int clr = PG_CLIR_STATE_UNKNOWN;
	if (clir) {
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_clir_states); i++)
		{
			if (!strcmp(clir, pg_clir_states[i].name)) {
				clr = pg_clir_states[i].id;
				break;
			}
		}
	}
	return clr;
}
//------------------------------------------------------------------------------
// end of pg_get_clir_state()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_clir_state_to_string()
//------------------------------------------------------------------------------
static char *pg_clir_state_to_string(int state)
{
	size_t i;
	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_clir_states); i++)
	{
		if (state == pg_clir_states[i].id)
			return pg_clir_states[i].name;
	}
	return "unknown";
}
//------------------------------------------------------------------------------
// end of pg_clir_state_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_clir_status_to_string()
//------------------------------------------------------------------------------
static char *pg_clir_status_to_string(int status)
{	
	switch (status)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CLIR_STATUS_NOT_PROVISIONED:
			return "not provisioned";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CLIR_STATUS_PERM_MODE_PROVISIONED:
			return "permanent mode - provisioned";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CLIR_STATUS_UNKNOWN:
			return "unknown";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CLIR_STATUS_TEMP_MODE_RESTRICTED:
			return "temporary mode - presentation restricted";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CLIR_STATUS_TEMP_MODE_ALLOWED:
			return "temporary mode - presentation allowed";
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			return "error";
	}
}
//------------------------------------------------------------------------------
// end of pg_clir_status_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_channel_gsm_param()
//------------------------------------------------------------------------------
static int pg_get_channel_gsm_param(const char *param)
{
	size_t i;
	int prm = PG_CHANNEL_GSM_PARAM_UNKNOWN;
	if (param) {
		for (i=0; i<PG_CHANNEL_GSM_PARAMS_COUNT(pg_channel_gsm_params); i++)
		{
			if (!strcmp(param, pg_channel_gsm_params[i].name)) {
				prm = pg_channel_gsm_params[i].id;
				break;
			}
		}
	}
	return prm;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_gsm_param()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_channel_gsm_param()
//------------------------------------------------------------------------------
static inline int pg_get_channel_gsm_param_operation(const char *operation)
{
	int opid = PG_CHANNEL_GSM_PARAM_OP_UNKNOWN;
	if (operation) {
		if (!strcmp(operation, "get"))
			opid = PG_CHANNEL_GSM_PARAM_OP_GET;
		else if (!strcmp(operation, "set"))
			opid = PG_CHANNEL_GSM_PARAM_OP_SET;
		else if (!strcmp(operation, "query"))
			opid = PG_CHANNEL_GSM_PARAM_OP_QUERY;
		else if (!strcmp(operation, "delete"))
			opid = PG_CHANNEL_GSM_PARAM_OP_DELETE;
	}
	return opid;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_gsm_param_operation()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_channel_gsm_debug_param()
//------------------------------------------------------------------------------
static int pg_get_channel_gsm_debug_param(const char *debug)
{
	size_t i;
	int prm = PG_CHANNEL_GSM_DEBUG_UNKNOWN;
	if (debug) {
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_channel_gsm_debugs); i++)
		{
			if (!strcmp(debug, pg_channel_gsm_debugs[i].name)) {
				prm = pg_channel_gsm_debugs[i].id;
				break;
			}
		}
	}
	return prm;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_gsm_debug_param()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_gsm_call_progress()
//------------------------------------------------------------------------------
static int pg_get_gsm_call_progress(const char *progress)
{
	size_t i;
	int prg = PG_GSM_CALL_PROGRESS_TYPE_UNKNOWN;
	if (progress) {
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_progress_types); i++)
		{
			if (!strcmp(progress, pg_call_gsm_progress_types[i].name)) {
				prg = pg_call_gsm_progress_types[i].id;
				break;
			}
		}
	}
	return prg;
}
//------------------------------------------------------------------------------
// end of pg_get_gsm_call_progress()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_call_gsm_progress_to_string()
//------------------------------------------------------------------------------
static char *pg_call_gsm_progress_to_string(int progress)
{
	size_t i;
	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_progress_types); i++)
	{
		if (progress == pg_call_gsm_progress_types[i].id)
			return pg_call_gsm_progress_types[i].name;
	}
	return "unknown";
}
//------------------------------------------------------------------------------
// end of pg_call_gsm_progress_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_call_gsm_incoming_type()
//------------------------------------------------------------------------------
static int pg_get_call_gsm_incoming_type(const char *incoming)
{
	size_t i;
	int inc = PG_CALL_GSM_INCOMING_TYPE_UNKNOWN;
	if (incoming) {
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_incoming_types); i++)
		{
			if (!strcmp(incoming, pg_call_gsm_incoming_types[i].name)) {
				inc = pg_call_gsm_incoming_types[i].id;
				break;
			}
		}
	}
	return inc;
}
//------------------------------------------------------------------------------
// end of pg_get_call_gsm_incoming_type()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_call_gsm_incoming_type_to_string()
//------------------------------------------------------------------------------
static char *pg_call_gsm_incoming_type_to_string(int incoming)
{
	size_t i;
	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_incoming_types); i++)
	{
		if (incoming == pg_call_gsm_incoming_types[i].id)
			return pg_call_gsm_incoming_types[i].name;
	}
	return "unknown";
}
//------------------------------------------------------------------------------
// end of pg_call_gsm_incoming_type_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_gsm_call_outgoing()
//------------------------------------------------------------------------------
static int pg_get_gsm_call_outgoing(const char *outgoing)
{
	size_t i;
	int out = PG_CALL_GSM_OUTGOING_TYPE_UNKNOWN;
	if (outgoing) {
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_outgoing_types); i++)
		{
			if (!strcmp(outgoing, pg_call_gsm_outgoing_types[i].name)) {
				out = pg_call_gsm_outgoing_types[i].id;
				break;
			}
		}
	}
	return out;
}
//------------------------------------------------------------------------------
// end of pg_get_gsm_call_outgoing()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_call_gsm_outgoing_to_string()
//------------------------------------------------------------------------------
static char *pg_call_gsm_outgoing_to_string(int outgoing)
{
	size_t i;
	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_outgoing_types); i++)
	{
		if (outgoing == pg_call_gsm_outgoing_types[i].id)
			return pg_call_gsm_outgoing_types[i].name;
	}
	return "unknown";
}
//------------------------------------------------------------------------------
// end of pg_call_gsm_outgoing_to_string()
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
// pg_vinetic_get_ali_nelec_nlpm()
//------------------------------------------------------------------------------
static int pg_vinetic_get_ali_nelec_nlpm(const char *mode)
{
	size_t i;
	int out = -1;
	if (mode) {
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_vinetic_ali_nelec_nlpms); i++)
		{
			if (!strcmp(mode, pg_vinetic_ali_nelec_nlpms[i].name)) {
				out = pg_vinetic_ali_nelec_nlpms[i].id;
				break;
			}
		}
	}
	return out;
}
//------------------------------------------------------------------------------
// end of pg_vinetic_get_ali_nelec_nlpm()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_vinetic_ali_nelec_nlpm_to_string()
//------------------------------------------------------------------------------
static char *pg_vinetic_ali_nelec_nlpm_to_string(int mode)
{
	size_t i;
	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_vinetic_ali_nelec_nlpms); i++)
	{
		if (mode == pg_vinetic_ali_nelec_nlpms[i].id)
			return pg_vinetic_ali_nelec_nlpms[i].name;
	}
	return "unknown";
}
//------------------------------------------------------------------------------
// end of pg_vinetic_ali_nelec_nlpm_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_vio_get()
//------------------------------------------------------------------------------
static int pg_channel_gsm_vio_get(struct pg_channel_gsm *ch_gsm)
{
	FILE *fp;
	char buf[256];
	char name[64];
	char sim[64];
	char type[64];
	unsigned int pos;
	unsigned int vin_num;
	char vc_type[4];
	unsigned int vc_slot;
	unsigned int vio;
	int res = -1;

	if (ch_gsm) {
		if ((fp = fopen(ch_gsm->board->path, "r"))) {
			while (fgets(buf, sizeof(buf), fp))
			{
				if (sscanf(buf, "GSM%u %[0-9A-Za-z-] %[0-9A-Za-z/!-] %[0-9A-Za-z/!-] VIN%u%[ACMLP]%u VIO=%u", &pos, type, name, sim, &vin_num, vc_type, &vc_slot, &vio) == 8) {
					if (pos == ch_gsm->position_on_board) {
						res = vio;
						break;
					}
				}
			}
			fclose(fp);
		} else
			errno = ENODEV;
	} else
		errno = ENODEV;

	return res;
}
//------------------------------------------------------------------------------
// pg_channel_gsm_vio_get()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_power_set()
//------------------------------------------------------------------------------
static int pg_channel_gsm_power_set(struct pg_channel_gsm *ch_gsm, int state)
{
	FILE *fp;
	int res = -1;

	if (ch_gsm) {
		if ((fp = fopen(ch_gsm->board->path, "w"))) {
			fprintf(fp, "GSM%u PWR=%d", ch_gsm->position_on_board, state);
			fclose(fp);
			res = 0;
		} else
			errno = ENODEV;
	} else
		errno = ENODEV;

	return res;
}
//------------------------------------------------------------------------------
// pg_channel_gsm_power_set()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_serial_set()
//------------------------------------------------------------------------------
static int pg_channel_gsm_serial_set(struct pg_channel_gsm *ch_gsm, int serial)
{
	FILE *fp;
	int res = -1;

	if (ch_gsm) {
		if ((fp = fopen(ch_gsm->board->path, "w"))) {
			fprintf(fp, "GSM%u SERIAL=%d", ch_gsm->position_on_board, serial);
			fclose(fp);
			res = 0;
		} else
			errno = ENODEV;
	} else
		errno = ENODEV;

	return res;
}
//------------------------------------------------------------------------------
// pg_channel_gsm_serial_set()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_key_press()
//------------------------------------------------------------------------------
static int pg_channel_gsm_key_press(struct pg_channel_gsm *ch_gsm, int state)
{
	FILE *fp;
	int res = -1;

	if (ch_gsm) {
		if ((fp = fopen(ch_gsm->board->path, "w"))) {
			fprintf(fp, "GSM%u KEY=%d", ch_gsm->position_on_board, state);
			fclose(fp);
			res = 0;
		} else
			errno = ENODEV;
	} else
		errno = ENODEV;

	return res;
}
//------------------------------------------------------------------------------
// pg_channel_gsm_key_press()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_vinetic_from_board()
//------------------------------------------------------------------------------
static inline struct pg_vinetic *pg_get_vinetic_from_board(struct pg_board *brd, unsigned int pos)
{
	struct pg_vinetic *vin = NULL;

	if (brd) {
		ast_mutex_lock(&brd->lock);
		AST_LIST_TRAVERSE(&brd->vinetic_list, vin, pg_board_vinetic_list_entry)
			if (vin->position_on_board == pos) break;
		ast_mutex_unlock(&brd->lock);
	}

	return vin;
}
//------------------------------------------------------------------------------
// end of pg_get_vinetic_from_board()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_vinetic_by_name()
//------------------------------------------------------------------------------
static struct pg_vinetic *pg_get_vinetic_by_name(const char *name)
{
	struct pg_board *brd = NULL;
	struct pg_vinetic *vin = NULL;
	// check for name present
	if (name) {
		// traverse board list for matching entry name
		AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
		{
			ast_mutex_lock(&brd->lock);
			AST_LIST_TRAVERSE(&brd->vinetic_list, vin, pg_board_vinetic_list_entry)
				// compare name strings
				if (!strcmp(name, vin->name)) break;
			ast_mutex_unlock(&brd->lock);
		}
	}
	return vin;
}
//------------------------------------------------------------------------------
// end of pg_get_vinetic_by_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_is_vinetic_run()
//------------------------------------------------------------------------------
static inline int pg_is_vinetic_run(struct pg_vinetic *vin)
{
	int res = 0;

	if (vin) {
		ast_mutex_lock(&vin->lock);
		if (vin->state == PG_VINETIC_STATE_RUN)
			res = 1;
		ast_mutex_unlock(&vin->lock);
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_is_vinetic_run()
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
// pg_get_channel_rtp()
//------------------------------------------------------------------------------
static inline struct pg_channel_rtp *pg_get_channel_rtp(struct pg_vinetic *vin)
{
	struct pg_channel_rtp *rtp = NULL;

	if (vin) {
		ast_mutex_lock(&vin->lock);
		AST_LIST_TRAVERSE(&vin->channel_rtp_list, rtp, pg_vinetic_channel_rtp_list_entry)
		{
			ast_mutex_lock(&rtp->lock);
			if (!rtp->busy) {
				if ((rtp->fd = open(rtp->path, O_RDWR | O_NONBLOCK)) < 0) {
					ast_log(LOG_ERROR, "RTP channel=\"%s\": can't open device \"%s\": %s\n", rtp->name, rtp->path, strerror(errno));
				} else {
					// init statistic counter
					rtp->send_sid_count = 0;
					rtp->send_drop_count = 0;
					rtp->send_frame_count = 0;
					rtp->recv_frame_count = 0;
					// init DTMF buffer
					rtp->dtmfptr = rtp->dtmfbuf;
					rtp->dtmfbuf[0] = '\0';
					rtp->busy = 1;
					ast_mutex_unlock(&rtp->lock);
					break;
				}
			}
			ast_mutex_unlock(&rtp->lock);
		}
		ast_mutex_unlock(&vin->lock);
	}

	return rtp;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_rtp()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_put_channel_rtp()
//------------------------------------------------------------------------------
static inline void pg_put_channel_rtp(struct pg_channel_rtp *rtp)
{
	if (rtp) {
		ast_mutex_lock(&rtp->lock);
		close(rtp->fd);
		rtp->fd = -1;
		rtp->busy = 0;
		ast_mutex_unlock(&rtp->lock);
	}
}
//------------------------------------------------------------------------------
// end of pg_put_channel_rtp()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_call_gsm_state_to_string()
//------------------------------------------------------------------------------
static char *pg_call_gsm_state_to_string(int state)
{
	switch (state)
	{
		case PG_CALL_GSM_STATE_NULL: return "null";
		case PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING: return "outgoing call proceeding";
		case PG_CALL_GSM_STATE_CALL_DELIVERED: return "call delivered";
		case PG_CALL_GSM_STATE_CALL_PRESENT: return "call present";
		case PG_CALL_GSM_STATE_CALL_RECEIVED: return "call received";
		case PG_CALL_GSM_STATE_ACTIVE: return "active";
		case PG_CALL_GSM_STATE_LOCAL_HOLD: return "local hold";
		case PG_CALL_GSM_STATE_REMOTE_HOLD: return "remote hold";
		case PG_CALL_GSM_STATE_RELEASE_INDICATION: return "release indication";
		case PG_CALL_GSM_STATE_OVERLAP_RECEIVING: return "overlap receiving";
		default: return "unknown";
	}
}
//------------------------------------------------------------------------------
// end of pg_call_gsm_state_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_call_gsm_message_to_string()
//------------------------------------------------------------------------------
static char *pg_call_gsm_message_to_string(int message)
{
	switch (message)
	{
		case PG_CALL_GSM_MSG_SETUP_REQ: return "SETUP_REQ";
		case PG_CALL_GSM_MSG_PROCEEDING_IND: return "PROCEEDING_IND";
		case PG_CALL_GSM_MSG_ALERTING_IND: return "ALERTING_IND";
		case PG_CALL_GSM_MSG_SETUP_CONFIRM: return "SETUP_CONFIRM";
		case PG_CALL_GSM_MSG_RELEASE_REQ: return "RELEASE_REQ";
		case PG_CALL_GSM_MSG_RELEASE_IND: return "RELEASE_IND";
		case PG_CALL_GSM_MSG_SETUP_IND: return "SETUP_IND";
		case PG_CALL_GSM_MSG_INFO_IND: return "INFO_IND";
		case PG_CALL_GSM_MSG_SETUP_RESPONSE: return "SETUP_RESPONSE";
		case PG_CALL_GSM_MSG_HOLD_REQ: return "HOLD_REQ";
		case PG_CALL_GSM_MSG_UNHOLD_REQ: return "UNHOLD_REQ";
		case PG_CALL_GSM_MSG_HOLD_IND: return "HOLD_IND";
		case PG_CALL_GSM_MSG_UNHOLD_IND: return "UNHOLD_IND";
		default: return "unknown";
	}
}
//------------------------------------------------------------------------------
// end of pg_call_gsm_message_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_call_gsm_direction_to_string()
//------------------------------------------------------------------------------
static char *pg_call_gsm_direction_to_string(int direction)
{
	switch (direction)
	{
		case PG_CALL_GSM_DIRECTION_OUTGOING: return "outgoing";
		case PG_CALL_GSM_DIRECTION_INCOMING: return "incoming";
		default: return "unknown";
	}
}
//------------------------------------------------------------------------------
// end of pg_call_gsm_direction_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_is_channel_gsm_has_calls()
//------------------------------------------------------------------------------
static inline int pg_is_channel_gsm_has_calls(struct pg_channel_gsm *ch_gsm)
{
	int res = 0;

	if (ch_gsm) {
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->call_list.first) res = 1;
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_is_channel_gsm_has_calls()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_is_channel_gsm_has_active_calls()
//------------------------------------------------------------------------------
static inline int pg_is_channel_gsm_has_active_calls(struct pg_channel_gsm *ch_gsm)
{
	int res = 0;
	struct pg_call_gsm *call;

	if (ch_gsm) {
		ast_mutex_lock(&ch_gsm->lock);
		AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
		{
			if (call->state != PG_CALL_GSM_STATE_LOCAL_HOLD) {
				res = 1;
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_is_channel_gsm_has_active_calls()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_is_channel_gsm_has_same_requestor()
//------------------------------------------------------------------------------
static inline int pg_is_channel_gsm_has_same_requestor(struct pg_channel_gsm *ch_gsm, char *requestor)
{
	int res = 0;
	struct pg_call_gsm *call;
	struct address addr;

	if (ch_gsm && requestor && strlen(requestor)) {
		address_classify(requestor, &addr);
		res = 1;
		ast_mutex_lock(&ch_gsm->lock);
		AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
		{
			if (call->direction == PG_CALL_GSM_DIRECTION_OUTGOING) {
				if (!is_address_equal(&addr, &call->calling_name)) {
					res = 0;
					break;
				}
			} else {
				if (!is_address_equal(&addr, &call->called_name)) {
					res = 0;
					break;
				}
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_is_channel_gsm_has_same_requestor()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_is_channel_gsm_has_unconfirmed_calls()
//------------------------------------------------------------------------------
static inline int pg_is_channel_gsm_has_unconfirmed_calls(struct pg_channel_gsm *ch_gsm)
{
	int res = 0;
	struct pg_call_gsm *call;

	if (ch_gsm) {
		ast_mutex_lock(&ch_gsm->lock);
		AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
		{
			if (!call->line) {
				res = 1;
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_is_channel_gsm_has_unconfirmed_calls()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_get_calls_count()
//------------------------------------------------------------------------------
static inline int pg_channel_gsm_get_calls_count(struct pg_channel_gsm *ch_gsm)
{
	int count = 0;
	struct pg_call_gsm *call;

	if (ch_gsm) {
		ast_mutex_lock(&ch_gsm->lock);
		AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
		{
			if (call->state != PG_CALL_GSM_STATE_RELEASE_INDICATION) count++;
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return count;
}
//------------------------------------------------------------------------------
// end of pg_channel_gsm_get_calls_count()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_get_new_call()
//------------------------------------------------------------------------------
static inline struct pg_call_gsm *pg_channel_gsm_get_new_call(struct pg_channel_gsm *ch_gsm)
{
	struct pg_call_gsm *call = NULL;
	if (ch_gsm) {
		ast_mutex_lock(&ch_gsm->lock);
		if ((call = ast_calloc(1, sizeof(struct pg_call_gsm)))) {
			call->channel_gsm = ch_gsm;
			call->state = PG_CALL_GSM_STATE_NULL;
			gettimeofday(&call->start_time, NULL);
			call->hash = ast_random();
			AST_LIST_INSERT_TAIL(&ch_gsm->call_list, call, entry);
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return call;
}
//------------------------------------------------------------------------------
// end of pg_channel_gsm_get_new_call()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_put_call()
//------------------------------------------------------------------------------
static inline void pg_channel_gsm_put_call(struct pg_channel_gsm *ch_gsm, struct pg_call_gsm *call)
{
	if (ch_gsm && call) {
		ast_mutex_lock(&ch_gsm->lock);
		AST_LIST_REMOVE(&ch_gsm->call_list, call, entry);
		ast_free(call);
		ast_mutex_unlock(&ch_gsm->lock);
	}
}
//------------------------------------------------------------------------------
// end of pg_channel_gsm_put_call()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_get_call()
//------------------------------------------------------------------------------
static inline struct pg_call_gsm *pg_channel_gsm_get_call(struct pg_channel_gsm *ch_gsm, int line)
{
	struct pg_call_gsm *call = NULL;
	if (ch_gsm) {
		ast_mutex_lock(&ch_gsm->lock);
		AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
			if (call->line == line) break;
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return call;
}
//------------------------------------------------------------------------------
// end of pg_channel_gsm_get_call()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_get_call_by_hash()
//------------------------------------------------------------------------------
static inline struct pg_call_gsm *pg_channel_gsm_get_call_by_hash(struct pg_channel_gsm *ch_gsm, u_int32_t hash)
{
	struct pg_call_gsm *call = NULL;
	if (ch_gsm) {
		ast_mutex_lock(&ch_gsm->lock);
		AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
			if (call->hash == hash) break;
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return call;
}
//------------------------------------------------------------------------------
// end of pg_channel_gsm_get_call_by_hash()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_event_to_char()
//------------------------------------------------------------------------------
static char pg_event_to_char(int event)
{
	switch (event)
	{
		case 0: return '0';
		case 1: return '1';
		case 2: return '2';
		case 3: return '3';
		case 4: return '4';
		case 5: return '5';
		case 6: return '6';
		case 7: return '7';
		case 8: return '8';
		case 9: return '9';
		case 10: return '*';
		case 11: return '#';
		case 12: return 'A';
		case 13: return 'B';
		case 14: return 'C';
		case 15: return 'D';
		case 16: return 'X';	// Hook Flash
		default: return -1;
	}
}
//------------------------------------------------------------------------------
// pg_event_to_char()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_atcommand_insert_spacer()
//------------------------------------------------------------------------------
static int pg_atcommand_insert_spacer(struct pg_channel_gsm* ch_gsm, int timeout)
{
	struct pg_at_cmd *cmd;

	// creating command container
	if (!(cmd = ast_calloc(1, sizeof(struct pg_at_cmd))))
		return -1;
	// set command data
	cmd->id = -1; // spacer
	cmd->timeout = timeout;

	// append to head of queue
	AST_LIST_INSERT_HEAD(&ch_gsm->cmd_queue, cmd, entry);

	return 0;
}
//------------------------------------------------------------------------------
// end of pg_atcommand_insert_spacer()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_atcommand_queue_prepend()
//------------------------------------------------------------------------------
static int pg_atcommand_queue_prepend(struct pg_channel_gsm* ch_gsm,
										int id,
										u_int32_t oper,
										u_int32_t subcmd,
										int timeout,
										int show,
										const char *fmt, ...)
{
	struct pg_at_cmd *cmd;
	struct at_command *at;
	char *opstr;

	va_list vargs;

	// check command id
 	if (id < 0) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": invalid cmd id=[%d]\n", ch_gsm->alias, id);
		return -1;
	}
	// get at command data
	at = NULL;
	if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
		at = get_at_com_by_id(id, sim300_at_com_list, AT_SIM300_MAXNUM);
	else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
		at = get_at_com_by_id(id, sim900_at_com_list, AT_SIM900_MAXNUM);
	else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
		at = get_at_com_by_id(id, m10_at_com_list, AT_M10_MAXNUM);
	if (!at) {
		ast_log(LOG_WARNING, "GSM channel=\"%s\": can't find at commmand id=[%d]\n", ch_gsm->alias, id);
		return -1;
	}
	// check for is one at command operation
	if (!(opstr = get_at_com_oper_by_id(oper))) {
		ast_log(LOG_WARNING, "GSM channel=\"%s\": [%0X] is not known at commmand operation\n", ch_gsm->alias, oper);
		return -1;
	}
	// check for operation is available for this AT command
	if (!(oper & at->operations)) {
		ast_log(LOG_WARNING, "GSM channel=\"%s\": operation [%0X] is not available for \"%s\"\n", ch_gsm->alias, oper, at->name);
		return -1;
	}
	// creating command container
	if (!(cmd = ast_calloc(1, sizeof(struct pg_at_cmd))))
		return -1;
	// set command data
	cmd->timeout = (timeout > 0) ? timeout : PG_DEFAULT_AT_RESPONSE_TIMEOUT;
	cmd->sub_cmd = subcmd;
	cmd->id = id;
	cmd->oper = oper;
	cmd->at = at;
	cmd->show = show;
	if (cmd->timeout >= 60)
		cmd->attempt = 0;
	else
		cmd->attempt = 5;
	// build command
	if (fmt) {
		cmd->cmd_len = 0;
		cmd->cmd_len += sprintf(cmd->cmd_buf + cmd->cmd_len, "%s%s", cmd->at->name, opstr);
		va_start(vargs, fmt);
		cmd->cmd_len += vsprintf(cmd->cmd_buf + cmd->cmd_len, fmt, vargs);
		va_end(vargs);
		cmd->cmd_len += sprintf(cmd->cmd_buf + cmd->cmd_len, "\r");
	} else
		cmd->cmd_len = sprintf(cmd->cmd_buf, "%s%s\r", cmd->at->name, opstr);

	// append to head of queue
	AST_LIST_INSERT_HEAD(&ch_gsm->cmd_queue, cmd, entry);

	return 0;
}
//------------------------------------------------------------------------------
// end of pg_atcommand_queue_prepend()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_atcommand_queue_append()
//------------------------------------------------------------------------------
static int pg_atcommand_queue_append(struct pg_channel_gsm* ch_gsm,
										int id,
										u_int32_t oper,
										uintptr_t subcmd,
										int timeout,
										int show,
										const char *fmt, ...)
{
	struct pg_at_cmd *cmd;
	struct at_command *at;
	char *opstr;

	va_list vargs;

	// check command id
 	if (id < 0) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": invalid cmd id=[%d]\n", ch_gsm->alias, id);
		return -1;
	}
	// get at command data
	at = NULL;
	if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
		at = get_at_com_by_id(id, sim300_at_com_list, AT_SIM300_MAXNUM);
	else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
		at = get_at_com_by_id(id, sim900_at_com_list, AT_SIM900_MAXNUM);
	else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
		at = get_at_com_by_id(id, m10_at_com_list, AT_M10_MAXNUM);
	if (!at) {
		ast_log(LOG_WARNING, "GSM channel=\"%s\": can't find at commmand id=[%d]\n", ch_gsm->alias, id);
		return -1;
	}
	// check for is one at command operation
	if (!(opstr = get_at_com_oper_by_id(oper))) {
		ast_log(LOG_WARNING, "GSM channel=\"%s\": [%0X] is not known at commmand operation\n", ch_gsm->alias, oper);
		return -1;
	}
	// check for operation is available for this AT command
	if (!(oper & at->operations)) {
		ast_log(LOG_WARNING, "GSM channel=\"%s\": operation [%0X] is not available for \"%s\"\n", ch_gsm->alias, oper, at->name);
		return -1;
	}
	// creating command container
	if (!(cmd = ast_calloc(1, sizeof(struct pg_at_cmd))))
		return -1;
	// set command data
	cmd->timeout = (timeout > 0) ? timeout : PG_DEFAULT_AT_RESPONSE_TIMEOUT;
	cmd->sub_cmd = subcmd;
	cmd->id = id;
	cmd->oper = oper;
	cmd->at = at;
	cmd->show = show;
	if (cmd->timeout >= 60)
		cmd->attempt = 0;
	else
		cmd->attempt = 5;
	// build command
	if (fmt) {
		cmd->cmd_len = 0;
		cmd->cmd_len += sprintf(cmd->cmd_buf + cmd->cmd_len, "%s%s", cmd->at->name, opstr);
		va_start(vargs, fmt);
		cmd->cmd_len += vsprintf(cmd->cmd_buf + cmd->cmd_len, fmt, vargs);
		va_end(vargs);
		cmd->cmd_len += sprintf(cmd->cmd_buf + cmd->cmd_len, "\r");
	} else
		cmd->cmd_len = sprintf(cmd->cmd_buf, "%s%s\r", cmd->at->name, opstr);

	// append to tail of queue
	AST_LIST_INSERT_TAIL(&ch_gsm->cmd_queue, cmd, entry);

	return 0;
}
//------------------------------------------------------------------------------
// end of pg_atcommand_queue_append()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_atcommand_trysend()
//------------------------------------------------------------------------------
static int pg_atcommand_trysend(struct pg_channel_gsm* ch_gsm)
{
	struct timeval time_data, timer;
	struct ast_tm *time_ptr, time_buf;
	int res;
	//
	res = 0;
	// check access to queue
	if ((ch_gsm->cmd_done) && ((ch_gsm->at_cmd) || (ch_gsm->at_cmd = AST_LIST_REMOVE_HEAD(&ch_gsm->cmd_queue, entry)))) {
		// check for spacer
		if (ch_gsm->at_cmd->id > -1) {
			if (write(ch_gsm->tty_fd, ch_gsm->at_cmd->cmd_buf, ch_gsm->at_cmd->cmd_len) < 0) {
				if (errno != EAGAIN) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": write: %s\n", ch_gsm->alias, strerror(errno));
					ast_free(ch_gsm->at_cmd);
					ch_gsm->at_cmd = NULL;
					ch_gsm->cmd_done = 1;
					res = -1;
				}
			} else {
				gettimeofday(&time_data, NULL);
				timer.tv_sec = ch_gsm->at_cmd->timeout;
				timer.tv_usec = 0;
				x_timer_set(ch_gsm->at_cmd->timer, timer);
				if (ch_gsm->at_cmd->show) {
					write(ch_gsm->at_pipe[1], ch_gsm->at_cmd->cmd_buf, ch_gsm->at_cmd->cmd_len - 1);
					write(ch_gsm->at_pipe[1], &char_lf, 1);
				}
				if (ch_gsm->debug.receiver) {
					ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
					if (ch_gsm->debug.receiver_debug_fp) {
						if ((time_ptr = ast_localtime(&time_data, &time_buf, NULL)))
							fprintf(ch_gsm->debug.receiver_debug_fp, "\n[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT send [%.*s]\n",
													time_ptr->tm_year + 1900,
													time_ptr->tm_mon+1,
													time_ptr->tm_mday,
													time_ptr->tm_hour,
													time_ptr->tm_min,
													time_ptr->tm_sec,
													time_data.tv_usec,
													ch_gsm->at_cmd->cmd_len - 1,
													ch_gsm->at_cmd->cmd_buf);
						else
							fprintf(ch_gsm->debug.receiver_debug_fp, "\n[%ld.%06ld] AT send [%.*s]\n",
													time_data.tv_sec,
													time_data.tv_usec,
													ch_gsm->at_cmd->cmd_len - 1,
													ch_gsm->at_cmd->cmd_buf);
						fflush(ch_gsm->debug.receiver_debug_fp);
						fclose(ch_gsm->debug.receiver_debug_fp);
						ch_gsm->debug.receiver_debug_fp = NULL;
					}
				}
				if (ch_gsm->debug.at) {
					ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
					if (ch_gsm->debug.at_debug_fp) {
						if ((time_ptr = ast_localtime(&time_data, &time_buf, NULL)))
							fprintf(ch_gsm->debug.at_debug_fp, "[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT send [%.*s]\n",
													time_ptr->tm_year + 1900,
													time_ptr->tm_mon+1,
													time_ptr->tm_mday,
													time_ptr->tm_hour,
													time_ptr->tm_min,
													time_ptr->tm_sec,
													time_data.tv_usec,
													ch_gsm->at_cmd->cmd_len - 1,
													ch_gsm->at_cmd->cmd_buf);
						else
							fprintf(ch_gsm->debug.at_debug_fp, "[%ld.%06ld] AT send [%.*s]\n",
													time_data.tv_sec,
													time_data.tv_usec,
													ch_gsm->at_cmd->cmd_len - 1,
													ch_gsm->at_cmd->cmd_buf);
						fflush(ch_gsm->debug.at_debug_fp);
						fclose(ch_gsm->debug.at_debug_fp);
						ch_gsm->debug.at_debug_fp = NULL;
					}
				}
			}
			ch_gsm->cmd_done = 0;
		} else {
			// spacer
			gettimeofday(&time_data, NULL);
			timer.tv_sec = ch_gsm->at_cmd->timeout;
			timer.tv_usec = 0;
			x_timer_set(ch_gsm->at_cmd->timer, timer);
			ch_gsm->cmd_done = 0;
		}
	} // end od data to send is ready
	return res;
}
//------------------------------------------------------------------------------
// end of pg_atcommand_trysend()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_atcommand_queue_flush()
//------------------------------------------------------------------------------
static inline void pg_atcommand_queue_flush(struct pg_channel_gsm* ch_gsm)
{
	struct pg_at_cmd *at_cmd;
	
	while ((at_cmd = AST_LIST_REMOVE_HEAD(&ch_gsm->cmd_queue, entry)))
		ast_free(at_cmd);
}
//------------------------------------------------------------------------------
// end of pg_atcommand_queue_flush()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_call_incoming()
//------------------------------------------------------------------------------
static int pg_channel_gsm_call_incoming(struct pg_channel_gsm *ch_gsm, struct pg_call_gsm* call)
{
	int res;
	u_int32_t ch_id;
	char calling[MAX_ADDRESS_LENGTH];
	char called[MAX_ADDRESS_LENGTH]; 
	struct ast_channel *ast_ch;
	struct pg_vinetic *vin;
	struct pg_channel_rtp *rtp;

	if (ch_gsm->channel_rtp) {
		rtp = ch_gsm->channel_rtp;
		vin = rtp->vinetic;
	} else if ((!(vin = pg_get_vinetic_from_board(ch_gsm->board, ch_gsm->position_on_board/4))) ||
			(!pg_is_vinetic_run(vin)) ||
			(!(rtp = pg_get_channel_rtp(vin)))) {
		ast_log(LOG_WARNING, "pg_get_channel_rtp() failed\n");
		return -1;
	}

	if (!ch_gsm->channel_rtp_usage) {

		rtp->loc_ssrc = ast_random();
		rtp->rem_ssrc = ast_random();
		rtp->loc_timestamp = ast_random();
		rtp->loc_timestamp |= 160;
		rtp->loc_seq_num = ast_random() & 0xffff;

		rtp->recv_ssrc = 0;
		rtp->recv_timestamp = 0;
		rtp->recv_seq_num = 0;

		rtp->event_is_now_recv = 0;

#if ASTERISK_VERSION_NUM >= 100000
		if (!ast_best_codec(vin->capabilities, &rtp->format)) {
			ast_log(LOG_WARNING, "ast_best_codec() failed\n");
			pg_put_channel_rtp(rtp);
			return -1;
		}
#else
		if (!(rtp->format = ast_best_codec(vin->capabilities))) {
			ast_log(LOG_WARNING, "ast_best_codec() failed\n");
			pg_put_channel_rtp(rtp);
			return -1;
		}
#endif

#if ASTERISK_VERSION_NUM >= 100000
		switch (rtp->format.id)
#else
		switch (rtp->format)
#endif
		{
			case AST_FORMAT_G723_1:
				/*! G.723.1 compression */
				rtp->payload_type = RTP_PT_G723;
				rtp->encoder_packet_time = VIN_PTE_30;
				rtp->encoder_algorithm = VIN_ENC_G7231_5_3;
				break;
			case AST_FORMAT_GSM:
				/*! GSM compression */
				rtp->payload_type = RTP_PT_GSM;
				break;
			case AST_FORMAT_ULAW:
				/*! Raw mu-law data (G.711) */
				rtp->payload_type = RTP_PT_PCMU;
				rtp->encoder_packet_time = VIN_PTE_20;
				rtp->encoder_algorithm = VIN_ENC_G711_MLAW;
				break;
			case AST_FORMAT_ALAW:
				/*! Raw A-law data (G.711) */
				rtp->payload_type = RTP_PT_PCMA;
				rtp->encoder_packet_time = VIN_PTE_20;
				rtp->encoder_algorithm = VIN_ENC_G711_ALAW;
				break;
			case AST_FORMAT_G726_AAL2:
				/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_ADPCM:
				/*! ADPCM (IMA) */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_SLINEAR:
				/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_LPC10:
				/*! LPC10, 180 samples/frame */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_G729A:
				/*! G.729A audio */
				rtp->payload_type = RTP_PT_G729;
				rtp->encoder_packet_time = VIN_PTE_20;
				rtp->encoder_algorithm = VIN_ENC_G729AB_8;
				break;
			case AST_FORMAT_SPEEX:
				/*! SpeeX Free Compression */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_ILBC:
				/*! iLBC Free Compression */
				rtp->payload_type = RTP_PT_DYNAMIC;
				rtp->encoder_algorithm = VIN_ENC_ILBC_15_2;
				break;
			case AST_FORMAT_G726:
				/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
				rtp->payload_type = 2; // from vinetic defaults
				rtp->encoder_packet_time = VIN_PTE_20;
				rtp->encoder_algorithm = VIN_ENC_G726_32;
				break;
			case AST_FORMAT_G722:
				/*! G.722 */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_SLINEAR16:
				/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
				rtp->payload_type = -1;
				break;
			default:
				rtp->payload_type = -1;
#if ASTERISK_VERSION_NUM >= 100000
				ast_log(LOG_ERROR, "unknown asterisk frame format=%s\n", ast_getformatname(&rtp->format));
#else
				ast_log(LOG_ERROR, "unknown asterisk frame format=%s\n", ast_getformatname(rtp->format));
#endif
				break;
		}
		if (rtp->payload_type < 0) {
#if ASTERISK_VERSION_NUM >= 100000
			ast_log(LOG_WARNING, "can't assign frame format=%s with RTP payload type\n", ast_getformatname(&rtp->format));
#else
			ast_log(LOG_WARNING, "can't assign frame format=%s with RTP payload type\n", ast_getformatname(rtp->format));
#endif
			pg_put_channel_rtp(rtp);
			return -1;
		}
		rtp->payload_type &= 0x7f;
		rtp->event_payload_type = 107;

		// set vinetic audio path
		ast_mutex_lock(&vin->lock);
		// unblock vinetic
		if ((res = vin_reset_status(&vin->context)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_status(): line %d\n", vin->name, vin->context.errorline);
			goto pg_channel_gsm_call_incoming_end;
		}
		// ALI module
		if (ch_gsm->vinetic_alm_slot >= 0) {
			if (!is_vin_ali_enabled(vin->context)) {
				// enable vinetic ALI module
				if ((res = vin_ali_enable(vin->context)) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_enable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_enable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_channel_gsm_call_incoming_end;
				}
			}
			// enable vinetic ALI channel
			vin_ali_channel_set_input_sig_b(vin->context, ch_gsm->vinetic_alm_slot, 1, rtp->position_on_vinetic);
			vin_ali_channel_set_gainr(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.gainr);
			vin_ali_channel_set_gainx(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.gainx);
			if ((res = vin_ali_channel_enable(vin->context, ch_gsm->vinetic_alm_slot)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_channel_enable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_channel_gsm_call_incoming_end;
			}
			if (ch_gsm->config.ali_nelec == VIN_EN) {
				// enable vinetic ALI Near End LEC
				vin_ali_near_end_lec_set_dtm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_tm);
				vin_ali_near_end_lec_set_oldc(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_oldc);
				vin_ali_near_end_lec_set_as(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_as);
				vin_ali_near_end_lec_set_nlp(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlp);
				vin_ali_near_end_lec_set_nlpm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlpm);
				if ((res = vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot)) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_channel_gsm_call_incoming_end;
				}
			} else {
				// disable vinetic ALI Near End LEC
				if ((res = vin_ali_near_end_lec_disable(vin->context, ch_gsm->vinetic_alm_slot)) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_disable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_disable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_channel_gsm_call_incoming_end;
				}
			}
			// set ALI channel operation mode ACTIVE_HIGH_VBATH
			if ((res = vin_set_opmode(&vin->context, ch_gsm->vinetic_alm_slot, VIN_OP_MODE_ACTIVE_HIGH_VBATH)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_opmode(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_opmode(): line %d\n", vin->name, vin->context.errorline);
				goto pg_channel_gsm_call_incoming_end;
			}
		}
		// signaling module
		if (!is_vin_signaling_enabled(vin->context)) {
			// enable coder module
			if ((res = vin_signaling_enable(vin->context)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_enable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_enable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_channel_gsm_call_incoming_end;
			}
		}
		// set signaling channel
		vin_signaling_channel_set_input_ali(vin->context, rtp->position_on_vinetic, 1, ch_gsm->vinetic_alm_slot);
		vin_signaling_channel_set_input_coder(vin->context, rtp->position_on_vinetic, 2, rtp->position_on_vinetic);
		if ((res = vin_signaling_channel_enable(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_enable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_channel_gsm_call_incoming_end;
		}
		// set DTMF receiver
		vin_dtmf_receiver_set_as(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_dtmf_receiver_set_is(vin->context, rtp->position_on_vinetic, VIN_IS_SIGINA);
		vin_dtmf_receiver_set_et(vin->context, rtp->position_on_vinetic, VIN_ACTIVE);
		if ((res = vin_dtmf_receiver_enable(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_dtmf_receiver_enable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_dtmf_receiver_enable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_channel_gsm_call_incoming_end;
		}
		// set signalling channel RTP
		vin_signaling_channel_config_rtp_set_ssrc(vin->context, rtp->position_on_vinetic, rtp->rem_ssrc);
		vin_signaling_channel_config_rtp_set_evt_pt(vin->context, rtp->position_on_vinetic, rtp->event_payload_type);
		if ((res = vin_signaling_channel_config_rtp(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_config_rtp(): line %d\n", vin->name, vin->context.errorline);
			goto pg_channel_gsm_call_incoming_end;
		}
		// coder module
		if (!is_vin_coder_enabled(vin->context)) {
			// enable coder module
			if ((res = vin_coder_enable(vin->context)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_enable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_enable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_channel_gsm_call_incoming_end;
			}
			// set coder configuration RTP
			vin_coder_config_rtp_set_timestamp(vin->context, 0);
			if ((res = vin_coder_config_rtp(vin->context)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_config_rtp(): line %d\n", vin->name, vin->context.errorline);
				goto pg_channel_gsm_call_incoming_end;
			}
		}
		// set coder channel RTP
		vin_coder_channel_config_rtp_set_ssrc(vin->context, rtp->position_on_vinetic, rtp->rem_ssrc);
		vin_coder_channel_config_rtp_set_seq_nr(vin->context, rtp->position_on_vinetic, 0);
		if ((res = vin_coder_channel_config_rtp(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_config_rtp(): line %d\n", vin->name, vin->context.errorline);
			goto pg_channel_gsm_call_incoming_end;
		}
		// set coder channel speech compression
		vin_coder_channel_set_ns(vin->context, rtp->position_on_vinetic, VIN_NS_INACTIVE);
		vin_coder_channel_set_hp(vin->context, rtp->position_on_vinetic, VIN_INACTIVE);
		vin_coder_channel_set_pf(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_cng(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_bfi(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_dec(vin->context, rtp->position_on_vinetic, VIN_ACTIVE);
		vin_coder_channel_set_im(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_pst(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_sic(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_pte(vin->context, rtp->position_on_vinetic, rtp->encoder_packet_time);
		vin_coder_channel_set_enc(vin->context, rtp->position_on_vinetic, rtp->encoder_algorithm);
		vin_coder_channel_set_gain1(vin->context, rtp->position_on_vinetic, ch_gsm->config.gain1);
		vin_coder_channel_set_gain2(vin->context, rtp->position_on_vinetic, ch_gsm->config.gain2);
		vin_coder_channel_set_input_sig_a(vin->context, rtp->position_on_vinetic, 1, rtp->position_on_vinetic);
		if ((res = vin_coder_channel_enable(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_enable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_channel_gsm_call_incoming_end;
		}
pg_channel_gsm_call_incoming_end:
		if (res < 0) {
			vin->state = PG_VINETIC_STATE_IDLE;
			ast_mutex_unlock(&vin->lock);
			pg_put_channel_rtp(rtp);
			return -1;
		} else {
			ast_mutex_unlock(&vin->lock);
		}
	}
	ch_gsm->channel_rtp_usage++;
	ch_gsm->channel_rtp = rtp;

	// prevent deadlock while asterisk channel is allocating
	ast_mutex_unlock(&ch_gsm->lock);
	// increment channel ID
	ast_mutex_lock(&pg_lock);
	ch_id = channel_id++;
	ast_mutex_unlock(&pg_lock);
	// allocation channel in pbx spool
	sprintf(calling, "%s%s", (call->calling_name.type.full == 145)?("+"):(""), call->calling_name.value);
	sprintf(called, "%s%s", (call->called_name.type.full == 145)?("+"):(""), call->called_name.value);
#if ASTERISK_VERSION_NUM < 10800
	ast_ch = ast_channel_alloc(1,						/* int needqueue */
								AST_STATE_RING,			/* int state */
								calling,				/* const char *cid_num */
								calling,				/* const char *cid_name */
								NULL,					/* const char *acctcode */
								called,					/* const char *exten */
								ch_gsm->config.gsm_call_context,	/* const char *context */
								0,						/* const int amaflag */
								"PGGSM/%s-%08x",		/* const char *name_fmt, ... */
								ch_gsm->alias, ch_id);
#else
	ast_ch = ast_channel_alloc(1,						/* int needqueue */
								AST_STATE_RING,			/* int state */
								calling,				/* const char *cid_num */
								calling,				/* const char *cid_name */
								NULL,					/* const char *acctcode */
								called,					/* const char *exten */
								ch_gsm->config.gsm_call_context,	/* const char *context */
								NULL,					/* const char *linkedid */
								0,						/* int amaflag */
								"PGGSM/%s-%08x",		/* const char *name_fmt, ... */
								ch_gsm->alias, ch_id);
#endif
	ast_mutex_lock(&ch_gsm->lock);

	// fail allocation channel
	if (!ast_ch) {
		ast_log(LOG_ERROR, "ast_channel_alloc() failed\n");
		return -1;
	}

	// init asterisk channel tag's
#if ASTERISK_VERSION_NUM >= 100000
	ast_format_cap_copy(ast_ch->nativeformats, vin->capabilities);
	ast_format_copy(&ast_ch->rawreadformat, &rtp->format);
	ast_format_copy(&ast_ch->rawwriteformat, &rtp->format);
	ast_format_copy(&ast_ch->writeformat, &rtp->format);
	ast_format_copy(&ast_ch->readformat, &rtp->format);
// 	ast_verb(3, "GSM channel=\"%s\": selected codec \"%s\"\n", ch_gsm->alias, ast_getformatname(&rtp->format));
#else
	ast_ch->nativeformats = vin->capabilities;
	ast_ch->rawreadformat = rtp->format;
	ast_ch->rawwriteformat = rtp->format;
	ast_ch->writeformat = rtp->format;
	ast_ch->readformat = rtp->format;
// 	ast_verb(3, "GSM channel=\"%s\": selected codec \"%s\"\n", ch_gsm->alias, ast_getformatname(rtp->format));
#endif
	ast_string_field_set(ast_ch, language, ch_gsm->config.language);

	ast_ch->tech = &pg_gsm_tech;
	ast_ch->tech_pvt = call;
	call->owner = ast_ch;
	call->channel_rtp = rtp;

	ast_verb(2, "GSM channel=\"%s\": incoming call \"%s\" -> \"%s\"\n", ch_gsm->alias, calling, called);

	return 0;
}
//------------------------------------------------------------------------------
// end of pg_channel_gsm_call_incoming()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_call_gsm_sm()
//------------------------------------------------------------------------------
static int pg_call_gsm_sm(struct pg_call_gsm* call, int message, int cause)
{
	struct timeval curr_tv;
	struct pg_channel_gsm *ch_gsm = call->channel_gsm;
	int res = -1;
	
	ast_debug(3, "GSM channel=\"%s\": call line=%d, state=%s, message=%s\n",
				ch_gsm->alias, call->line,
				pg_call_gsm_state_to_string(call->state),
				pg_call_gsm_message_to_string(message));

	switch (message)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_SETUP_REQ:
			if (call->state == PG_CALL_GSM_STATE_NULL) {
				// Set GSM module audio channel
				if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
					pg_atcommand_queue_append(ch_gsm, AT_SIM300_CHFA, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 0);
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
					pg_atcommand_queue_append(ch_gsm, AT_SIM900_CHFA, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 0);
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
					pg_atcommand_queue_append(ch_gsm, AT_M10_QAUDCH, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 1);
				// Send ATD
				pg_atcommand_insert_spacer(ch_gsm, 1);
				if (pg_atcommand_queue_prepend(ch_gsm, AT_D, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "%s%s;", (call->called_name.type.full == 145)?("+"):(""), call->called_name.value) < 0) {
					while (ast_channel_trylock(call->owner))
					{
						ast_mutex_unlock(&ch_gsm->lock);
						usleep(1000);
						ast_mutex_lock(&ch_gsm->lock);
					}
					ast_mutex_unlock(&ch_gsm->lock);
					call->owner->hangupcause = cause;
					ast_queue_control(call->owner, AST_CONTROL_CONGESTION);
					ast_mutex_lock(&ch_gsm->lock);
					ast_channel_unlock(call->owner);
					break;
				}
				call->state = PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING;
				ast_debug(3, "GSM channel=\"%s\": call line=%d, state=%s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_state_to_string(call->state));
				res = 0;
			} else
				ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message %s unexpected in state %s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_SETUP_IND:
			if (call->state == PG_CALL_GSM_STATE_NULL) {
				call->state = PG_CALL_GSM_STATE_OVERLAP_RECEIVING;
				ast_debug(3, "GSM channel=\"%s\": call line=%d, state=%s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_state_to_string(call->state));
				res = 0;
			} else
				ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message %s unexpected in state %s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_RELEASE_REQ:
			call->hangup_side = PG_CALL_GSM_HANGUP_SIDE_NETWORK;
			if (call->state != PG_CALL_GSM_STATE_RELEASE_INDICATION) {
				if (pg_channel_gsm_get_calls_count(ch_gsm) > 1) {
					pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1%d", call->line);
				} else {
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "0");
					else
						pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
				}
				call->hangup_side = PG_CALL_GSM_HANGUP_SIDE_CORE;
			}
			if (call->answer_time.tv_sec) {
				gettimeofday(&curr_tv, NULL);
				if (call->direction == PG_CALL_GSM_DIRECTION_OUTGOING) {
					ch_gsm->last_call_time_outgoing = curr_tv.tv_sec - call->answer_time.tv_sec;
					ch_gsm->total_call_time_outgoing += ch_gsm->last_call_time_outgoing;
				} else if (call->direction == PG_CALL_GSM_DIRECTION_INCOMING) {
					ch_gsm->last_call_time_incoming = curr_tv.tv_sec - call->answer_time.tv_sec;
					ch_gsm->total_call_time_incoming += ch_gsm->last_call_time_incoming;
				}
			}

			pg_cdr_table_insert_record(ch_gsm->device, ch_gsm->imsi, call, &pg_cdr_db_lock);

			pg_channel_gsm_put_call(ch_gsm, call);
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_PROCEEDING_IND:
			while (ast_channel_trylock(call->owner))
			{
				ast_mutex_unlock(&ch_gsm->lock);
				usleep(1000);
				ast_mutex_lock(&ch_gsm->lock);
			}

			ast_mutex_unlock(&ch_gsm->lock);
			if (ch_gsm->config.progress == PG_GSM_CALL_PROGRESS_TYPE_PROCEEDING)
				ast_queue_control(call->owner, AST_CONTROL_PROGRESS);
			else
				ast_queue_control(call->owner, AST_CONTROL_PROCEEDING);
			ast_mutex_lock(&ch_gsm->lock);

			ast_channel_unlock(call->owner);
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_ALERTING_IND:
			while (ast_channel_trylock(call->owner))
			{
				ast_mutex_unlock(&ch_gsm->lock);
				usleep(1000);
				ast_mutex_lock(&ch_gsm->lock);
			}

			ast_mutex_unlock(&ch_gsm->lock);
			if (ch_gsm->config.progress == PG_GSM_CALL_PROGRESS_TYPE_ALERTING)
				ast_queue_control(call->owner, AST_CONTROL_PROGRESS);
			else {
				ast_queue_control(call->owner, AST_CONTROL_RINGING);
				if (call->owner->_state != AST_STATE_UP)
					ast_setstate(call->owner, AST_STATE_RINGING);
			}
			ast_mutex_lock(&ch_gsm->lock);

			ast_channel_unlock(call->owner);
			call->state = PG_CALL_GSM_STATE_CALL_DELIVERED;
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_SETUP_CONFIRM:
			while (ast_channel_trylock(call->owner))
			{
				ast_mutex_unlock(&ch_gsm->lock);
				usleep(1000);
				ast_mutex_lock(&ch_gsm->lock);
			}

			ast_mutex_unlock(&ch_gsm->lock);
			ast_queue_control(call->owner, AST_CONTROL_ANSWER);
			ast_mutex_lock(&ch_gsm->lock);

			ast_channel_unlock(call->owner);
			gettimeofday(&call->answer_time, NULL);
			call->state = PG_CALL_GSM_STATE_ACTIVE;
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_INFO_IND:
			if (call->state == PG_CALL_GSM_STATE_OVERLAP_RECEIVING) {
				// check incoming type
				if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_SPEC) {
					// route incoming call to specified extension
					address_classify(ch_gsm->config.gsm_call_extension, &call->called_name);
				} else if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_DTMF) {
					// route incoming call to default extension
					address_classify("s", &call->called_name);
				} else if(ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_DYN) {
					// incoming call dynamic routed
					if (!pg_dcr_table_get_match_record(ch_gsm->imsi, &call->calling_name, ch_gsm->config.dcrttl, &call->called_name, &ch_gsm->lock))
						address_classify("s", &call->called_name);
				} else if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_DENY) {
					// incoming call denied
					ast_verb(2, "GSM channel=\"%s\": call line=%d: call from \"%s%s\" denied\n", ch_gsm->alias, call->line, (call->calling_name.type.full == 145)?("+"):(""), call->calling_name.value);
					// hangup
					if (pg_channel_gsm_get_calls_count(ch_gsm) > 1) {
						pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1%d", call->line);
					} else {
						if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "0");
						else
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					}
					pg_channel_gsm_put_call(ch_gsm, call);
					res = 0;
					break;
				} else {
					// incoming call unknown type
					ast_verb(2, "GSM channel=\"%s\": call line=%d: unknown type of incoming call\n", ch_gsm->alias, call->line);
					// hangup
					if (pg_channel_gsm_get_calls_count(ch_gsm) > 1) {
						pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1%d", call->line);
					} else {
						if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "0");
						else
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					}
					pg_channel_gsm_put_call(ch_gsm, call);
					break;
				}
				// alloc channel in pbx spool
				if (pg_channel_gsm_call_incoming(ch_gsm, call) < 0) {
					// error - hangup
					if (pg_channel_gsm_get_calls_count(ch_gsm) > 1) {
						pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1%d", call->line);
					} else {
						if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "0");
						else
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					}
					pg_channel_gsm_put_call(ch_gsm, call);
					break;
				}
				// start pbx
				if (ast_pbx_start(call->owner)) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": call line=%d: unable to start pbx on incoming call\n", ch_gsm->alias, call->line);
					ast_hangup(call->owner);
					break;
				}
				// prevent leak real audio channel
				if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
					pg_atcommand_queue_append(ch_gsm, AT_SIM300_CHFA, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 0);
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
					pg_atcommand_queue_append(ch_gsm, AT_SIM900_CHFA, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 0);
				else if (ch_gsm->gsm_module_type== POLYGATOR_MODULE_TYPE_M10)
					pg_atcommand_queue_append(ch_gsm, AT_M10_QAUDCH, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 1);
				// set new state
				call->state = PG_CALL_GSM_STATE_CALL_RECEIVED;
				ast_debug(3, "GSM channel=\"%s\": call line=%d, state=%s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_state_to_string(call->state));
				res = 0;
			} else
				ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message %s unexpected in state %s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_SETUP_RESPONSE:
			if (call->state == PG_CALL_GSM_STATE_CALL_RECEIVED) {
				// send ANSWER command to GSM module
				if (pg_channel_gsm_get_calls_count(ch_gsm) == 1)
					pg_atcommand_queue_prepend(ch_gsm, AT_A, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
				// set new state
				ast_setstate(call->owner, AST_STATE_UP);
				gettimeofday(&call->answer_time, NULL);
				call->state = PG_CALL_GSM_STATE_ACTIVE;
				ast_debug(3, "GSM channel=\"%s\": call line=%d, state=%s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_state_to_string(call->state));
				res = 0;
			} else
				ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message %s unexpected in state %s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_HOLD_REQ:
			if (call->state == PG_CALL_GSM_STATE_ACTIVE) {
				// send HOLD command to GSM module
				pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "2");
				// set new state
				call->state = PG_CALL_GSM_STATE_LOCAL_HOLD;
				ast_debug(3, "GSM channel=\"%s\": call line=%d, state=%s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_state_to_string(call->state));
				res = 0;
			} else if (call->state != PG_CALL_GSM_STATE_REMOTE_HOLD)
				ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message %s unexpected in state %s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_UNHOLD_REQ:
			if (call->state == PG_CALL_GSM_STATE_LOCAL_HOLD) {
				// set new state
				call->state = PG_CALL_GSM_STATE_ACTIVE;
				ast_debug(3, "GSM channel=\"%s\": call line=%d, state=%s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_state_to_string(call->state));
				res = 0;
			} else if (call->state != PG_CALL_GSM_STATE_REMOTE_HOLD)
				ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message %s unexpected in state %s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_HOLD_IND:
			if (call->state == PG_CALL_GSM_STATE_ACTIVE) {
				while (ast_channel_trylock(call->owner))
				{
					ast_mutex_unlock(&ch_gsm->lock);
					usleep(1000);
					ast_mutex_lock(&ch_gsm->lock);
				}

				ast_mutex_unlock(&ch_gsm->lock);
				ast_queue_control(call->owner, AST_CONTROL_HOLD);
				ast_mutex_lock(&ch_gsm->lock);

				ast_channel_unlock(call->owner);
				call->state = PG_CALL_GSM_STATE_REMOTE_HOLD;
				res = 0;
			} else if (call->state != PG_CALL_GSM_STATE_LOCAL_HOLD)
				ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message %s unexpected in state %s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_UNHOLD_IND:
			if (call->state == PG_CALL_GSM_STATE_REMOTE_HOLD) {
				while (ast_channel_trylock(call->owner))
				{
					ast_mutex_unlock(&ch_gsm->lock);
					usleep(1000);
					ast_mutex_lock(&ch_gsm->lock);
				}

				ast_mutex_unlock(&ch_gsm->lock);
				ast_queue_control(call->owner, AST_CONTROL_UNHOLD);
				ast_mutex_lock(&ch_gsm->lock);

				ast_channel_unlock(call->owner);
				call->state = PG_CALL_GSM_STATE_ACTIVE;
				res = 0;
			} else if (call->state != PG_CALL_GSM_STATE_LOCAL_HOLD)
				ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message %s unexpected in state %s\n",
									ch_gsm->alias, call->line,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case PG_CALL_GSM_MSG_RELEASE_IND:
			while (ast_channel_trylock(call->owner))
			{
				ast_mutex_unlock(&ch_gsm->lock);
				usleep(1000);
				ast_mutex_lock(&ch_gsm->lock);
			}

			ast_mutex_unlock(&ch_gsm->lock);
			call->owner->hangupcause = cause;
			ast_queue_control(call->owner, AST_CONTROL_HANGUP);
			ast_mutex_lock(&ch_gsm->lock);

			ast_channel_unlock(call->owner);
			call->state = PG_CALL_GSM_STATE_RELEASE_INDICATION;
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_log(LOG_WARNING, "GSM channel=\"%s\": call line=%d, message=%d %s unrecognized in state %s\n",
									ch_gsm->alias, call->line, message,
									pg_call_gsm_message_to_string(message),
									pg_call_gsm_state_to_string(call->state));
			break;
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_call_gsm_sm()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_workthread()
//------------------------------------------------------------------------------
static void *pg_channel_gsm_workthread(void *data)
{
#ifdef HAVE_ASTERISK_SELECT_H
	ast_fdset rfds;
#else
	fd_set rfds;
#endif
	u_int32_t ch_id;
	int res;
	int r_len;
	char r_char;
	char *r_cptr;
	char r_buf[2048];
	int r_buf_len;
	size_t r_buf_valid;
	size_t r_buf_active;
	struct timeval timeout;
	struct ast_tm *tm_ptr, tm_buf;
	struct timeval curr_tv;
	struct timezone curr_tz;

	struct termios termios;
	speed_t baudrate;

	size_t conference;
	size_t count;

	unsigned int cause_select;
	unsigned int cause_value;

	char *ip;
	int ilen;
	char *op;
	int olen;

	char *str0, *str1;
	sqlite3_stmt *sql0, *sql1;
	int row;

	union {
		// exec
		struct at_gen_clcc_exec *clcc_ex;
		struct at_gen_csq_exec *csq_ex;
		struct at_gen_cnum_exec *cnum_ex;
		// test
		// read
		struct at_gen_clir_read *clir_rd;
		struct at_gen_cops_read *cops_rd;
		struct at_gen_creg_read *creg_rd;
		struct at_gen_clvl_read *clvl_rd;
		struct at_gen_csca_read *csca_rd;
		struct at_sim300_cmic_read *sim300_cmic_rd;
		struct at_sim900_cmic_read *sim900_cmic_rd;
		struct at_m10_qmic_read *m10_qmic_rd;
		struct at_sim300_csmins_read *sim300_csmins_rd;
		struct at_sim900_csmins_read *sim900_csmins_rd;
		struct at_m10_qsimstat_read *m10_qsimstat_rd;
		// write
		struct at_gen_ccwa_write *ccwa_wr;
		struct at_gen_cusd_write *cusd_wr;
		struct at_gen_cmgr_write *cmgr_wr;
		// unsolicited
		struct at_gen_clip_unsol *clip_un;
	} parser_ptrs;

	char ts0[32], ts1[32];
	unsigned int tu0, tu1, tu2, tu3, tu4, tu5;
	char tmpbuf[512];
	struct pdu *pdu, *curr;

	struct pg_call_gsm *call;

	struct ast_channel *ast_ch_tmp = NULL;

	struct pg_channel_gsm *ch_gsm = (struct pg_channel_gsm *)data;

	ast_debug(4, "GSM channel=\"%s\": thread start\n", ch_gsm->alias);
	ast_verbose("Polygator: GSM channel=\"%s\" enabled\n", ch_gsm->alias);

	ast_mutex_lock(&ch_gsm->lock);
	res = ch_gsm->power_sequence_number;
	ast_mutex_unlock(&ch_gsm->lock);

	if (res > 0)
		usleep(799999 * res);

	ast_mutex_lock(&ch_gsm->lock);
	ch_gsm->power_sequence_number = -1;

	// check GSM module type
	if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_UNKNOWN) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": unknown GSM module type\n", ch_gsm->alias);
		ast_mutex_unlock(&ch_gsm->lock);
		goto pg_channel_gsm_workthread_end;
	}

	// enable power suply
	if (pg_channel_gsm_power_set(ch_gsm, 1)) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": can't set GSM power suply to on: %s\n", ch_gsm->alias, strerror(errno));
		ast_mutex_unlock(&ch_gsm->lock);
		goto pg_channel_gsm_workthread_end;
	}
	ch_gsm->flags.power = 1;
	ast_mutex_unlock(&ch_gsm->lock);
	sleep(3);
	ast_mutex_lock(&ch_gsm->lock);

	// open TTY device
	if ((ch_gsm->tty_fd = open(ch_gsm->tty_path, O_RDWR | O_NONBLOCK)) < 0) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": can't open \"%s\": %s\n", ch_gsm->alias, ch_gsm->tty_path, strerror(errno));
		ast_mutex_unlock(&ch_gsm->lock);
		goto pg_channel_gsm_workthread_end;
	}
	// set serial port to main channel
	if (pg_channel_gsm_serial_set(ch_gsm, 0) < 0) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": can't switch serial port to main channel: %s\n", ch_gsm->alias, strerror(errno));
		ast_mutex_unlock(&ch_gsm->lock);
		goto pg_channel_gsm_workthread_end;
	}

	// init command queue
	ch_gsm->cmd_done = 1;

	ch_gsm->pdu_cmt_wait = 0;
	ch_gsm->pdu_cds_wait = 0;
	ch_gsm->pdu_send_id = 0;

	ch_gsm->init.ready = 0;
	ch_gsm->init.clip = 1;
	ch_gsm->init.chfa = 1;
	ch_gsm->init.colp = 0;
	ch_gsm->init.clvl = 1;
	ch_gsm->init.cmic = 1;
	ch_gsm->init.cscs = 1;
	ch_gsm->init.cmee = 1;
	ch_gsm->init.ceer = 1;
	ch_gsm->init.cclk = 1;
	ch_gsm->init.creg = 0;
	ch_gsm->init.cmgf = 1;
	ch_gsm->init.echo = 1;
	ch_gsm->init.cnmi = 1;
	ch_gsm->init.fallback = 0;

	// startup flags
	ch_gsm->flags.shutdown = 0;
	ch_gsm->flags.restart = 0;
	ch_gsm->flags.init = 1;
	ch_gsm->flags.sim_change = 0;
	ch_gsm->flags.sim_test = 0;
	ch_gsm->flags.balance_req = 1;
	ch_gsm->flags.pin_required = 0;
	ch_gsm->flags.puk_required = 0;
	ch_gsm->flags.pin_accepted = 0;
	ch_gsm->flags.sms_table_needed = 1;
	ch_gsm->flags.dcr_table_needed = 1;
	ch_gsm->flags.main_tty = 1;

	ch_gsm->reg_try_count = ch_gsm->config.reg_try_count;

	ch_gsm->sms_ref = ast_random();

	// reset channel phone number
	ast_copy_string(ch_gsm->subscriber_number.value, "unknown", MAX_ADDRESS_LENGTH);
	ch_gsm->subscriber_number.length = strlen(ch_gsm->subscriber_number.value);
	ch_gsm->subscriber_number.type.bits.reserved = 1;
	ch_gsm->subscriber_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
	ch_gsm->subscriber_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;

	// set initial baudrate
	ch_gsm->baudrate = ch_gsm->config.baudrate;

	// start receiver debug session
	if (ch_gsm->debug.receiver) {
		ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
		if (ch_gsm->debug.receiver_debug_fp) {
			fprintf(ch_gsm->debug.receiver_debug_fp, "\nchannel now enabled\n");
			fflush(ch_gsm->debug.receiver_debug_fp);
			fclose(ch_gsm->debug.receiver_debug_fp);
			ch_gsm->debug.receiver_debug_fp = NULL;
		}
	}
	// start at debug session
	if (ch_gsm->debug.at) {
		ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
		if (ch_gsm->debug.at_debug_fp) {
			fprintf(ch_gsm->debug.at_debug_fp, "\nchannel now enabled\n");
			fflush(ch_gsm->debug.at_debug_fp);
			fclose(ch_gsm->debug.at_debug_fp);
			ch_gsm->debug.at_debug_fp = NULL;
		}
	}

	// set initial state
	ch_gsm->state = PG_CHANNEL_GSM_STATE_DISABLE;
	ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));

	ast_mutex_unlock(&ch_gsm->lock);

	// init receiver
	r_buf[0] = '\0';
	r_cptr = r_buf;
	r_buf_len = 0;
	r_buf_valid = 0;
	r_buf_active = 0;

	while (ch_gsm->flags.enable)
	{
		gettimeofday(&curr_tv, &curr_tz);

		FD_ZERO(&rfds);
		if (ch_gsm->flags.main_tty)
			FD_SET(ch_gsm->tty_fd, &rfds);

		timeout.tv_sec = 0;
		timeout.tv_usec = 250000;

		res = ast_select(ch_gsm->tty_fd + 1, &rfds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(ch_gsm->tty_fd, &rfds)) {
				r_len = read(ch_gsm->tty_fd, &r_char, 1);
				if (r_len > 0) {
					// print received symbol
					if (ch_gsm->debug.receiver) {
						ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
						if (ch_gsm->debug.receiver_debug_fp) {
							if (r_char == '\r')
								fprintf(ch_gsm->debug.receiver_debug_fp, "\nCR");
							else if (r_char == '\n')
								fprintf(ch_gsm->debug.receiver_debug_fp, "LF\n");
							else if (isprint(r_char))
								fprintf(ch_gsm->debug.receiver_debug_fp, "%c", r_char);
							else
								fprintf(ch_gsm->debug.receiver_debug_fp, "[%02x]", (unsigned char)r_char);
							fflush(ch_gsm->debug.receiver_debug_fp);
							fclose(ch_gsm->debug.receiver_debug_fp);
							ch_gsm->debug.receiver_debug_fp = NULL;
						}
					}
					if ((r_char == '\r') || (r_char == '\n')) {
						// check for data in received buffer
						if (r_char == '\n') {
							if (r_buf_len > 0)
								r_buf_valid = 1;
							else
								r_buf_valid = 0;
							if (!r_buf_active) r_buf_active = 1;
						}
						// terminate end of received string
						*r_cptr = '\0';
					} else if (r_char == '>') {
						// pdu prompt
						if (ch_gsm->debug.at) {
							ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
							if (ch_gsm->debug.at_debug_fp) {
								if ((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
									fprintf(ch_gsm->debug.at_debug_fp, "[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT SEND PDU - [%s]\n",
															tm_ptr->tm_year + 1900,
															tm_ptr->tm_mon+1,
															tm_ptr->tm_mday,
															tm_ptr->tm_hour,
															tm_ptr->tm_min,
															tm_ptr->tm_sec,
															curr_tv.tv_usec,
															ch_gsm->pdu_send_buf);
								else
									fprintf(ch_gsm->debug.at_debug_fp, "[%ld.%06ld] AT SEND PDU - [%s]\n",
															curr_tv.tv_sec,
															curr_tv.tv_usec,
															ch_gsm->pdu_send_buf);
								fflush(ch_gsm->debug.at_debug_fp);
								fclose(ch_gsm->debug.at_debug_fp);
								ch_gsm->debug.at_debug_fp = NULL;
							}
						}
						// Send SMS PDU
						write(ch_gsm->tty_fd, ch_gsm->pdu_send_buf, ch_gsm->pdu_send_len);
						// Send control symbol Ctrl-Z
						write(ch_gsm->tty_fd, &char_ctrlz, 1);
						r_buf[0] = '\0';
						r_cptr = r_buf;
						r_buf_len = 0;
						r_buf_valid = 0;
						r_buf_active = 0;
					} else {
						if (r_buf_active) {
							/* store current symbol into received buffer */
							*r_cptr++ = r_char;
							r_buf_len++;
							if (r_buf_len >= sizeof(r_buf)) {
								ast_log(LOG_WARNING, "GSM channel=\"%s\": AT-command buffer full\n", ch_gsm->alias);
								r_buf[0] = '\0';
								r_cptr = r_buf;
								r_buf_len = 0;
								r_buf_valid = 0;
								r_buf_active = 0;
							}
						}
					}
				} else if (r_len < 0) {
					if (errno != EAGAIN) {
						ast_log(LOG_ERROR, "GSM channel=\"%s\": read error: %s\n", ch_gsm->alias, strerror(errno));
					}
				}
			}
		} else if (res < 0) {
			ast_log(LOG_ERROR, "GSM channel=\"%s\": select error: %s\n", ch_gsm->alias, strerror(errno));
			continue;
		}

		ast_mutex_lock(&ch_gsm->lock);

		// check for command timeout expired
		if (ch_gsm->at_cmd) {
			// check command timeout
			if (is_x_timer_enable(ch_gsm->at_cmd->timer)) {
				// time is out
				if (is_x_timer_fired(ch_gsm->at_cmd->timer)) {
					// stop command timer
					x_timer_stop(ch_gsm->at_cmd->timer);
					ch_gsm->cmd_done = 1;
					// check for spacer
					if (ch_gsm->at_cmd->id < 0) {
						ast_free(ch_gsm->at_cmd);
						ch_gsm->at_cmd = NULL;
					} else {
						ast_debug(5, "GSM channel=\"%s\": AT [%.*s] attempt %lu\n", ch_gsm->alias, ch_gsm->at_cmd->cmd_len - 1, ch_gsm->at_cmd->cmd_buf, (unsigned long int)ch_gsm->at_cmd->attempt);
						if (ch_gsm->debug.receiver) {
							ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
							if (ch_gsm->debug.receiver_debug_fp) {
								if ((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
									fprintf(ch_gsm->debug.receiver_debug_fp, "\n[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT [%.*s] timeout attempt %lu\n",
															tm_ptr->tm_year + 1900,
															tm_ptr->tm_mon+1,
															tm_ptr->tm_mday,
															tm_ptr->tm_hour,
															tm_ptr->tm_min,
															tm_ptr->tm_sec,
															curr_tv.tv_usec,
															ch_gsm->at_cmd->cmd_len - 1,
				 											ch_gsm->at_cmd->cmd_buf,
					 										(unsigned long int)ch_gsm->at_cmd->attempt);
								else
									fprintf(ch_gsm->debug.receiver_debug_fp, "\n[%ld.%06ld] AT [%.*s] timeout attempt %lu\n",
															curr_tv.tv_sec,
															curr_tv.tv_usec,
															ch_gsm->at_cmd->cmd_len - 1,
				 											ch_gsm->at_cmd->cmd_buf,
				 											(unsigned long int)ch_gsm->at_cmd->attempt);
								fflush(ch_gsm->debug.receiver_debug_fp);
								fclose(ch_gsm->debug.receiver_debug_fp);
								ch_gsm->debug.receiver_debug_fp = NULL;
							}
						}
						if (ch_gsm->debug.at) {
							ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
							if (ch_gsm->debug.at_debug_fp) {
								if ((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
									fprintf(ch_gsm->debug.at_debug_fp, "[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT [%.*s] timeout attempt %lu\n",
															tm_ptr->tm_year + 1900,
															tm_ptr->tm_mon+1,
															tm_ptr->tm_mday,
															tm_ptr->tm_hour,
															tm_ptr->tm_min,
															tm_ptr->tm_sec,
															curr_tv.tv_usec,
															ch_gsm->at_cmd->cmd_len - 1,
				 											ch_gsm->at_cmd->cmd_buf,
				 											(unsigned long int)ch_gsm->at_cmd->attempt);
								else
									fprintf(ch_gsm->debug.at_debug_fp, "[%ld.%06ld] AT [%.*s] timeout attempt %lu\n",
															curr_tv.tv_sec,
															curr_tv.tv_usec,
															ch_gsm->at_cmd->cmd_len - 1,
				 											ch_gsm->at_cmd->cmd_buf,
					 										(unsigned long int)ch_gsm->at_cmd->attempt);
								fflush(ch_gsm->debug.at_debug_fp);
								fclose(ch_gsm->debug.at_debug_fp);
								ch_gsm->debug.at_debug_fp = NULL;
							}
						}
						//
						if (ch_gsm->at_cmd->attempt) {
							ch_gsm->at_cmd->attempt--;
							r_buf[0] = '\0';
							r_cptr = r_buf;
							r_buf_len = 0;
							r_buf_valid = 0;
							r_buf_active = 0;
							pg_atcommand_trysend(ch_gsm);
						} else {
							if (!ch_gsm->flags.func_test_run) {
								// select AT command id
								if (ch_gsm->at_cmd->at->id == AT_D)
									ast_log(LOG_NOTICE, "GSM channel=\"%s\": dialing failed\n", ch_gsm->alias);
								else if (ch_gsm->at_cmd->at->id == AT_H)
									ast_log(LOG_NOTICE, "GSM channel=\"%s\": hangup - permission denied\n", ch_gsm->alias);
								else {
									ast_log(LOG_WARNING, "GSM channel=\"%s\": command [%.*s] timeout expired!!!\n",
													ch_gsm->alias, ch_gsm->at_cmd->cmd_len - 1, ch_gsm->at_cmd->cmd_buf);
									ast_verb(4, "GSM channel=\"%s\": run functionality test\n", ch_gsm->alias);
								}
								// reset registaration status
								ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
								// reset callwait state
								ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
								// reset clir state
								ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
								// reset rssi value
								ch_gsm->rssi = 99;
								// reset ber value
								ch_gsm->ber = 99;
								// stop all timers
								memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
								// hangup active calls
								AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
									pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NORMAL_CLEARING);
								while (ch_gsm->call_list.first)
								{
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1000);
									ast_mutex_lock(&ch_gsm->lock);
								}
								// run functionality test
								ch_gsm->flags.func_test_run = 1;
								ch_gsm->flags.func_test_done = 0;
								ch_gsm->baudrate_test = ch_gsm->baudrate;
								ch_gsm->baudrate = 0;
								ch_gsm->state = PG_CHANNEL_GSM_STATE_TEST_FUN;
								ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								// flush at command queue
								ast_free(ch_gsm->at_cmd);
								ch_gsm->at_cmd = NULL;
								pg_atcommand_queue_flush(ch_gsm);
								// run testfun timer
								x_timer_set(ch_gsm->timers.testfun, testfun_timeout);
								// run testfunsend timer
								x_timer_set(ch_gsm->timers.testfunsend, testfunsend_timeout);
								// reset sms send operation
								ch_gsm->pdu_send_id = 0;
							}
							// init at command buffer
							r_buf[0] = '\0';
							r_cptr = r_buf;
							r_buf_len = 0;
							r_buf_valid = 0;
							r_buf_active = 0;
						}
					}
				} // end of timer fired
			} // end of timer run
		}

		if (r_buf_valid) {
			// is response from module
			if ((ch_gsm->at_cmd) && (is_at_com_response(ch_gsm->at_cmd->at, r_buf))) {
				if (ch_gsm->at_cmd->show) {
					write(ch_gsm->at_pipe[1], r_buf, r_buf_len);
					write(ch_gsm->at_pipe[1], &char_lf, 1);
				}
				if (ch_gsm->debug.receiver) {
					ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
					if (ch_gsm->debug.receiver_debug_fp) {
						if((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
							fprintf(ch_gsm->debug.receiver_debug_fp, "\n[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT recv [%.*s] - [%s] - ",
													tm_ptr->tm_year + 1900,
													tm_ptr->tm_mon+1,
													tm_ptr->tm_mday,
													tm_ptr->tm_hour,
													tm_ptr->tm_min,
													tm_ptr->tm_sec,
													curr_tv.tv_usec,
													ch_gsm->at_cmd->cmd_len - 1,
													ch_gsm->at_cmd->cmd_buf,
													r_buf);
						else
							fprintf(ch_gsm->debug.receiver_debug_fp, "\n[%ld.%06ld] AT recv [%.*s] - [%s] - ",
													curr_tv.tv_sec,
													curr_tv.tv_usec,
													ch_gsm->at_cmd->cmd_len - 1,
													ch_gsm->at_cmd->cmd_buf,
													r_buf);
						fflush(ch_gsm->debug.receiver_debug_fp);
						fclose(ch_gsm->debug.receiver_debug_fp);
						ch_gsm->debug.receiver_debug_fp = NULL;
					}
				}
				if (ch_gsm->debug.at) {
					ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
					if (ch_gsm->debug.at_debug_fp) {
						if((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
							fprintf(ch_gsm->debug.at_debug_fp, "[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT recv [%.*s] - [%s] - ",
													tm_ptr->tm_year + 1900,
													tm_ptr->tm_mon+1,
													tm_ptr->tm_mday,
													tm_ptr->tm_hour,
													tm_ptr->tm_min,
													tm_ptr->tm_sec,
													curr_tv.tv_usec,
													ch_gsm->at_cmd->cmd_len - 1,
													ch_gsm->at_cmd->cmd_buf,
													r_buf);
						else
							fprintf(ch_gsm->debug.at_debug_fp, "[%ld.%06ld] AT recv [%.*s] - [%s] - ",
													curr_tv.tv_sec,
													curr_tv.tv_usec,
													ch_gsm->at_cmd->cmd_len - 1,
													ch_gsm->at_cmd->cmd_buf,
													r_buf);
						fflush(ch_gsm->debug.at_debug_fp);
						fclose(ch_gsm->debug.at_debug_fp);
						ch_gsm->debug.at_debug_fp = NULL;
					}
				}					
				// test for command done
				if (is_at_com_done(r_buf)) {
					if (ch_gsm->at_cmd->show)
						close(ch_gsm->at_pipe[1]);
					if (ch_gsm->debug.receiver) {
						ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
						if (ch_gsm->debug.receiver_debug_fp) {
							fprintf(ch_gsm->debug.receiver_debug_fp, "done\n");
							fflush(ch_gsm->debug.receiver_debug_fp);
							fclose(ch_gsm->debug.receiver_debug_fp);
							ch_gsm->debug.receiver_debug_fp = NULL;
						}
					}
					if (ch_gsm->debug.at) {
						ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
						if (ch_gsm->debug.at_debug_fp) {
							fprintf(ch_gsm->debug.at_debug_fp, "done\n");
							fflush(ch_gsm->debug.at_debug_fp);
							fclose(ch_gsm->debug.at_debug_fp);
							ch_gsm->debug.at_debug_fp = NULL;
						}
					}
					ch_gsm->cmd_done = 1;
				} else {
					if (ch_gsm->debug.receiver) {
						ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
						if (ch_gsm->debug.receiver_debug_fp) {
							fprintf(ch_gsm->debug.receiver_debug_fp, "in progress\n");
							fflush(ch_gsm->debug.receiver_debug_fp);
							fclose(ch_gsm->debug.receiver_debug_fp);
							ch_gsm->debug.receiver_debug_fp = NULL;
						}
					}
					if (ch_gsm->debug.at) {
						ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
						if (ch_gsm->debug.at_debug_fp) {
							fprintf(ch_gsm->debug.at_debug_fp, "in progress\n");
							fflush(ch_gsm->debug.at_debug_fp);
							fclose(ch_gsm->debug.at_debug_fp);
							ch_gsm->debug.at_debug_fp = NULL;
						}
					}
				}
				// general AT commands
				// select by operation
				if (ch_gsm->at_cmd->oper == AT_OPER_EXEC) {
					// EXEC operations
					switch (ch_gsm->at_cmd->id)
					{
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_D:
						case AT_A:
						case AT_H:
							if (strstr(r_buf, "ERROR")) {
								if (ch_gsm->at_cmd->attempt) {
									ch_gsm->at_cmd->attempt--;
									pg_atcommand_trysend(ch_gsm);
								} else {
									ast_log(LOG_WARNING, "GSM channel=\"%s\": command [%.*s] attempts count fired!!!\n", ch_gsm->alias, ch_gsm->at_cmd->cmd_len - 1, ch_gsm->at_cmd->cmd_buf);
									// hangup unconfirmed call
									AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
									{
										if (!call->line)
											pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NORMAL_TEMPORARY_FAILURE);
									}
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_GMM:
						case AT_CGMM:
							if (strcmp(r_buf, "OK") && !strstr(r_buf, "ERROR")) {
								if (!ch_gsm->model) ch_gsm->model = ast_strdup(r_buf);
							}
							break;
						//+++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_GMR:
						case AT_CGMR:
							if (strcmp(r_buf, "OK") && !strstr(r_buf, "ERROR")) {
								if (!ch_gsm->firmware) ch_gsm->firmware = ast_strdup(r_buf);
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_GSN:
						case AT_CGSN:
							if (is_str_digit(r_buf)) {
								ast_copy_string(ch_gsm->imei, r_buf, sizeof(ch_gsm->imei));
#if 0
								// check for query
								if (chnl->querysig.imei) {
									ast_verbose("Polygator: GSM channel=\"%s\": qwery(imei): %s\n", ch_gsm->alias, chnl->imei);
									chnl->querysig.imei = 0;
								}
#endif
							} else if (strstr(r_buf, "ERROR")) {
#if 0
								// check for query
								if (chnl->querysig.imei) {
									ast_verbose("Polygator: GSM channel=\"%s\": qwery(imei): error\n", ch_gsm->alias);
									chnl->querysig.imei = 0;
								}
#endif
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CIMI:
							if (is_str_digit(r_buf)) {
								if (!ch_gsm->imsi) ch_gsm->imsi = ast_strdup(r_buf);
								if (ch_gsm->flags.dcr_table_needed) {
									pg_dcr_table_create(ch_gsm->imsi, &ch_gsm->lock);
									ch_gsm->flags.dcr_table_needed = 0;
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CLCC:
							if ((is_str_begin_by(r_buf, "+CLCC:")) && (sscanf(r_buf, "+CLCC: %u,%u,%u,%u,%u,\"%[0-9+]\",%u,\"%s\"", &tu0, &tu1, &tu2, &tu3, &tu4, ts0, &tu5, ts1) == 8)) {
								parser_ptrs.clcc_ex = (struct at_gen_clcc_exec *)tmpbuf;
								if (at_gen_clcc_exec_parse(r_buf, r_buf_len, parser_ptrs.clcc_ex) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_clcc_exec_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
								} else {
									// get unconfirmed call by line id
									if ((call = pg_channel_gsm_get_call(ch_gsm, 0))) {
										// check for clcc stat waiting
										if (parser_ptrs.clcc_ex->dir) {
											// incoming call - mobile terminated
											if ((parser_ptrs.clcc_ex->stat == 0) || (parser_ptrs.clcc_ex->stat == 4)) {
												call->line = parser_ptrs.clcc_ex->id;
												ast_verb(4, "GSM channel=\"%s\": incoming active line id=%d\n", ch_gsm->alias, parser_ptrs.clcc_ex->id);
												// processing OVERLAP_RECEIVING
												if (call->state == PG_CALL_GSM_STATE_OVERLAP_RECEIVING) {
													// get calling number
													if (parser_ptrs.clip_un->number_len > 0) {
														ast_copy_string(call->calling_name.value, parser_ptrs.clcc_ex->number, MAX_ADDRESS_LENGTH);
														call->calling_name.length = parser_ptrs.clcc_ex->number_len;
														call->calling_name.type.full = parser_ptrs.clcc_ex->type;
														address_normalize(&call->calling_name);
													} else
														address_classify("unknown", &call->calling_name);
													// run call state machine
													pg_call_gsm_sm(call, PG_CALL_GSM_MSG_INFO_IND, 0);
												}
											}
										} else {
											// outgoing call - mobile originated
											if ((parser_ptrs.clcc_ex->stat == 0) || (parser_ptrs.clcc_ex->stat == 2) || (parser_ptrs.clcc_ex->stat == 3)) {
												call->line = parser_ptrs.clcc_ex->id;
// 												call->clcc_stat = parser_ptrs.clcc_ex->stat;
												ast_verb(4, "GSM channel=\"%s\": outgoing active line id=%d\n", ch_gsm->alias, parser_ptrs.clcc_ex->id);
											}
										}
									}
									// check for change call state on active line
									if ((call = pg_channel_gsm_get_call(ch_gsm, parser_ptrs.clcc_ex->id))) {
										if (call->clcc_stat != parser_ptrs.clcc_ex->stat) {
											if (parser_ptrs.clcc_ex->dir == 0) {
												// outgoing call - mobile originated
												switch (parser_ptrs.clcc_ex->stat)
												{
													case 0: // active
														// user response - setup confirm
														if ((call->state == PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING) ||
															(call->state == PG_CALL_GSM_STATE_CALL_DELIVERED)) {
															pg_call_gsm_sm(call, PG_CALL_GSM_MSG_SETUP_CONFIRM, 0);
															// stop dial timer
															x_timer_stop(call->timers.dial);
															// stop proceeding timer
															x_timer_stop(call->timers.proceeding);
														} else if (call->state == PG_CALL_GSM_STATE_REMOTE_HOLD) {
															pg_call_gsm_sm(call, PG_CALL_GSM_MSG_UNHOLD_IND, 0);
														}
														break;
													case 1: // held
														if (call->state == PG_CALL_GSM_STATE_ACTIVE) {
															pg_call_gsm_sm(call, PG_CALL_GSM_MSG_HOLD_IND, 0);
														}
														break;
													case 2: // dialing
														if (call->state == PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING) {
															pg_call_gsm_sm(call, PG_CALL_GSM_MSG_PROCEEDING_IND, 0);
															// stop proceeding timer
															x_timer_stop(call->timers.proceeding);
														}
														break;
													case 3: // alerting
														if (call->state == PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING) {
															pg_call_gsm_sm(call, PG_CALL_GSM_MSG_ALERTING_IND, 0);
															// stop proceeding timer
															x_timer_stop(call->timers.proceeding);
														}
														break;
													default:
														break;
												}
											}
										}
										call->clcc_stat = parser_ptrs.clcc_ex->stat;
										call->clcc_mpty = parser_ptrs.clcc_ex->mpty;
									}
									// check for wait line id
									if (!(call = pg_channel_gsm_get_call(ch_gsm, parser_ptrs.clcc_ex->id))) {
										// check for clcc stat waiting
										if (parser_ptrs.clcc_ex->stat == 5) {
											if ((call = pg_channel_gsm_get_new_call(ch_gsm))) {
												call->direction = PG_CALL_GSM_DIRECTION_INCOMING;
												call->line = parser_ptrs.clcc_ex->id;
												call->clcc_stat = parser_ptrs.clcc_ex->stat;
												call->state = PG_CALL_GSM_STATE_OVERLAP_RECEIVING;
												// get wait line number
												if (parser_ptrs.clip_un->number_len > 0) {
													ast_copy_string(call->calling_name.value, parser_ptrs.clcc_ex->number, MAX_ADDRESS_LENGTH);
													call->calling_name.length = parser_ptrs.clcc_ex->number_len;
													call->calling_name.type.full = parser_ptrs.clcc_ex->type;
													address_normalize(&call->calling_name);
												} else
													address_classify("unknown", &call->calling_name);
												ast_verb(2, "GSM channel=\"%s\": Call \"%s%s\" Waiting...\n", ch_gsm->alias, 
																			(call->calling_name.type.full == 145)?("+"):(""),
																			call->calling_name.value);
												ast_verb(4, "GSM channel=\"%s\": wait line id=%d\n", ch_gsm->alias, parser_ptrs.clcc_ex->id);
												// run call state machine
												pg_call_gsm_sm(call, PG_CALL_GSM_MSG_INFO_IND, 0);
											}
										}
									}
									// perform line contest
									AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
									{
										if (call->line == parser_ptrs.clcc_ex->id) call->contest = 0;
									}
								} /* parsing success */
							} else if (is_str_begin_by(r_buf, "OK")) {
								// check for conference condition
								conference = 1;
								count = 0;
								AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
								{
									count++;
									if ((call->state != PG_CALL_GSM_STATE_ACTIVE) || (call->clcc_mpty)) {
										conference = 0;
										break;
									}
								}
								if ((count > 1) && (conference)) {
									// is conference
									count = 0;
									AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
									{
										if (count++)
											ast_channel_set_fd(call->owner, 0, -1);
										else
											ast_channel_set_fd(call->owner, 0, ch_gsm->channel_rtp->fd);
									}
									ast_verb(2, "GSM channel=\"%s\": Conference started\n", ch_gsm->alias);
									pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "3");
								} else {
									// adjust call states
									AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
									{
										if ((call->state == PG_CALL_GSM_STATE_ACTIVE) && (call->clcc_stat != 0)) {
											ast_verb(5, "GSM channel=\"%s\": call line id=%d adjust to active\n", ch_gsm->alias, call->line);
											pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "2");
											break;
										}
									}
								}
								// perform line contest
								AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
								{
									if (call->contest > 1)
										pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NORMAL_CLEARING);
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CPAS:
							if (is_str_begin_by(r_buf, "+CPAS:")) {
								if ((str0 = strchr(r_buf, SP))) {
									str0++;
									if (is_str_digit(str0)) {
										// perform action if cpas changed
										switch (atoi(str0))
										{
											//++++++++++++++++++++++++++++++++++
											case 0:
											case 2:
											case 3:
											case 4:
												break;
											//++++++++++++++++++++++++++++++++++
											default:
												ast_log(LOG_WARNING, "GSM channel=\"%s\": unknown cpas value = %d\n", ch_gsm->alias, atoi(str0));
												break;
										}
									}
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CSQ:
							if (is_str_begin_by(r_buf, "+CSQ:")) {
								parser_ptrs.csq_ex = (struct at_gen_csq_exec *)tmpbuf;
								if (at_gen_csq_exec_parse(r_buf, r_buf_len, parser_ptrs.csq_ex) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_csq_exec_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
								} else {
									ch_gsm->rssi = parser_ptrs.csq_ex->rssi;
									ch_gsm->ber = parser_ptrs.csq_ex->ber;
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CNUM:
							if (is_str_begin_by(r_buf, "+CNUM:")) {
								parser_ptrs.cnum_ex = (struct at_gen_cnum_exec *)tmpbuf;
								if (at_gen_cnum_exec_parse(r_buf, r_buf_len, parser_ptrs.cnum_ex) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_cnum_exec_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
								} else {
									ast_copy_string(ch_gsm->subscriber_number.value, parser_ptrs.cnum_ex->number, MAX_ADDRESS_LENGTH);
									ch_gsm->subscriber_number.length = parser_ptrs.cnum_ex->number_len;
									ch_gsm->subscriber_number.type.full = parser_ptrs.cnum_ex->type;
									address_normalize(&ch_gsm->subscriber_number);
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CEER:
							if (is_str_begin_by(r_buf, "+CEER:")) {
								call = pg_channel_gsm_get_call_by_hash(ch_gsm, ch_gsm->at_cmd->sub_cmd);
								if (sscanf(r_buf, "+CEER: \"Cause Select:%u Cause:%u\"", &cause_select, &cause_value) == 2) { // SIM900
									ast_verb(4, "GSM channel=\"%s\": CEER Cause Select=%u Cause=%u\n", ch_gsm->alias, cause_select, cause_value);
									switch (cause_select)
									{
										case 67:
											if (call) {
												if ((cause_value > 0) && (cause_value < 128))
													pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, cause_value);
												else
													pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, call->hangup_cause);
											}
											break;
										default:
											if (call)
												pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, call->hangup_cause);
											break;
									}
								} else if (sscanf(r_buf, "+CEER: %u,%u", &cause_select, &cause_value) == 2) { // M10
									ast_verb(4, "GSM channel=\"%s\": CEER Location ID=%u Cause=%u\n", ch_gsm->alias, cause_select, cause_value);
									switch (cause_select)
									{
										case 1:
											if (call)
												pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, cause_value);
											break;
										default:
											if (call)
												pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, call->hangup_cause);
											break;
									}
								} else {
									if (call)
										pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, call->hangup_cause);
								}
							} else if (strstr(r_buf, "ERROR")) {
								if (call)
									pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, call->hangup_cause);
							}
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						default:
							break;
						}
					} // end of EXEC operations
				else if (ch_gsm->at_cmd->oper == AT_OPER_TEST) {
					// TEST operations
					switch (ch_gsm->at_cmd->id)
					{
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						default:
							break;
					}
				} // end of TEST operations
				else if (ch_gsm->at_cmd->oper == AT_OPER_READ) {
					// READ operations
					switch (ch_gsm->at_cmd->id)
					{
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CLIR:
							if (strstr(r_buf, "+CLIR:")) {
								parser_ptrs.clir_rd = (struct at_gen_clir_read *)tmpbuf;
								if (at_gen_clir_read_parse(r_buf, r_buf_len, parser_ptrs.clir_rd) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_clir_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
#if 0
									// check for query
									if(chnl->querysig.hidenum){
										ast_verbose("Polygator: GSM channel=\"%s\": qwery(hidenum): error\n", ch_gsm->alias);
										chnl->querysig.hidenum = 0;
										}
#endif
								} else {
									ch_gsm->clir = parser_ptrs.clir_rd->n;
									ch_gsm->clir_status = parser_ptrs.clir_rd->m;
#if 0
									// check for query
									if(chnl->querysig.hidenum){
										ast_verbose("Polygator: GSM channel=\"%s\": qwery(hidenum): %s\n",
											ch_gsm->alias,
											eggsm_hidenum_settings_str(chnl->hidenum_set));
										chnl->querysig.hidenum = 0;
										}
#endif
								}
							} else if (strstr(r_buf, "OK")) {
								if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SERVICE) {
									ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
									ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								}
							} else if (strstr(r_buf, "ERROR")) {
								ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
								ch_gsm->clir_status = PG_CLIR_STATUS_UNKNOWN;
#if 0
								// check for query
								if(chnl->querysig.hidenum){
									ast_verbose("Polygator: GSM channel=\"%s\": qwery(hidenum): error\n", ch_gsm->alias);
									chnl->querysig.hidenum = 0;
									}
#endif
								if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SERVICE) {
									ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
									ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_COPS:
							if (is_str_begin_by(r_buf, "+COPS:")) {
								parser_ptrs.cops_rd = (struct at_gen_cops_read *)tmpbuf;
								if (at_gen_cops_read_parse(r_buf, r_buf_len, parser_ptrs.cops_rd) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_cops_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
								} else {
									if ((parser_ptrs.cops_rd->format == 0) && (parser_ptrs.cops_rd->oper_len >= 0)) {
										if (!ch_gsm->operator_name) ch_gsm->operator_name = ast_strdup(parser_ptrs.cops_rd->oper);
									} else if ((parser_ptrs.cops_rd->format == 2) && (parser_ptrs.cops_rd->oper_len >= 0)) {
										if (!ch_gsm->operator_code) ch_gsm->operator_code = ast_strdup(parser_ptrs.cops_rd->oper);
									}
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CREG:
							if (is_str_begin_by(r_buf, "+CREG:")) {
								ch_gsm->reg_stat_old = ch_gsm->reg_stat;
								parser_ptrs.creg_rd = (struct at_gen_creg_read *)&tmpbuf;
								if (at_gen_creg_read_parse(r_buf, r_buf_len, parser_ptrs.creg_rd) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_parse creg_read(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
								} else {
									ch_gsm->reg_stat = parser_ptrs.creg_rd->stat;
									if (ch_gsm->reg_stat_old != ch_gsm->reg_stat) {
										ast_verbose("Polygator: GSM channel=\"%s\": registration status - %s\n", ch_gsm->alias, reg_status_print(ch_gsm->reg_stat));
										// processing registaration status
										if ((ch_gsm->reg_stat == REG_STAT_NOTREG_NOSEARCH) || (ch_gsm->reg_stat == REG_STAT_REG_DENIED)) {
											// "0" no search, "3" denied
											// stop runquartersecond timer
											x_timer_stop(ch_gsm->timers.runquartersecond);
											// stop runhalfsecond timer
											x_timer_stop(ch_gsm->timers.runhalfsecond);
											// stop runonesecond timer
											x_timer_stop(ch_gsm->timers.runonesecond);
											// stop runhalfminute timer
											x_timer_stop(ch_gsm->timers.runhalfminute);
											// stop runoneminute timer
											x_timer_stop(ch_gsm->timers.runoneminute);
											// stop smssend timer
											x_timer_stop(ch_gsm->timers.smssend);
											// stop runvifesecond timer
											x_timer_stop(ch_gsm->timers.runfivesecond);
											// stop registering timer
											x_timer_stop(ch_gsm->timers.registering);
											// hangup active call
											AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
											{
												// hangup - GSM
												if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
													pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "0");
												else
													pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
												// hangup - asterisk core call
												pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NORMAL_CLEARING);
											}
											if (ch_gsm->flags.sim_inserted) {
												// decrement attempt counter
												if (ch_gsm->reg_try_count > 0)
													ch_gsm->reg_try_count--;
												// check rest of attempt count
												if (ch_gsm->reg_try_count != 0) {
													ast_verbose("Polygator: GSM channel=\"%s\": try next attempt registration on BTS\n", ch_gsm->alias);
													if (ch_gsm->reg_try_count > 0)
														ast_verbose("Polygator: GSM channel=\"%s\": remaining %d attempts\n", ch_gsm->alias, ch_gsm->reg_try_count);
												} else { // attempts count fired
													ast_verbose("Polygator: GSM channel=\"%s\": registration attempts count fired\n", ch_gsm->alias);
													// set change sim signal
													ch_gsm->flags.sim_change = 1;
													// mark baned sim id
													if (ch_gsm->iccid_ban) ast_free(ch_gsm->iccid_ban);
													ch_gsm->iccid_ban = ast_strdup(ch_gsm->iccid_ban);
												}
											}
											// disable GSM module functionality
											pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
											//
											ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
											ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
											// start simpoll timer
											x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
										} else if ((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING)) {
											// "1" home network, "5" roaming
											ch_gsm->reg_try_count = ch_gsm->config.reg_try_count;
											// stop registering timer
											x_timer_stop(ch_gsm->timers.registering);
										}
									}
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CFUN:
							if (ch_gsm->flags.func_test_run) {
								if (strstr(r_buf, "+CFUN:")) {
									ch_gsm->flags.func_test_run = 0;
									ch_gsm->flags.func_test_done = 1;
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CLVL:
							if (is_str_begin_by(r_buf, "+CLVL:")) {
								if ((str0 = strchr(r_buf, SP))) {
									str0++;
									if (is_str_digit(str0)) {
										ch_gsm->gainin = atoi(str0);
#if 0
										// check for query
										if (chnl->querysig.gainin) {
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainin): %d\n", ch_gsm->alias, rc);
											chnl->querysig.gainin = 0;
										}
#endif
									} else{
#if 0
										// check for query
										if(chnl->querysig.gainin){
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainin): error\n", ch_gsm->alias);
											chnl->querysig.gainin = 0;
											}
#endif
									}
								}
							} else if (strstr(r_buf, "ERROR")) {
#if 0
								// check for query
								if(chnl->querysig.gainin){
									ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainin): error\n", ch_gsm->alias);
									chnl->querysig.gainin = 0;
									}
#endif
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CSCA:
							if (is_str_begin_by(r_buf, "+CSCA:")) {
								parser_ptrs.csca_rd = (struct at_gen_csca_read *)tmpbuf;
								if (at_gen_csca_read_parse(r_buf, r_buf_len, parser_ptrs.csca_rd) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_csca_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
								} else {
									if (is_address_string(parser_ptrs.csca_rd->sca)) {
										ast_copy_string(ch_gsm->smsc_number.value, parser_ptrs.csca_rd->sca, MAX_ADDRESS_LENGTH);
										ch_gsm->smsc_number.length = parser_ptrs.csca_rd->sca_len;
										ch_gsm->smsc_number.type.full = parser_ptrs.csca_rd->tosca;
										address_normalize(&ch_gsm->smsc_number);
									} else if (is_str_xdigit(parser_ptrs.csca_rd->sca)) {
										ip = parser_ptrs.csca_rd->sca;
										ilen = parser_ptrs.csca_rd->sca_len;
										op = ch_gsm->smsc_number.value;
										olen = MAX_ADDRESS_LENGTH;
										memset(ch_gsm->smsc_number.value, 0 , MAX_ADDRESS_LENGTH);
										//
										if (!str_hex_to_bin(&ip, &ilen, &op, &olen)) {
											ch_gsm->smsc_number.length = strlen(ch_gsm->smsc_number.value);
											ch_gsm->smsc_number.type.full = parser_ptrs.csca_rd->tosca;
											address_normalize(&ch_gsm->smsc_number);
										}
									}
								}
							} else if (strstr(r_buf, "ERROR")) {
								ast_copy_string(ch_gsm->smsc_number.value, "unknown", MAX_ADDRESS_LENGTH);
								ch_gsm->smsc_number.length = strlen(ch_gsm->smsc_number.value);
								ch_gsm->smsc_number.type.bits.reserved = 1;
								ch_gsm->smsc_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
								ch_gsm->smsc_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CPIN:
							if (!strncasecmp(r_buf, "+CPIN:", 6)) {
								// stop pinwait timer
								x_timer_stop(ch_gsm->timers.pinwait);
								// processing response
								if ((ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_CFUN) || (ch_gsm->state == PG_CHANNEL_GSM_STATE_CHECK_PIN)) {
									// set SIM status to polling mode
									if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
										pg_atcommand_queue_append(ch_gsm, AT_SIM300_CSMINS, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
									else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
										pg_atcommand_queue_append(ch_gsm, AT_SIM900_CSMINS, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
									else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
										pg_atcommand_queue_append(ch_gsm, AT_M10_QSIMSTAT, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
									// get imei
									pg_atcommand_queue_append(ch_gsm, AT_GSN, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
									// get model
									pg_atcommand_queue_append(ch_gsm, AT_GMM, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
									// get firmware
									pg_atcommand_queue_append(ch_gsm, AT_GMR, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
									//
									if (!strcasecmp(r_buf, "+CPIN: NOT INSERTED")) {
										// - SIM card not inserted
										if (ch_gsm->flags.sim_inserted) {
											// reset channel phone number
											ast_copy_string(ch_gsm->subscriber_number.value, "unknown", MAX_ADDRESS_LENGTH);
											ch_gsm->subscriber_number.length = strlen(ch_gsm->subscriber_number.value);
											ch_gsm->subscriber_number.type.bits.reserved = 1;
											ch_gsm->subscriber_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
											ch_gsm->subscriber_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
											// reset SMS center number
											ast_copy_string(ch_gsm->smsc_number.value, "unknown", MAX_ADDRESS_LENGTH);
											ch_gsm->smsc_number.length = strlen(ch_gsm->smsc_number.value);
											ch_gsm->smsc_number.type.bits.reserved = 1;
											ch_gsm->smsc_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
											ch_gsm->smsc_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;

											if (ch_gsm->operator_name) ast_free(ch_gsm->operator_name); ch_gsm->operator_name = NULL;
											if (ch_gsm->operator_code) ast_free(ch_gsm->operator_code); ch_gsm->operator_code = NULL;
											if (ch_gsm->imsi) ast_free(ch_gsm->imsi); ch_gsm->imsi = NULL;
											if (ch_gsm->iccid) ast_free(ch_gsm->iccid); ch_gsm->iccid = NULL;
// 											if (ch_gsm->pin) ast_free(ch_gsm->pin); ch_gsm->pin = NULL;
// 											if (ch_gsm->puk) ast_free(ch_gsm->puk); ch_gsm->puk = NULL;
											if (ch_gsm->config.balance_request) ast_free(ch_gsm->config.balance_request); ch_gsm->config.balance_request = NULL;

											ch_gsm->flags.sms_table_needed = 1;
											x_timer_stop(ch_gsm->timers.smssend);

											ch_gsm->flags.dcr_table_needed = 1;

											ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
											ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;

											ast_verbose("Polygator: GSM channel=\"%s\": SIM removed\n", ch_gsm->alias);
										} else if (!ch_gsm->flags.sim_startup)
											ast_verbose("Polygator: GSM channel=\"%s\": SIM not inserted\n", ch_gsm->alias);
										//
										ch_gsm->flags.sim_startup = 1;
										ch_gsm->flags.sim_inserted = 0;
										//
										if (ch_gsm->flags.sim_change)
											ch_gsm->flags.sim_test = 1;
										//
										pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
										//
										ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
										ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
										// start simpoll timer
										x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
									} else if (!strcasecmp(r_buf, "+CPIN: READY")) {
										// - PIN ready
										if (!ch_gsm->flags.sim_inserted)
											ast_verbose("Polygator: GSM channel=\"%s\": SIM inserted\n", ch_gsm->alias);
										if (!ch_gsm->flags.pin_accepted)
											ast_verbose("Polygator: GSM channel=\"%s\": PIN ready\n", ch_gsm->alias);
										// set SIM present flag
										ch_gsm->flags.sim_startup = 1;
										ch_gsm->flags.sim_inserted = 1;
										ch_gsm->flags.pin_required = 0;
										ch_gsm->flags.puk_required = 0;
										ch_gsm->flags.pin_accepted = 1;
										// get iccid
										if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
											pg_atcommand_queue_append(ch_gsm, AT_SIM300_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
										else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
											pg_atcommand_queue_append(ch_gsm, AT_SIM900_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
										else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
											pg_atcommand_queue_append(ch_gsm, AT_M10_QCCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
										//
										if (ch_gsm->flags.suspend_now) {
											// stop all timers
											memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
											ch_gsm->flags.suspend_now = 0;
											pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
											ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_SUSPEND;
											ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
											// start waitsuspend timer
											x_timer_set(ch_gsm->timers.waitsuspend, waitsuspend_timeout);
										} else if (ch_gsm->flags.sim_change) {
											pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
											ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
											ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
											// start simpoll timer
											x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
										} else {
											ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_CALL_READY;
											ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
											// start callready timer
											x_timer_set(ch_gsm->timers.callready, callready_timeout);
										}
									} else if (!strcasecmp(r_buf, "+CPIN: SIM PIN")) {
										// - PIN is required
										if (!ch_gsm->flags.sim_inserted)
											ast_verbose("Polygator: GSM channel=\"%s\": SIM inserted\n", ch_gsm->alias);
											// set SIM present flag
											ch_gsm->flags.sim_startup = 1;
											ch_gsm->flags.sim_inserted = 1;
											ch_gsm->flags.pin_required = 1;
											ch_gsm->flags.puk_required = 0;
											ch_gsm->flags.pin_accepted = 0;
											// get iccid
											if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
												pg_atcommand_queue_append(ch_gsm, AT_SIM300_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
											else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
												pg_atcommand_queue_append(ch_gsm, AT_SIM900_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
											else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
												pg_atcommand_queue_append(ch_gsm, AT_M10_QCCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
									} else if (!strcasecmp(r_buf, "+CPIN: SIM PUK")) {
										// - PUK is required
										if (!ch_gsm->flags.sim_inserted)
											ast_verbose("Polygator: GSM channel=\"%s\": SIM inserted\n", ch_gsm->alias);
										// set SIM present flag
										ch_gsm->flags.sim_startup = 1;
										ch_gsm->flags.sim_inserted = 1;
										ch_gsm->flags.pin_required = 0;
										ch_gsm->flags.puk_required = 1;
										ch_gsm->flags.pin_accepted = 0;
										// get iccid
										if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
											pg_atcommand_queue_append(ch_gsm, AT_SIM300_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
										else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
											pg_atcommand_queue_append(ch_gsm, AT_SIM900_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
										else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
											pg_atcommand_queue_append(ch_gsm, AT_M10_QCCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
									} else if (!strcasecmp(r_buf, "+CPIN: SIM ERROR")) {
										// - SIM ERROR
										pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
										//
										ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
										ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
										// start simpoll timer
										x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
									}
								} else if (ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_SUSPEND) {
									if (!strcasecmp(r_buf, "+CPIN: NOT READY")) {
										// stop waitsuspend timer
										x_timer_stop(ch_gsm->timers.waitsuspend);
										//
										ast_verbose("GSM channel=\"%s\": module switch to suspend state\n", ch_gsm->alias);
										//
										ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
										ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
										// reset registaration status
										ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
										// reset callwait state
										ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
										// reset clir state
										ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
										// reset rssi value
										ch_gsm->rssi = 99;
										// reset ber value
										ch_gsm->ber = 99;
									}
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						default:
							break;
						}
					} // end of READ operations
				else if (ch_gsm->at_cmd->oper == AT_OPER_WRITE) {
					// WRITE operations
					switch (ch_gsm->at_cmd->id)
					{
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CCWA:
							if (strstr(r_buf, "+CCWA:")) {
								parser_ptrs.ccwa_wr = (struct at_gen_ccwa_write *)tmpbuf;
								if (at_gen_ccwa_write_parse(r_buf, r_buf_len, parser_ptrs.ccwa_wr) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_ccwa_write_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
#if 0
									// check for query
									if(chnl->querysig.callwait){
										ast_verbose("Polygator: GSM channel=\"%s\": qwery(callwait): error\n", ch_gsm->alias);
										chnl->querysig.callwait = 0;
										}
#endif
								} else {
									if (parser_ptrs.ccwa_wr->class & CALL_CLASS_VOICE) {
										ch_gsm->callwait = parser_ptrs.ccwa_wr->status;
#if 0
										// check for query
										if(chnl->querysig.callwait){
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(callwait): %s\n",
												ch_gsm->alias,
												eggsm_callwait_status_str(chnl->callwait));
											chnl->querysig.callwait = 0;
											}
#endif
									}
								}
							} else if (strstr(r_buf, "OK")) {
								if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CCWA_SET)
									ch_gsm->callwait = ch_gsm->config.callwait;
								if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SERVICE) {
									ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
									ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								}
							} else if (strstr(r_buf, "ERROR")) {
								ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
#if 0
								// check for query
								if (chnl->querysig.callwait) {
									ast_verbose("Polygator: GSM channel=\"%s\": qwery(callwait): error\n", ch_gsm->alias);
									chnl->querysig.callwait = 0;
								}
#endif
								if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SERVICE) {
									ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
									ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CPIN:
							if (strstr(r_buf, "OK")) {
								if (ch_gsm->flags.pin_required) {
									ast_verbose("Polygator: GSM channel=\"%s\": PIN accepted\n", ch_gsm->alias);
									ch_gsm->flags.pin_required = 0;
									ch_gsm->flags.pin_accepted = 1;
									if (ch_gsm->flags.suspend_now) {
										// stop all timers
										memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
										ch_gsm->flags.suspend_now = 0;
										pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
										ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_SUSPEND;
										ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
										// start waitsuspend timer
										x_timer_set(ch_gsm->timers.waitsuspend, waitsuspend_timeout);
									} else if (ch_gsm->flags.sim_change) {
										pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
										ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
										// start simpoll timer
										x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
									} else {
										ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_CALL_READY;
										ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
										// start callready timer
										x_timer_set(ch_gsm->timers.callready, callready_timeout);
									}
								} else if (ch_gsm->flags.puk_required) {
									ast_verbose("Polygator: GSM channel=\"%s\": PUK accepted - PIN set to \"%s\"\n", ch_gsm->alias, ch_gsm->pin);
									pg_set_pin_by_iccid(ch_gsm->iccid, ch_gsm->pin, &ch_gsm->lock);
									ch_gsm->flags.puk_required = 0;
									ch_gsm->flags.pin_accepted = 1;
									if (ch_gsm->flags.suspend_now) {
										// stop all timers
										memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
										ch_gsm->flags.suspend_now = 0;
										pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
										ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_SUSPEND;
										ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
										// start waitsuspend timer
										x_timer_set(ch_gsm->timers.waitsuspend, waitsuspend_timeout);
									} else if (ch_gsm->flags.sim_change) {
										pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
										ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
										ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
										// start simpoll timer
										x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
									} else {
										ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_CALL_READY;
										ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
										// start callready timer
										x_timer_set(ch_gsm->timers.callready, callready_timeout);
									}
								}
							} if (strstr(r_buf, "ERROR")) {
								res = -1;
								sscanf(r_buf, "+CME ERROR: %d", &res);
								if (ch_gsm->flags.pin_required) {
									ast_verbose("Polygator: GSM channel=\"%s\": ICCID=\"%s\" PIN=\"%s\" not accepted: %s\n", ch_gsm->alias, ch_gsm->iccid?ch_gsm->iccid:"unknown", ch_gsm->pin?ch_gsm->pin:"unknown", cme_error_print(res));
								} else if (ch_gsm->flags.puk_required) {
									ast_verbose("Polygator: GSM channel=\"%s\": ICCID=\"%s\" PUK=\"%s\" not accepted: %s\n", ch_gsm->alias, ch_gsm->iccid?ch_gsm->iccid:"unknown", ch_gsm->puk?ch_gsm->puk:"unknown", cme_error_print(res));
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CLIR:
							if (strstr(r_buf, "OK")) {
								ch_gsm->clir = ch_gsm->config.clir;
								if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SERVICE) {
									ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
									ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								}
							} else if (strstr(r_buf, "ERROR")) {
								if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SERVICE) {
									ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
									ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								}
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CLVL:
							if (is_str_begin_by(r_buf, "OK"))
								ch_gsm->gainin = ch_gsm->config.gainin;
							else if (strstr(r_buf, "ERROR"))
								ch_gsm->gainin = 0;
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CUSD:
							if (strstr(r_buf, "+CUSD:")) {
								parser_ptrs.cusd_wr = (struct at_gen_cusd_write *)tmpbuf;
								if (at_gen_cusd_write_parse(r_buf, r_buf_len, parser_ptrs.cusd_wr) < 0) {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_cusd_write_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
									if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
									ch_gsm->ussd = ast_strdup("USSD response parsing error");
									if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
										if (ch_gsm->balance) ast_free(ch_gsm->balance);
										ch_gsm->balance = ast_strdup(ch_gsm->ussd);
									}
									write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
									close(ch_gsm->ussd_pipe[1]);
								} else {
									if (parser_ptrs.cusd_wr->str_len > 0) {
										if ((str0 =  get_ussd_decoded(parser_ptrs.cusd_wr->str, parser_ptrs.cusd_wr->str_len, parser_ptrs.cusd_wr->dcs))) {
											if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
											ch_gsm->ussd = ast_strdup(str0);
											if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
												if (ch_gsm->balance) ast_free(ch_gsm->balance);
												ch_gsm->balance = ast_strdup(ch_gsm->ussd);
											}
											write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
											close(ch_gsm->ussd_pipe[1]);
											free(str0);
										} else {
											if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
											ch_gsm->ussd = ast_strdup("bad response");
											if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
												if (ch_gsm->balance) ast_free(ch_gsm->balance);
												ch_gsm->balance = ast_strdup(ch_gsm->ussd);
											}
											write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
											close(ch_gsm->ussd_pipe[1]);
										}
									} else {
										if (parser_ptrs.cusd_wr->n == 0) {
											if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
											ch_gsm->ussd = ast_strdup("empty response");
											if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
												if (ch_gsm->balance) ast_free(ch_gsm->balance);
												ch_gsm->balance = ast_strdup(ch_gsm->ussd);
											}
											write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
											close(ch_gsm->ussd_pipe[1]);
										}
										else if (parser_ptrs.cusd_wr->n == 1) {
											if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
											ch_gsm->ussd = ast_strdup("response can't be presented");
											if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
												if (ch_gsm->balance) ast_free(ch_gsm->balance);
												ch_gsm->balance = ast_strdup(ch_gsm->ussd);
											}
											write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
											close(ch_gsm->ussd_pipe[1]);
										}
										else if (parser_ptrs.cusd_wr->n == 2) {
											if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
											ch_gsm->ussd = ast_strdup("bad request");
											if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
												if (ch_gsm->balance) ast_free(ch_gsm->balance);
												ch_gsm->balance = ast_strdup(ch_gsm->ussd);
											}
											write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
											close(ch_gsm->ussd_pipe[1]);
										}
									}
								}
							}
							else if(strstr(r_buf, "OK")) {
								if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SERVICE) {
									ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
									ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								}
							}
							else if (strstr(r_buf, "ERROR")) {
								if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
								ch_gsm->ussd = ast_strdup("AT command error");
								if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
									if (ch_gsm->balance) ast_free(ch_gsm->balance);
									ch_gsm->balance = ast_strdup(ch_gsm->ussd);
								}
								write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
								close(ch_gsm->ussd_pipe[1]);
								if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SERVICE) {
									ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
									ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								}								
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CMGR:
#if 0
							if (ch_gsm->at_cmd->sub_cmd == PG_AT_SUBCMD_CMGR_USER) {
								if(strstr(r_buf, "+CMGR:")){
									parser_ptrs.cmgr_wr = (struct at_gen_cmgr_write *)tmpbuf;
									if(at_gen_cmgr_write_parse(r_buf, r_buf_len, chnl->parser_ptrs.cmgr_wr) < 0){
										ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_cmgr_write_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
										//
										ast_copy_string(chnl->sms_user_pdubuf, "message read error", 1024);
										chnl->sms_user_pdulen = strlen("message read error");
										chnl->sms_user_done = 1;
										}
									else
										chnl->sms_user_length = chnl->parser_ptrs.cmgr_wr->length;
									}
								else if(is_str_xdigit(r_buf)){ // PDU data
									//
									if(r_len < 1024){
										memcpy(chnl->sms_user_pdubuf, r_buf, r_len);
										chnl->sms_user_pdulen = r_len;
										chnl->sms_user_valid = 1;
										}
									else
										ast_log(LOG_WARNING, "GSM channel=\"%s\": pdu buffer too long=%d\n", ch_gsm->alias, r_len);
									}
								else if(strstr(r_buf, "OK")){
									//
									if(!chnl->sms_user_valid){
										ast_copy_string(chnl->sms_user_pdubuf, "empty memory slot", 1024);
										chnl->sms_user_pdulen = strlen("empty memory slot");
										}
									chnl->sms_user_done = 1;
									}
								else if(strstr(r_buf, "ERROR")){
									//
									ast_copy_string(chnl->sms_user_pdubuf, "message read error", 1024);
									chnl->sms_user_pdulen = strlen("message read error");
									chnl->sms_user_done = 1;
									}
								}
#endif
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case AT_CMGS:
							if (strstr(r_buf, "+CMGS:")) {
								if ((str0 = strchr(r_buf, SP))) {
									str0++;
									if (is_str_digit(str0)) {
										res = atoi(str0);
										// copy sent pdu from preparing to sent
										char *p2s_owner = NULL;
										int p2s_scatype = 0;
										char *p2s_scaname = NULL;
										int p2s_datype = 0;
										char *p2s_daname = NULL;
										int p2s_dcs = 0;
										int p2s_partid = 0;
										int p2s_partof = 0;
										int p2s_part = 0;
										int p2s_submitpdulen = 0;
										char *p2s_submitpdu = NULL;
										int p2s_attempt = 0;
										int p2s_flash = 0;
										char *p2s_hash = NULL;
										char *p2s_content = NULL;
										int msgid;
										int maxmsgid;
										// get info from preparing
										ast_mutex_lock(&pg_sms_db_lock);
										str0 = sqlite3_mprintf("SELECT owner,scatype,scaname,datype,daname,dcs,partid,partof,part,submitpdulen,submitpdu,attempt,hash,flash,content FROM '%q-preparing' WHERE msgno=%d;", ch_gsm->iccid, ch_gsm->pdu_send_id);
										while (1)
										{
											res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
											if (res == SQLITE_OK) {
												row = 0;
												while (1)
												{
													res = sqlite3_step(sql0);
													if (res == SQLITE_ROW) {
														row++;
														p2s_owner = (sqlite3_column_text(sql0, 0))?(ast_strdup((char *)sqlite3_column_text(sql0, 0))):("unknown");
														p2s_scatype = sqlite3_column_int(sql0, 1);
														p2s_scaname = (sqlite3_column_text(sql0, 2))?(ast_strdup((char *)sqlite3_column_text(sql0, 2))):("unknown");
														p2s_datype = sqlite3_column_int(sql0, 3);
														p2s_daname = (sqlite3_column_text(sql0, 4))?(ast_strdup((char *)sqlite3_column_text(sql0, 4))):("unknown");
														p2s_dcs = sqlite3_column_int(sql0, 5);
														p2s_partid = sqlite3_column_int(sql0, 6);
														p2s_partof = sqlite3_column_int(sql0, 7);
														p2s_part = sqlite3_column_int(sql0, 8);
														p2s_submitpdulen = sqlite3_column_int(sql0, 9);
														p2s_submitpdu = (sqlite3_column_text(sql0, 10))?(ast_strdup((char *)sqlite3_column_text(sql0, 10))):("unknown");
														p2s_attempt = sqlite3_column_int(sql0, 11);
														p2s_hash = (sqlite3_column_text(sql0, 12))?(ast_strdup((char *)sqlite3_column_text(sql0, 12))):("unknown");
														p2s_flash = sqlite3_column_int(sql0, 13);
														p2s_content = (sqlite3_column_text(sql0, 14))?(ast_strdup((char *)sqlite3_column_text(sql0, 14))):("unknown");
													} else if(res == SQLITE_DONE)
														break;
													else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
													}
												sqlite3_finalize(sql0);
												break;
											} else if (res == SQLITE_BUSY) {
												ast_mutex_unlock(&ch_gsm->lock);
												usleep(1000);
												ast_mutex_lock(&ch_gsm->lock);
												continue;
											} else {
												ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
												break;
											}
										}
										sqlite3_free(str0);
										ast_mutex_unlock(&pg_sms_db_lock);
										// get msgid from sent
										maxmsgid = msgid = 0;
										ast_mutex_lock(&pg_sms_db_lock);
										str0 = sqlite3_mprintf("SELECT msgid,hash FROM '%q-sent';", ch_gsm->iccid);
										while (1)
										{
											res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
											if (res == SQLITE_OK) {
												row = 0;
												while (1)
												{
													res = sqlite3_step(sql0);
													if (res == SQLITE_ROW) {
														row++;
														maxmsgid = mmax(sqlite3_column_int(sql0, 0), maxmsgid);
														if ((p2s_hash) && (sqlite3_column_text(sql0, 1)) && (!strcmp(p2s_hash, (char *)sqlite3_column_text(sql0, 1)))) {
															msgid = sqlite3_column_int(sql0, 0);
															break;
														}
													} else if (res == SQLITE_DONE)
														break;
													else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
												}
												sqlite3_finalize(sql0);
												break;
											} else if (res == SQLITE_BUSY) {
												ast_mutex_unlock(&ch_gsm->lock);
												usleep(1000);
												ast_mutex_lock(&ch_gsm->lock);
												continue;
											} else {
												ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
												break;
											}
										}
										sqlite3_free(str0);
										ast_mutex_unlock(&pg_sms_db_lock);
										//
										if (!msgid) msgid = maxmsgid + 1;
										// write pdu to sent database
										ast_mutex_lock(&pg_sms_db_lock);
										str0 = sqlite3_mprintf("INSERT INTO '%q-sent' ("
															"owner, " // TEXT
															"msgid, " // INTEGER
															"status, " // INTEGER
															"mr, " // INTEGER
															"scatype, " // INTEGER
															"scaname, " // TEXT
															"datype, " // INTEGER
															"daname, " //
															"dcs, " // INTEGER
															"sent, " // INTEGER
															"received, " // INTEGER
															"partid, " // INTEGER
															"partof, " // INTEGER
															"part, " // INTEGER
															"submitpdulen, " // INTEGER
															"submitpdu, " // TEXT
															"attempt, " // INTEGER
															"hash, " // VARCHAR(32)
															"flash, " // INTEGER
															"content" // TEXT
														") VALUES ("
															"'%q', " // owner TEXT
															"%d, " // msgid INTEGER
															"%d, " // status INTEGER
															"%d, " // mr INTEGER
															"%d, " // scatype INTEGER
															"'%q', " // scaname TEXT
															"%d, " // datype INTEGER
															"'%q', " // daname TEXT
															"%d, " // dcs INTEGER
															"%ld, " // sent INTEGER
															"%ld, " // received INTEGER
															"%d, " // partid INTEGER
															"%d, " // partof INTEGER
															"%d, " // part INTEGER
															"%d, " // submitpdulen INTEGER
															"'%q', " // submitpdu TEXT
															"%d, " // attempt INTEGER
															"'%q', " // hash VARCHAR(32)
															"%d, " // flash INTEGER
															"'%q'" // content TEXT
															");",
															ch_gsm->iccid,
															p2s_owner, // owner TEXT
															msgid, // msgid INTEGER
															0, // status INTEGER
															res, // mr INTEGER
															p2s_scatype, // scatype INTEGER
															p2s_scaname, // scaname TEXT
															p2s_datype, // datype INTEGER
															p2s_daname, // daname TEXT
															p2s_dcs, // dcs INTEGER
															curr_tv.tv_sec, // sent INTEGER
															0, // received INTEGER
															p2s_partid, // partid INTEGER
															p2s_partof, // partof INTEGER
															p2s_part, // part INTEGER
															p2s_submitpdulen, // submitpdulen INTEGER
															p2s_submitpdu, // submitpdu TEXT
															p2s_attempt, // attempt INTEGER
															p2s_hash, // hash VARCHAR(32)
															p2s_flash, // flash INTEGER
															p2s_content); // submitpdu TEXT
										while (1)
										{
											res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
											if (res == SQLITE_OK) {
												row = 0;
												while (1)
												{
													res = sqlite3_step(sql0);
													if (res == SQLITE_ROW)
														row++;
													else if (res == SQLITE_DONE)
														break;
													else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
												}
												sqlite3_finalize(sql0);
												break;
											} else if (res == SQLITE_BUSY) {
												ast_mutex_unlock(&ch_gsm->lock);
												usleep(1000);
												ast_mutex_lock(&ch_gsm->lock);
												continue;
											} else {
												ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
												break;
											}
										}
										sqlite3_free(str0);
										ast_mutex_unlock(&pg_sms_db_lock);
										//
										if (p2s_owner) ast_free(p2s_owner);
										if (p2s_scaname) ast_free(p2s_scaname);
										if (p2s_daname) ast_free(p2s_daname);
										if (p2s_submitpdu) ast_free(p2s_submitpdu);
										if (p2s_hash) ast_free(p2s_hash);
										if (p2s_content) ast_free(p2s_content);
										// delete pdu from preparing
										ast_mutex_lock(&pg_sms_db_lock);
										str0 = sqlite3_mprintf("DELETE FROM '%q-preparing' WHERE msgno=%d;", ch_gsm->iccid, ch_gsm->pdu_send_id);
										while (1)
										{
											res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
											if (res == SQLITE_OK) {
												row = 0;
												while (1)
												{
													res = sqlite3_step(sql0);
													if (res == SQLITE_ROW)
														row++;
													else if (res == SQLITE_DONE)
														break;
													else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
												}
												sqlite3_finalize(sql0);
												break;
											} else if (res == SQLITE_BUSY) {
												ast_mutex_unlock(&ch_gsm->lock);
												usleep(1000);
												ast_mutex_lock(&ch_gsm->lock);
												continue;
											} else {
												ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
												break;
											}
										}
										sqlite3_free(str0);
										ast_mutex_unlock(&pg_sms_db_lock);
										// set new zero interval if is multipart message
										if (p2s_part != p2s_partof) {
											x_timer_set(ch_gsm->timers.smssend, zero_timeout);
											ast_verb(9, "GSM channel=\"%s\": set msec timeout om sms send timer\n", ch_gsm->alias);
										}
									}
								}
								ch_gsm->pdu_send_id = 0;
							} else if (strstr(r_buf, "ERROR")) {
								if ((str0 = strrchr(r_buf, SP))) {
									str0++;
									if (is_str_digit(str0))
										ast_log(LOG_ERROR, "GSM channel=\"%s\": message send error: %s\n", ch_gsm->alias, cms_error_print(atoi(str0)));
									else
										ast_log(LOG_ERROR, "GSM channel=\"%s\": message send error: %s\n", ch_gsm->alias, r_buf);
									// check for send attempt count
									if (((ch_gsm->pdu_send_attempt+1) >= ch_gsm->config.sms_send_attempt) || atoi(str0) == CMS_ERROR_PS_BUSY) {
										// copy pdu from preparing to sent
										char *assembled_content;
										int assembled_content_len;
										char assembled_destination[MAX_ADDRESS_LENGTH];
										//
										char *p2d_owner = NULL;
										int p2d_partof = 0;
										int p2d_part = 0;
										char *p2d_hash = NULL;
										int p2d_flash = 0;
										// allocating memory for content assembling
										assembled_content = ast_calloc(ch_gsm->config.sms_max_part, 640);
										// get info from current preparing PDU
										ast_mutex_lock(&pg_sms_db_lock);
										str0 = sqlite3_mprintf("SELECT owner,datype,daname,partof,hash,flash FROM '%q-preparing' WHERE msgno=%d;", ch_gsm->iccid, ch_gsm->pdu_send_id);
										while (1)
										{
											res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
											if (res == SQLITE_OK) {
												row = 0;
												while (1)
												{
													res = sqlite3_step(sql0);
													if (res == SQLITE_ROW) {
														row++;
														p2d_owner = ast_strdup((char *)sqlite3_column_text(sql0, 0));
														sprintf(assembled_destination, "%s%s", (sqlite3_column_int(sql0, 1)==145)?("+"):(""), sqlite3_column_text(sql0, 2));
														p2d_partof = sqlite3_column_int(sql0, 3);
														p2d_hash = ast_strdup((char *)sqlite3_column_text(sql0, 4));
														p2d_flash = sqlite3_column_int(sql0, 5);
													} else if (res == SQLITE_DONE)
														break;
													else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
												}
												sqlite3_finalize(sql0);
												break;
											} else if (res == SQLITE_BUSY) {
												ast_mutex_unlock(&ch_gsm->lock);
												usleep(1000);
												ast_mutex_lock(&ch_gsm->lock);
												continue;
											} else {
												ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
												break;
											}
										}
										sqlite3_free(str0);
										ast_mutex_unlock(&pg_sms_db_lock);
										// search all parts of this message
										if (p2d_owner && p2d_hash) {
											int traverse_continue;
											assembled_content_len = 0;
											// tracking through all parts in all list
											for (p2d_part=1; p2d_part<=p2d_partof; p2d_part++)
											{
												traverse_continue = 0;
												// preparing
												ast_mutex_lock(&pg_sms_db_lock);
												str0 = sqlite3_mprintf("SELECT content FROM '%q-preparing' WHERE part=%d AND hash='%q';", ch_gsm->iccid, p2d_part, p2d_hash);
												while (1)
												{
													res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
													if (res == SQLITE_OK) {
														row = 0;
														while (1)
														{
															res = sqlite3_step(sql0);
															if (res == SQLITE_ROW) {
																row++;
																if (row == 1) {
																	if (assembled_content)
																		assembled_content_len += sprintf(assembled_content+assembled_content_len, "%s", sqlite3_column_text(sql0, 0));
																} else
																	ast_log(LOG_ERROR, "GSM channel=\"%s\": duplicated(%d) part=%d of message \"%s\"\n", ch_gsm->alias, row, p2d_part, p2d_hash);
															} else if (res == SQLITE_DONE)
																break;
															else if (res == SQLITE_BUSY) {
																ast_mutex_unlock(&ch_gsm->lock);
																usleep(1000);
																ast_mutex_lock(&ch_gsm->lock);
																continue;
															} else {
																ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
																break;
															}
														}
														// check result
														if (row) {
															// delete from preparing list
															str1 = sqlite3_mprintf("DELETE FROM '%q-preparing' WHERE part=%d AND hash='%q';", ch_gsm->iccid, p2d_part, p2d_hash);
															while(1){
																res = sqlite3_prepare_fun(pg_sms_db, str1, strlen(str1), &sql1, NULL);
																if (res == SQLITE_OK) {
																	row = 0;
																	while (1)
																	{
																		res = sqlite3_step(sql1);
																		if (res == SQLITE_ROW)
																			row++;
																		else if (res == SQLITE_DONE)
																			break;
																		else if (res == SQLITE_BUSY) {
																			ast_mutex_unlock(&ch_gsm->lock);
																			usleep(1000);
																			ast_mutex_lock(&ch_gsm->lock);
																			continue;
																		} else {
																			ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
																			break;
																		}
																	}
																	sqlite3_finalize(sql1);
																	break;
																} else if (res == SQLITE_BUSY) {
																	ast_mutex_unlock(&ch_gsm->lock);
																	usleep(1000);
																	ast_mutex_lock(&ch_gsm->lock);
																	continue;
																} else {
																	ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
																	break;
																}
															}
															sqlite3_free(str1);
															// to next step
															traverse_continue = 1;
														}
														sqlite3_finalize(sql0);
														break;
													} else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
												}
												sqlite3_free(str0);
												ast_mutex_unlock(&pg_sms_db_lock);
												if (traverse_continue) continue;
												// sent
												ast_mutex_lock(&pg_sms_db_lock);
												str0 = sqlite3_mprintf("SELECT content FROM '%q-sent' WHERE part=%d AND hash='%q';", ch_gsm->iccid, p2d_part, p2d_hash);
												while (1)
												{
													res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
													if (res == SQLITE_OK) {
														row = 0;
														while (1)
														{
															res = sqlite3_step(sql0);
															if(res == SQLITE_ROW){
																row++;
																if (row == 1){
																	if (assembled_content)
																		assembled_content_len += sprintf(assembled_content+assembled_content_len, "%s", sqlite3_column_text(sql0, 0));
																} else
																	ast_log(LOG_ERROR, "GSM channel=\"%s\": duplicated(%d) part=%d of message \"%s\"\n", ch_gsm->alias, row, p2d_part, p2d_hash);
															} else if (res == SQLITE_DONE)
																break;
															else if (res == SQLITE_BUSY) {
																ast_mutex_unlock(&ch_gsm->lock);
																usleep(1000);
																ast_mutex_lock(&ch_gsm->lock);
																continue;
															} else {
																ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
																break;
															}
														}
														// check result
														if (row) {
															// delete from preparing list
															str1 = sqlite3_mprintf("DELETE FROM '%q-sent' WHERE part=%d AND hash='%q';", ch_gsm->iccid, p2d_part, p2d_hash);
															while (1)
															{
																res = sqlite3_prepare_fun(pg_sms_db, str1, strlen(str1), &sql1, NULL);
																if (res == SQLITE_OK) {
																	row = 0;
																	while (1)
																	{
																		res = sqlite3_step(sql1);
																		if (res == SQLITE_ROW)
																			row++;
																		else if (res == SQLITE_DONE)
																			break;
																		else if (res == SQLITE_BUSY) {
																			ast_mutex_unlock(&ch_gsm->lock);
																			usleep(1000);
																			ast_mutex_lock(&ch_gsm->lock);
																			continue;
																		} else {
																			ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
																			break;
																		}
																	}
																	sqlite3_finalize(sql1);
																	break;
																} else if(res == SQLITE_BUSY) {
																	ast_mutex_unlock(&ch_gsm->lock);
																	usleep(1000);
																	ast_mutex_lock(&ch_gsm->lock);
																	continue;
																} else {
																	ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
																	break;
																}
															}
															sqlite3_free(str1);
														}
														sqlite3_finalize(sql0);
														break;
													} else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
												}
												sqlite3_free(str0);
												ast_mutex_unlock(&pg_sms_db_lock);
											} // end of tracking section
											// move message to discard/outbox list
											if (strcmp(p2d_owner, "this")) {
												// is not owner - move to outbox
												ast_mutex_lock(&pg_sms_db_lock);
												str0 = sqlite3_mprintf("INSERT INTO '%q-outbox' ("
																				"destination, " // TEXT
																				"content, " // TEXT
																				"flash, " // INTEGER
																				"enqueued, " // INTEGER
																				"hash" // VARCHAR(32) UNIQUE
																			") VALUES ("
																				"'%q', " // destination TEXT
																				"'%q', " // content TEXT
																				"%d, " // flash INTEGER
																				"%ld, " // enqueued INTEGER
																				"'%q');", // hash  VARCHAR(32) UNIQUE
																			p2d_owner,
																			assembled_destination, // destination TEXT
																			assembled_content, // content TEXT
																			p2d_flash, // flash TEXT
																			curr_tv.tv_sec, // enqueued INTEGER
																			p2d_hash); // hash  VARCHAR(32) UNIQUE
												while (1)
												{
													res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
													if (res == SQLITE_OK) {
														row = 0;
														while (1)
														{
															res = sqlite3_step(sql0);
															if (res == SQLITE_ROW)
																row++;
															else if (res == SQLITE_DONE)
																break;
															else if (res == SQLITE_BUSY) {
																ast_mutex_unlock(&ch_gsm->lock);
																usleep(1000);
																ast_mutex_lock(&ch_gsm->lock);
																continue;
															} else {
																ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
																break;
															}
														}
														sqlite3_finalize(sql0);
														break;
													} else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
												}
												sqlite3_free(str0);
												ast_mutex_lock(&pg_sms_db_lock);
											} else {
												// is owner - move to discard
												ast_mutex_lock(&pg_sms_db_lock);
												str0 = sqlite3_mprintf("INSERT INTO '%q-discard' ("
																				"destination, " // TEXT
																				"content, " // TEXT
																				"flash, " // INTEGER
																				"cause, " // TEXT
																				"timestamp, " // INTEGER
																				"hash" // VARCHAR(32) UNIQUE
																			") VALUES ("
																				"'%q', " // destination TEXT
																				"'%q', " // content TEXT
																				"%d, " // flash INTEGER
																				"'%q', " // cause TEXT
																				"%ld, " // timestamp INTEGER
																				"'%q');", // hash  VARCHAR(32) UNIQUE
																			ch_gsm->iccid,
																			assembled_destination, // destination TEXT
																			assembled_content, // content TEXT
																			p2d_flash, // flash TEXT
																			(is_str_digit(str0))?(cms_error_print(atoi(str0))):(r_buf), // cause TEXT
																			curr_tv.tv_sec, // timestamp INTEGER
																			p2d_hash); // hash  VARCHAR(32) UNIQUE
												while (1)
												{
													res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
													if (res == SQLITE_OK) {
														row = 0;
														while (1)
														{
															res = sqlite3_step(sql0);
															if (res == SQLITE_ROW)
																row++;
															else if (res == SQLITE_DONE)
																break;
															else if (res == SQLITE_BUSY) {
																ast_mutex_unlock(&ch_gsm->lock);
																usleep(1000);
																ast_mutex_lock(&ch_gsm->lock);
																continue;
															} else {
																ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
																break;
															}
														}
														sqlite3_finalize(sql0);
														break;
													} else if (res == SQLITE_BUSY) {
														ast_mutex_unlock(&ch_gsm->lock);
														usleep(1000);
														ast_mutex_lock(&ch_gsm->lock);
														continue;
													} else {
														ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
														break;
													}
												}
												sqlite3_free(str0);
												ast_mutex_unlock(&pg_sms_db_lock);
											}
										}
										// free allocated memory
										if (assembled_content) ast_free(assembled_content);
										if (p2d_owner) ast_free(p2d_owner);
										if (p2d_hash) ast_free(p2d_hash);
									} // end of check for attempt count
								} // end of searching last SPACE
								//
								ch_gsm->pdu_send_id = 0;
							} else if(strstr(r_buf, "OK")) {
								;
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						default:
							break;
					}
				} // end of WRITE operations
				else
					ast_log(LOG_ERROR, "GSM channel=\"%s\": general AT command [%.*s] with unknown [%0X] operation\n",
							ch_gsm->alias,
							ch_gsm->at_cmd->cmd_len - 1,
							ch_gsm->at_cmd->cmd_buf,
							ch_gsm->at_cmd->oper);

				// SIM300 AT commands
				if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300) {
					// select by operation
					if (ch_gsm->at_cmd->oper == AT_OPER_EXEC) {
						// EXEC operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_SIM300_CCID:
								if (is_str_xdigit(r_buf)) {
									if (!ch_gsm->iccid) ch_gsm->iccid = ast_strdup(r_buf);
									if (ch_gsm->flags.sms_table_needed) {
										pg_sms_db_table_create(ch_gsm->iccid, &ch_gsm->lock);
										ch_gsm->flags.sms_table_needed = 0;
										// start smssend timer
										x_timer_set(ch_gsm->timers.smssend, onesec_timeout);
									}
									if (ch_gsm->flags.sim_change) {
										if (ch_gsm->iccid && ch_gsm->iccid_ban && strcmp(ch_gsm->iccid, ch_gsm->iccid_ban)) {
											// new SIM
											ch_gsm->flags.sim_change = 0;
											ch_gsm->flags.sim_test = 0;
										} else {
											// old SIM
											if (ch_gsm->flags.sim_test) {
												ast_verbose("Polygator: GSM channel=\"%s\": this SIM card used all registration attempts and already inserted\n", ch_gsm->alias);
												ch_gsm->flags.sim_test = 0;
											}
										}
									} else if (ch_gsm->flags.pin_required) {
										if (ch_gsm->pin) ast_free(ch_gsm->pin);
										if ((ch_gsm->pin = pg_get_pin_by_iccid(ch_gsm->iccid, &ch_gsm->lock))) {
											pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\"", ch_gsm->pin);
										} else {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM ICCID=\"%s\" PIN required\n", ch_gsm->alias, ch_gsm->iccid);
										}
									} else if (ch_gsm->flags.puk_required) {
										if (ch_gsm->puk) ast_free(ch_gsm->puk);
										if ((ch_gsm->puk = pg_get_puk_by_iccid(ch_gsm->iccid, &ch_gsm->lock))) {
											if (!ch_gsm->pin) ch_gsm->pin = pg_get_pin_by_iccid(ch_gsm->iccid, &ch_gsm->lock);
											if (!ch_gsm->pin) ch_gsm->pin = ast_strdup("0000");
											pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\",\"%s\"", ch_gsm->puk, ch_gsm->pin);
										} else {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM ICCID=\"%s\" PUK required\n", ch_gsm->alias, ch_gsm->iccid);
										}
									}
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					}
					else if (ch_gsm->at_cmd->oper == AT_OPER_TEST) {
						// TEST operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					}
					else if(ch_gsm->at_cmd->oper == AT_OPER_READ){
						// READ operations
						switch(ch_gsm->at_cmd->id){
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_SIM300_CMIC:
								if (strstr(r_buf, "+CMIC:")) {
									parser_ptrs.sim300_cmic_rd = (struct at_sim300_cmic_read *)tmpbuf;
									if (at_sim300_cmic_read_parse(r_buf, r_buf_len, parser_ptrs.sim300_cmic_rd) < 0) {
										// parsing error
										ast_log(LOG_ERROR, "GSM channel=\"%s\": at_sim300_cmic_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
#if 0
										// check for query
										if(chnl->querysig.gainout){
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): error\n", ch_gsm->alias);
											chnl->querysig.gainout = 0;
											}
#endif
									} else {
										ch_gsm->gainout = parser_ptrs.sim300_cmic_rd->main_mic;
#if 0
										// check for query
										if(chnl->querysig.gainout){
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): %d\n", ch_gsm->alias, chnl->gainout);
											chnl->querysig.gainout = 0;
											}
#endif
									}
								} else if (strstr(r_buf, "ERROR")) {
#if 0
									// check for query
									if(chnl->querysig.gainout){
										ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): error\n", ch_gsm->alias);
										chnl->querysig.gainout = 0;
										}
#endif
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_SIM300_CSMINS:
								if (strstr(r_buf, "+CSMINS:")) {
									// parse csmins
									parser_ptrs.sim300_csmins_rd = (struct at_sim300_csmins_read *)tmpbuf;
									if (at_sim300_csmins_read_parse(r_buf, r_buf_len, parser_ptrs.sim300_csmins_rd) < 0) {
										ast_log(LOG_ERROR, "GSM channel=\"%s\": at_sim300_csmins_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
									} else {
										if (parser_ptrs.sim300_csmins_rd->sim_inserted != ch_gsm->flags.sim_inserted) {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM %s\n", ch_gsm->alias, (parser_ptrs.sim300_csmins_rd->sim_inserted)?("inserted"):("removed"));
											if (!parser_ptrs.sim300_csmins_rd->sim_inserted) {
												// reset channel phone number
												ast_copy_string(ch_gsm->subscriber_number.value, "unknown", MAX_ADDRESS_LENGTH);
												ch_gsm->subscriber_number.length = strlen(ch_gsm->subscriber_number.value);
												ch_gsm->subscriber_number.type.bits.reserved = 1;
												ch_gsm->subscriber_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
												ch_gsm->subscriber_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
												// reset SMS center number
												ast_copy_string(ch_gsm->smsc_number.value, "unknown", MAX_ADDRESS_LENGTH);
												ch_gsm->smsc_number.length = strlen(ch_gsm->smsc_number.value);
												ch_gsm->smsc_number.type.bits.reserved = 1;
												ch_gsm->smsc_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
												ch_gsm->smsc_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;

												if (ch_gsm->operator_name) ast_free(ch_gsm->operator_name); ch_gsm->operator_name = NULL;
												if (ch_gsm->operator_code) ast_free(ch_gsm->operator_code); ch_gsm->operator_code = NULL;
												if (ch_gsm->imsi) ast_free(ch_gsm->imsi); ch_gsm->imsi = NULL;
												if (ch_gsm->iccid) ast_free(ch_gsm->iccid); ch_gsm->iccid = NULL;
// 												if (ch_gsm->pin) ast_free(ch_gsm->pin); ch_gsm->pin = NULL;
// 												if (ch_gsm->puk) ast_free(ch_gsm->puk); ch_gsm->puk = NULL;
												if (ch_gsm->config.balance_request) ast_free(ch_gsm->config.balance_request); ch_gsm->config.balance_request = NULL;

												// stop callready timer
												x_timer_stop(ch_gsm->timers.callready);
												// stop smssend timer
												x_timer_stop(ch_gsm->timers.smssend);
												ch_gsm->flags.sms_table_needed = 1;

												ch_gsm->flags.dcr_table_needed = 1;

												// reset registaration status
												ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
												// reset callwait statech_gsm->pin
												ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
												// reset clir state
												ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
												// reset rssi value
												ch_gsm->rssi = 99;
												// reset ber value
												ch_gsm->ber = 99;
// 												// reset attempts count
// 												ch_gsm->reg_try_count = ch_gsm->config.reg_try_count;
												if(ch_gsm->flags.sim_change)
													ch_gsm->flags.sim_test = 1;
												// start simpoll timer
												if(ch_gsm->state != PG_CHANNEL_GSM_STATE_SUSPEND)
													x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
											}
										}
										ch_gsm->flags.sim_inserted = parser_ptrs.sim300_csmins_rd->sim_inserted;
									}
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					} else if (ch_gsm->at_cmd->oper == AT_OPER_WRITE) {
						// WRITE operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_SIM300_CMIC:
								if (is_str_begin_by(r_buf, "OK"))
									ch_gsm->gainout = ch_gsm->config.gainout;
								else if (strstr(r_buf, "ERROR"))
									ch_gsm->gainout = 0;
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					}
				} // end of SIM300 AT commands

				// SIM900 AT commands
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900) {
					// select by operation
					if (ch_gsm->at_cmd->oper == AT_OPER_EXEC) {
						// EXEC operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_SIM900_CCID:
								if (is_str_xdigit(r_buf)) {
									if (!ch_gsm->iccid) ch_gsm->iccid = ast_strdup(r_buf);
									if (ch_gsm->flags.sms_table_needed) {
										pg_sms_db_table_create(ch_gsm->iccid, &ch_gsm->lock);
										ch_gsm->flags.sms_table_needed = 0;
										// start smssend timer
										x_timer_set(ch_gsm->timers.smssend, onesec_timeout);
									}
									if (ch_gsm->flags.sim_change) {
										if (ch_gsm->iccid && ch_gsm->iccid_ban && strcmp(ch_gsm->iccid, ch_gsm->iccid_ban)) {
											// new SIM
											ch_gsm->flags.sim_change = 0;
											ch_gsm->flags.sim_test = 0;
										} else {
											// old SIM
											if (ch_gsm->flags.sim_test) {
												ast_verbose("Polygator: GSM channel=\"%s\": this SIM card used all registration attempts and already inserted\n", ch_gsm->alias);
												ch_gsm->flags.sim_test = 0;
											}
										}
									} else if (ch_gsm->flags.pin_required) {
										if (ch_gsm->pin) ast_free(ch_gsm->pin);
										if ((ch_gsm->pin = pg_get_pin_by_iccid(ch_gsm->iccid, &ch_gsm->lock))) {
											pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\"", ch_gsm->pin);
										} else {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM ICCID=\"%s\" PIN required\n", ch_gsm->alias, ch_gsm->iccid);
										}
									} else if (ch_gsm->flags.puk_required) {
										if (ch_gsm->puk) ast_free(ch_gsm->puk);
										if ((ch_gsm->puk = pg_get_puk_by_iccid(ch_gsm->iccid, &ch_gsm->lock))) {
											if (!ch_gsm->pin) ch_gsm->pin = pg_get_pin_by_iccid(ch_gsm->iccid, &ch_gsm->lock);
											if (!ch_gsm->pin) ch_gsm->pin = ast_strdup("0000");
											pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\",\"%s\"", ch_gsm->puk, ch_gsm->pin);
										} else {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM ICCID=\"%s\" PUK required\n", ch_gsm->alias, ch_gsm->iccid);
										}
									}
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
							}
					} else if (ch_gsm->at_cmd->oper == AT_OPER_TEST) {
						// TEST operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					} else if(ch_gsm->at_cmd->oper == AT_OPER_READ) {
						// READ operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_SIM900_CMIC:
								if (strstr(r_buf, "+CMIC:")) {
									parser_ptrs.sim900_cmic_rd = (struct at_sim900_cmic_read *)tmpbuf;
									if (at_sim900_cmic_read_parse(r_buf, r_buf_len, parser_ptrs.sim900_cmic_rd) < 0) {
										// parsing error
										ast_log(LOG_ERROR, "GSM channel=\"%s\": at_sim900_cmic_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
										r_buf[0] = '\0';
										r_cptr = r_buf;
										r_buf_len = 0;
										r_buf_valid = 0;
										r_buf_active = 0;
#if 0
										// check for query
										if(chnl->querysig.gainout){
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): error\n", ch_gsm->alias);
											chnl->querysig.gainout = 0;
											}
#endif
									} else {
										ch_gsm->gainout = parser_ptrs.sim900_cmic_rd->main_hs_mic;
#if 0
										// check for query
										if(chnl->querysig.gainout){
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): %d\n", ch_gsm->alias, chnl->gainout_curr);
											chnl->querysig.gainout = 0;
											}
#endif
									}
								} else if (strstr(r_buf, "ERROR")) {
#if 0									
									// check for query
									if(chnl->querysig.gainout){
										ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): error\n", ch_gsm->alias);
										chnl->querysig.gainout = 0;
										}
#endif
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_SIM900_CSMINS:
								if (strstr(r_buf, "+CSMINS:")) {
									// parse csmins
									parser_ptrs.sim900_csmins_rd = (struct at_sim900_csmins_read *)tmpbuf;
									if (at_sim900_csmins_read_parse(r_buf, r_buf_len, parser_ptrs.sim900_csmins_rd) < 0) {
										ast_log(LOG_ERROR, "GSM channel=\"%s\": at_sim900_csmins_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
									} else {
										if (parser_ptrs.sim900_csmins_rd->sim_inserted != ch_gsm->flags.sim_inserted) {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM %s\n", ch_gsm->alias, (parser_ptrs.sim900_csmins_rd->sim_inserted)?("inserted"):("removed"));
											if (!parser_ptrs.sim900_csmins_rd->sim_inserted) {
												// reset channel phone number
												ast_copy_string(ch_gsm->subscriber_number.value, "unknown", MAX_ADDRESS_LENGTH);
												ch_gsm->subscriber_number.length = strlen(ch_gsm->subscriber_number.value);
												ch_gsm->subscriber_number.type.bits.reserved = 1;
												ch_gsm->subscriber_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
												ch_gsm->subscriber_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
												// reset SMS center number
												ast_copy_string(ch_gsm->smsc_number.value, "unknown", MAX_ADDRESS_LENGTH);
												ch_gsm->smsc_number.length = strlen(ch_gsm->smsc_number.value);
												ch_gsm->smsc_number.type.bits.reserved = 1;
												ch_gsm->smsc_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
												ch_gsm->smsc_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;

												if (ch_gsm->operator_name) ast_free(ch_gsm->operator_name); ch_gsm->operator_name = NULL;
												if (ch_gsm->operator_code) ast_free(ch_gsm->operator_code); ch_gsm->operator_code = NULL;
												if (ch_gsm->imsi) ast_free(ch_gsm->imsi); ch_gsm->imsi = NULL;
												if (ch_gsm->iccid) ast_free(ch_gsm->iccid); ch_gsm->iccid = NULL;
// 												if (ch_gsm->pin) ast_free(ch_gsm->pin); ch_gsm->pin = NULL;
// 												if (ch_gsm->puk) ast_free(ch_gsm->puk); ch_gsm->puk = NULL;
												if (ch_gsm->config.balance_request) ast_free(ch_gsm->config.balance_request); ch_gsm->config.balance_request = NULL;

												// stop callready timer
												x_timer_stop(ch_gsm->timers.callready);
												// stop smssend timer
												x_timer_stop(ch_gsm->timers.smssend);
												ch_gsm->flags.sms_table_needed = 1;

												ch_gsm->flags.dcr_table_needed = 1;

												// reset registaration status
												ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
												// reset callwait state
												ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
												// reset clir state
												ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
												// reset rssi value
												ch_gsm->rssi = 99;
												// reset ber value
												ch_gsm->ber = 99;
// 												// reset attempts count
// 												ch_gsm->reg_try_count = ch_gsm->config.reg_try_count;
												if(ch_gsm->flags.sim_change)
													ch_gsm->flags.sim_test = 1;
												// start simpoll timer
												if(ch_gsm->state != PG_CHANNEL_GSM_STATE_SUSPEND)
													x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
											}
										}
										ch_gsm->flags.sim_inserted = parser_ptrs.sim900_csmins_rd->sim_inserted;
									}
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					} else if (ch_gsm->at_cmd->oper == AT_OPER_WRITE) {
						// WRITE operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_SIM900_CMIC:
								if (is_str_begin_by(r_buf, "OK"))
									ch_gsm->gainout = ch_gsm->config.gainout;
								else if (strstr(r_buf, "ERROR"))
									ch_gsm->gainout = 0;
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					}
				} // end of SIM900 AT commands

				// M10 AT commands
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10) {
					// select by operation
					if (ch_gsm->at_cmd->oper == AT_OPER_EXEC) {
						// EXEC operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_M10_QCCID:
								if (is_str_xdigit(r_buf)) {
									if (!ch_gsm->iccid) ch_gsm->iccid = ast_strdup(r_buf);
									if (ch_gsm->flags.sms_table_needed) {
										pg_sms_db_table_create(ch_gsm->iccid, &ch_gsm->lock);
										ch_gsm->flags.sms_table_needed = 0;
										// start smssend timer
										x_timer_set(ch_gsm->timers.smssend, onesec_timeout);
									}
									if (ch_gsm->flags.sim_change) {
										if (ch_gsm->iccid && ch_gsm->iccid_ban && strcmp(ch_gsm->iccid, ch_gsm->iccid_ban)) {
											// new SIM
											ch_gsm->flags.sim_change = 0;
											ch_gsm->flags.sim_test = 0;
										} else {
											// old SIM
											if (ch_gsm->flags.sim_test) {
												ast_verbose("Polygator: GSM channel=\"%s\": this SIM card used all registration attempts and already inserted\n", ch_gsm->alias);
												ch_gsm->flags.sim_test = 0;
											}
										}
									} else if (ch_gsm->flags.pin_required) {
										if (ch_gsm->pin) ast_free(ch_gsm->pin);
										if ((ch_gsm->pin = pg_get_pin_by_iccid(ch_gsm->iccid, &ch_gsm->lock))) {
											pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\"", ch_gsm->pin);
										} else {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM ICCID=\"%s\" PIN required\n", ch_gsm->alias, ch_gsm->iccid);
										}
									} else if (ch_gsm->flags.puk_required) {
										if (ch_gsm->puk) ast_free(ch_gsm->puk);
										if ((ch_gsm->puk = pg_get_puk_by_iccid(ch_gsm->iccid, &ch_gsm->lock))) {
											if (!ch_gsm->pin) ch_gsm->pin = pg_get_pin_by_iccid(ch_gsm->iccid, &ch_gsm->lock);
											if (!ch_gsm->pin) ch_gsm->pin = ast_strdup("0000");
											pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\",\"%s\"", ch_gsm->puk, ch_gsm->pin);
										} else {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM ICCID=\"%s\" PUK required\n", ch_gsm->alias, ch_gsm->iccid);
										}
									}
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					}
					else if (ch_gsm->at_cmd->oper == AT_OPER_TEST) {
						// TEST operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					}
					else if (ch_gsm->at_cmd->oper == AT_OPER_READ) {
						// READ operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_M10_QMIC:
								if (strstr(r_buf, "+QMIC:")) {
									parser_ptrs.m10_qmic_rd = (struct at_m10_qmic_read *)tmpbuf;
									if (at_m10_qmic_read_parse(r_buf, r_buf_len, parser_ptrs.m10_qmic_rd) < 0) {
										ast_log(LOG_ERROR, "GSM channel=\"%s\": at_m10_qmic_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
#if 0
										// check for query
										if(chnl->querysig.gainout){
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): error\n", ch_gsm->alias);
											chnl->querysig.gainout = 0;
											}
#endif
									} else {
										ch_gsm->gainout = parser_ptrs.m10_qmic_rd->headset_mic;
#if 0
										// check for query
										if(chnl->querysig.gainout){
											ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): %d\n", ch_gsm->alias, chnl->gainout_curr);
											chnl->querysig.gainout = 0;
											}
#endif
									}
								} else if (strstr(r_buf, "ERROR")) {
#if 0
									// check for query
									if(chnl->querysig.gainout){
										ast_verbose("Polygator: GSM channel=\"%s\": qwery(gainout): error\n", ch_gsm->alias);
										chnl->querysig.gainout = 0;
										}
#endif
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_M10_QSIMSTAT:
								if (strstr(r_buf, "+QSIMSTAT:")) {
									// parse csmins
									parser_ptrs.m10_qsimstat_rd = (struct at_m10_qsimstat_read *)tmpbuf;
									if (at_m10_qsimstat_read_parse(r_buf, r_buf_len, parser_ptrs.m10_qsimstat_rd) < 0) {
										ast_log(LOG_ERROR, "GSM channel=\"%s\": at_m10_qsimstat_read_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
									} else {
										if(parser_ptrs.m10_qsimstat_rd->sim_inserted != ch_gsm->flags.sim_inserted) {
											ast_verbose("Polygator: GSM channel=\"%s\": SIM %s\n", ch_gsm->alias, (parser_ptrs.m10_qsimstat_rd->sim_inserted)?("inserted"):("removed"));
											if (!parser_ptrs.m10_qsimstat_rd->sim_inserted) {
												// reset channel phone number
												ast_copy_string(ch_gsm->subscriber_number.value, "unknown", MAX_ADDRESS_LENGTH);
												ch_gsm->subscriber_number.length = strlen(ch_gsm->subscriber_number.value);
												ch_gsm->subscriber_number.type.bits.reserved = 1;
												ch_gsm->subscriber_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
												ch_gsm->subscriber_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
												// reset SMS center number
												ast_copy_string(ch_gsm->smsc_number.value, "unknown", MAX_ADDRESS_LENGTH);
												ch_gsm->smsc_number.length = strlen(ch_gsm->smsc_number.value);
												ch_gsm->smsc_number.type.bits.reserved = 1;
												ch_gsm->smsc_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
												ch_gsm->smsc_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;

												if (ch_gsm->operator_name) ast_free(ch_gsm->operator_name); ch_gsm->operator_name = NULL;
												if (ch_gsm->operator_code) ast_free(ch_gsm->operator_code); ch_gsm->operator_code = NULL;
												if (ch_gsm->imsi) ast_free(ch_gsm->imsi); ch_gsm->imsi = NULL;
												if (ch_gsm->iccid) ast_free(ch_gsm->iccid); ch_gsm->iccid = NULL;
// 												if (ch_gsm->pin) ast_free(ch_gsm->pin); ch_gsm->pin = NULL;
// 												if (ch_gsm->puk) ast_free(ch_gsm->puk); ch_gsm->puk = NULL;
												if (ch_gsm->config.balance_request) ast_free(ch_gsm->config.balance_request); ch_gsm->config.balance_request = NULL;

												// stop callready timer
												x_timer_stop(ch_gsm->timers.callready);
												// stop smssend timer
												x_timer_stop(ch_gsm->timers.smssend);
												ch_gsm->flags.sms_table_needed = 1;

												ch_gsm->flags.dcr_table_needed = 1;

												// reset registaration status
												ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
												// reset callwait state
												ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
												// reset clir state
												ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
												// reset rssi value
												ch_gsm->rssi = 99;
												// reset ber value
												ch_gsm->ber = 99;
// 												// reset attempts count
// 												ch_gsm->reg_try_count = ch_gsm->config.reg_try_count;
												if(ch_gsm->flags.sim_change)
													ch_gsm->flags.sim_test = 1;
												// start simpoll timer
												if(ch_gsm->state != PG_CHANNEL_GSM_STATE_SUSPEND)
													x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
											}
										}
										ch_gsm->flags.sim_inserted = parser_ptrs.m10_qsimstat_rd->sim_inserted;
									}
								}
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					} else if (ch_gsm->at_cmd->oper == AT_OPER_WRITE) {
						// WRITE operations
						switch (ch_gsm->at_cmd->id)
						{
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							case AT_M10_QMIC:
								if (is_str_begin_by(r_buf, "OK"))
									ch_gsm->gainout = ch_gsm->config.gainout;
								else if (strstr(r_buf, "ERROR"))
									ch_gsm->gainout = 0;
								break;
							//++++++++++++++++++++++++++++++++++++++++++++++++++
							default:
								break;
						}
					}
				} // end of M10 AT commands
				// test for command done
				if (ch_gsm->cmd_done) {
					// if done -> remove from queue
					ast_free(ch_gsm->at_cmd);
					ch_gsm->at_cmd = NULL;
					r_buf_active = 0;
				}
				r_buf[0] = '\0';
				r_cptr = r_buf;
				r_buf_len = 0;
				r_buf_valid = 0;
			} else {
				if (ch_gsm->debug.receiver) {
					ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
					if (ch_gsm->debug.receiver_debug_fp) {
						if((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
							fprintf(ch_gsm->debug.receiver_debug_fp, "\n[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT UNSOLICITED RESPONSE - [%s]\n",
													tm_ptr->tm_year + 1900,
													tm_ptr->tm_mon+1,
													tm_ptr->tm_mday,
													tm_ptr->tm_hour,
													tm_ptr->tm_min,
													tm_ptr->tm_sec,
													curr_tv.tv_usec,
													r_buf);
						else
							fprintf(ch_gsm->debug.receiver_debug_fp, "\n[%ld.%06ld] AT UNSOLICITED RESPONSE - [%s]\n",
													curr_tv.tv_sec,
													curr_tv.tv_usec,
													r_buf);
						fflush(ch_gsm->debug.receiver_debug_fp);
						fclose(ch_gsm->debug.receiver_debug_fp);
						ch_gsm->debug.receiver_debug_fp = NULL;
					}
				}
				if (ch_gsm->debug.at) {
					ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
					if (ch_gsm->debug.at_debug_fp) {
						if((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
							fprintf(ch_gsm->debug.at_debug_fp, "[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT UNSOLICITED RESPONSE - [%s]\n",
													tm_ptr->tm_year + 1900,
													tm_ptr->tm_mon+1,
													tm_ptr->tm_mday,
													tm_ptr->tm_hour,
													tm_ptr->tm_min,
													tm_ptr->tm_sec,
													curr_tv.tv_usec,
													r_buf);
						else
							fprintf(ch_gsm->debug.at_debug_fp, "[%ld.%06ld] AT UNSOLICITED RESPONSE - [%s]\n",
													curr_tv.tv_sec,
													curr_tv.tv_usec,
													r_buf);
						fflush(ch_gsm->debug.at_debug_fp);
						fclose(ch_gsm->debug.at_debug_fp);
						ch_gsm->debug.at_debug_fp = NULL;
					}
				}
				ast_debug(5, "GSM channel=\"%s\": AT UNSOLICITED RESPONSE - [%s]\n", ch_gsm->alias, r_buf);
			}
		}

		if (r_buf_valid) {
			// -----------------------------------------------------------------
			// UNSOLICITED RESULT CODE
			// BUSY
			if (is_str_begin_by(r_buf, "BUSY")) {
				AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
				{
			  		// valid if call state OUTGOING_CALL_PROCEEDING || CALL_DELIVERED
					if ((call->state == PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING) || (call->state == PG_CALL_GSM_STATE_CALL_DELIVERED)) {
						call->hangup_cause = AST_CAUSE_USER_BUSY;
						pg_atcommand_queue_prepend(ch_gsm, AT_CEER, AT_OPER_EXEC, call->hash, pg_at_response_timeout, 0, NULL);
					}
				}
			}
			// NO CARRIER
			else if (strstr(r_buf, "NO CARRIER")) {
				AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
				{
					if ((call->state == PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING) || (call->state == PG_CALL_GSM_STATE_CALL_DELIVERED)) {
						call->hangup_cause = AST_CAUSE_NO_ANSWER;
						pg_atcommand_queue_prepend(ch_gsm, AT_CEER, AT_OPER_EXEC, call->hash, pg_at_response_timeout, 0, NULL);
					} else if (call->state == PG_CALL_GSM_STATE_ACTIVE) {
						if (pg_channel_gsm_get_calls_count(ch_gsm) == 1) {
							call->hangup_cause = AST_CAUSE_NORMAL_CLEARING;
							pg_atcommand_queue_prepend(ch_gsm, AT_CEER, AT_OPER_EXEC, call->hash, pg_at_response_timeout, 0, NULL);
						}
					}
				}
			}
			// NO DIALTONE
			else if (strstr(r_buf, "NO DIALTONE")) {
				AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
				{
					if ((call->state == PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING) || (call->state == PG_CALL_GSM_STATE_CALL_DELIVERED)) {
						call->hangup_cause = AST_CAUSE_NO_ANSWER;
						pg_atcommand_queue_prepend(ch_gsm, AT_CEER, AT_OPER_EXEC, call->hash, pg_at_response_timeout, 0, NULL);
					} else if (call->state == PG_CALL_GSM_STATE_ACTIVE) {
						if (pg_channel_gsm_get_calls_count(ch_gsm) == 1) {
							call->hangup_cause = AST_CAUSE_NORMAL_CLEARING;
							pg_atcommand_queue_prepend(ch_gsm, AT_CEER, AT_OPER_EXEC, call->hash, pg_at_response_timeout, 0, NULL);
						}
					}
				}
			}
			// NO ANSWER (M10)
			else if (strstr(r_buf, "NO ANSWER")) {
				AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
				{
					if ((call->state == PG_CALL_GSM_STATE_OUTGOING_CALL_PROCEEDING) || (call->state == PG_CALL_GSM_STATE_CALL_DELIVERED)) {
						call->hangup_cause = AST_CAUSE_NO_ANSWER;
						pg_atcommand_queue_prepend(ch_gsm, AT_CEER, AT_OPER_EXEC, call->hash, pg_at_response_timeout, 0, NULL);
					}
				}
			}
			// RING
			else if (is_str_begin_by(r_buf, "RING")) {
				AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
				{
					if (call->direction == PG_CALL_GSM_DIRECTION_INCOMING) {
						if (call->state == PG_CALL_GSM_STATE_OVERLAP_RECEIVING) {
							// set calling party as unknown
							address_classify("unknown", &call->calling_name);
							// run call state machine
							pg_call_gsm_sm(call, PG_CALL_GSM_MSG_INFO_IND, 0);
						}
					} else {
						// outgoing
						if ((call->state != PG_CALL_GSM_STATE_ACTIVE) &&
								(call->state != PG_CALL_GSM_STATE_LOCAL_HOLD) &&
									(call->state != PG_CALL_GSM_STATE_REMOTE_HOLD)) {
							pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_CHANNEL_UNACCEPTABLE);
						}
					}
				}
				if (!pg_is_channel_gsm_has_calls(ch_gsm)) {
					if ((call = pg_channel_gsm_get_new_call(ch_gsm))) {
						call->direction = PG_CALL_GSM_DIRECTION_INCOMING;
						pg_channel_gsm_last = ch_gsm;
						pg_call_gsm_sm(call, PG_CALL_GSM_MSG_SETUP_IND, 0);
					}
				}
			}
			// CLIP
			else if (is_str_begin_by(r_buf, "+CLIP:")) {
				// parse clip
				parser_ptrs.clip_un = (struct at_gen_clip_unsol *)tmpbuf;
				if (at_gen_clip_unsol_parse(r_buf, r_buf_len, parser_ptrs.clip_un) < 0) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_clip_unsol_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
				} else {
					AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
					{
						// valid if call state OVERLAP_RECEIVING
						if (call->state == PG_CALL_GSM_STATE_OVERLAP_RECEIVING) {
							// get calling number
							if (parser_ptrs.clip_un->number_len > 0) {
								ast_copy_string(call->calling_name.value, parser_ptrs.clip_un->number, MAX_ADDRESS_LENGTH);
								call->calling_name.length = parser_ptrs.clip_un->number_len;
								call->calling_name.type.full = parser_ptrs.clip_un->type;
								address_normalize(&call->calling_name);
							} else
								address_classify("unknown", &call->calling_name);
							// run call state machine
							pg_call_gsm_sm(call, PG_CALL_GSM_MSG_INFO_IND, 0);
						}
					}
				}
			}
			// CUSD
			else if (strstr(r_buf, "+CUSD:")) {
				parser_ptrs.cusd_wr = (struct at_gen_cusd_write *)tmpbuf;
				if (at_gen_cusd_write_parse(r_buf, r_buf_len, parser_ptrs.cusd_wr) < 0) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": at_gen_cusd_write_parse(%.*s) error\n", ch_gsm->alias, r_buf_len, r_buf);
					if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
					ch_gsm->ussd = ast_strdup("USSD response parsing error");
					if (ch_gsm->ussd_sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
						if (ch_gsm->balance) ast_free(ch_gsm->balance);
						ch_gsm->balance = ast_strdup(ch_gsm->ussd);
					}
					write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
					close(ch_gsm->ussd_pipe[1]);
				} else {
					if (parser_ptrs.cusd_wr->str_len > 0) {
						if ((str0 =  get_ussd_decoded(parser_ptrs.cusd_wr->str, parser_ptrs.cusd_wr->str_len, parser_ptrs.cusd_wr->dcs))) {
							if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
							ch_gsm->ussd = ast_strdup(str0);
							if (ch_gsm->ussd_sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
								if (ch_gsm->balance) ast_free(ch_gsm->balance);
								ch_gsm->balance = ast_strdup(ch_gsm->ussd);
							}
							write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
							close(ch_gsm->ussd_pipe[1]);
							free(str0);
						} else {
							if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
							ch_gsm->ussd = ast_strdup("bad response");
							if (ch_gsm->ussd_sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
								if (ch_gsm->balance) ast_free(ch_gsm->balance);
								ch_gsm->balance = ast_strdup(ch_gsm->ussd);
							}
							write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
							close(ch_gsm->ussd_pipe[1]);
						}
					} else {
						if (parser_ptrs.cusd_wr->n == 0) {
							if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
							ch_gsm->ussd = ast_strdup("empty response");
							if (ch_gsm->ussd_sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
								if (ch_gsm->balance) ast_free(ch_gsm->balance);
								ch_gsm->balance = ast_strdup(ch_gsm->ussd);
							}
							write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
							close(ch_gsm->ussd_pipe[1]);
						}
						else if (parser_ptrs.cusd_wr->n == 1) {
							if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
							ch_gsm->ussd = ast_strdup("response can't be presented");
							if (ch_gsm->ussd_sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
								if (ch_gsm->balance) ast_free(ch_gsm->balance);
								ch_gsm->balance = ast_strdup(ch_gsm->ussd);
							}
							write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
							close(ch_gsm->ussd_pipe[1]);
						}
						else if (parser_ptrs.cusd_wr->n == 2) {
							if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
							ch_gsm->ussd = ast_strdup("bad request");
							if (ch_gsm->ussd_sub_cmd == PG_AT_SUBCMD_CUSD_GET_BALANCE) {
								if (ch_gsm->balance) ast_free(ch_gsm->balance);
								ch_gsm->balance = ast_strdup(ch_gsm->ussd);
							}
							write(ch_gsm->ussd_pipe[1], ch_gsm->ussd, strlen(ch_gsm->ussd));
							close(ch_gsm->ussd_pipe[1]);
						}
					}
				}
			}
			// NORMAL POWER DOWN
			else if (is_str_begin_by(r_buf, "NORMAL POWER DOWN")) {
				ast_verb(4, "GSM channel=\"%s\": normal power down\n", ch_gsm->alias);
				ch_gsm->state = PG_CHANNEL_GSM_STATE_DISABLE;
				ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				// stop testviodown timer
				x_timer_stop(ch_gsm->timers.testviodown);
				// stop waitviodown timer
				x_timer_stop(ch_gsm->timers.waitviodown);
				//
				if (!ch_gsm->flags.restart_now) {
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				ch_gsm->state = PG_CHANNEL_GSM_STATE_DISABLE;
				ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				// disable power suply
				if (pg_channel_gsm_power_set(ch_gsm, 0)) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": can't set GSM power suply to off: %s\n", ch_gsm->alias, strerror(errno));
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				ch_gsm->flags.power = 0;
				//
				ast_mutex_unlock(&ch_gsm->lock);
				sleep(3);
				ast_mutex_lock(&ch_gsm->lock);
				//
				res = ch_gsm->power_sequence_number = pg_get_channel_gsm_power_sequence_number();
				ast_mutex_unlock(&ch_gsm->lock);
				if (res > 0)
					usleep(799999 * res);
				ast_mutex_lock(&ch_gsm->lock);
				ch_gsm->power_sequence_number = -1;
				// enable power suply
				if (pg_channel_gsm_power_set(ch_gsm, 1)) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": can't set GSM power suply to on: %s\n", ch_gsm->alias, strerror(errno));
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				ch_gsm->flags.power = 1;
				//
				ast_mutex_unlock(&ch_gsm->lock);
				sleep(3);
				ast_mutex_lock(&ch_gsm->lock);
			}
			// RDY
			else if (is_str_begin_by(r_buf, "RDY")) {
				// stop waitrdy timer
				x_timer_stop(ch_gsm->timers.waitrdy);
				// valid if mgmt state WAIT_RDY
				if (ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_RDY) {
					ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_CFUN;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				}
				ast_verb(4, "GSM channel=\"%s\": serial port work at speed = %d baud\n", ch_gsm->alias, ch_gsm->baudrate);
// 				pg_atcommand_queue_append(ch_gsm, AT_UNKNOWN, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "AT+IPR=%d;&W", chnl->config.baudrate);
			}
			// CFUN
			else if (is_str_begin_by(r_buf, "+CFUN:")) {
				// valid if mgmt state WAIT_CFUN
				if (ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_CFUN) {
					if (!strcasecmp(r_buf, "+CFUN: 0")) {
						// minimum functionality - try to enable GSM module full functionality
						pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1");
					}
					// next mgmt state -- check for SIM is READY
					ch_gsm->state = PG_CHANNEL_GSM_STATE_CHECK_PIN;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
					// start pinwait timer
					x_timer_set(ch_gsm->timers.pinwait, pinwait_timeout);
				}
			} // end of CFUN
			// CPIN
			else if (!strncasecmp(r_buf, "+CPIN:", 6)) {
				// stop pinwait timer
				x_timer_stop(ch_gsm->timers.pinwait);
				//
				if ((ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_CFUN) || (ch_gsm->state == PG_CHANNEL_GSM_STATE_CHECK_PIN)) {
					// processing response
					// set SIM status to polling mode
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
						pg_atcommand_queue_append(ch_gsm, AT_SIM300_CSMINS, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_append(ch_gsm, AT_SIM900_CSMINS, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
						pg_atcommand_queue_append(ch_gsm, AT_M10_QSIMSTAT, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
					// get imei
					pg_atcommand_queue_append(ch_gsm, AT_GSN, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					// get model
					pg_atcommand_queue_append(ch_gsm, AT_GMM, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					// get firmware
					pg_atcommand_queue_append(ch_gsm, AT_GMR, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					//
					if (!strcasecmp(r_buf, "+CPIN: NOT INSERTED")) {
						// - SIM card not inserted
						if (ch_gsm->flags.sim_inserted) {
							// reset channel phone number
							ast_copy_string(ch_gsm->subscriber_number.value, "unknown", MAX_ADDRESS_LENGTH);
							ch_gsm->subscriber_number.length = strlen(ch_gsm->subscriber_number.value);
							ch_gsm->subscriber_number.type.bits.reserved = 1;
							ch_gsm->subscriber_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
							ch_gsm->subscriber_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
							// reset SMS center number
							ast_copy_string(ch_gsm->smsc_number.value, "unknown", MAX_ADDRESS_LENGTH);
							ch_gsm->smsc_number.length = strlen(ch_gsm->smsc_number.value);
							ch_gsm->smsc_number.type.bits.reserved = 1;
							ch_gsm->smsc_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
							ch_gsm->smsc_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;

							if (ch_gsm->operator_name) ast_free(ch_gsm->operator_name); ch_gsm->operator_name = NULL;
							if (ch_gsm->operator_code) ast_free(ch_gsm->operator_code); ch_gsm->operator_code = NULL;
							if (ch_gsm->imsi) ast_free(ch_gsm->imsi); ch_gsm->imsi = NULL;
							if (ch_gsm->iccid) ast_free(ch_gsm->iccid); ch_gsm->iccid = NULL;
// 							if (ch_gsm->pin) ast_free(ch_gsm->pin); ch_gsm->pin = NULL;
// 							if (ch_gsm->puk) ast_free(ch_gsm->puk); ch_gsm->puk = NULL;
							if (ch_gsm->config.balance_request) ast_free(ch_gsm->config.balance_request); ch_gsm->config.balance_request = NULL;

							ch_gsm->flags.sms_table_needed = 1;
							x_timer_stop(ch_gsm->timers.smssend);

							ch_gsm->flags.dcr_table_needed = 1;

							ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
							ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
							ast_verbose("Polygator: GSM channel=\"%s\": SIM removed\n", ch_gsm->alias);
						} else if (!ch_gsm->flags.sim_startup)
							ast_verbose("Polygator: GSM channel=\"%s\": SIM not inserted\n", ch_gsm->alias);
						//
						ch_gsm->flags.sim_startup = 1;
						ch_gsm->flags.sim_inserted = 0;
						//
						if (ch_gsm->flags.sim_change)
							ch_gsm->flags.sim_test = 1;
						//
						pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
						//
						ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
						ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						// start simpoll timer
						x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
					} else if (!strcasecmp(r_buf, "+CPIN: READY")) {
						// - PIN ready
						if (!ch_gsm->flags.sim_inserted)
							ast_verbose("Polygator: GSM channel=\"%s\": SIM inserted\n", ch_gsm->alias);
						if (!ch_gsm->flags.pin_accepted)
							ast_verbose("Polygator: GSM channel=\"%s\": PIN ready\n", ch_gsm->alias);
						// set SIM present flag
						ch_gsm->flags.sim_startup = 1;
						ch_gsm->flags.sim_inserted = 1;
						ch_gsm->flags.pin_required = 0;
						ch_gsm->flags.puk_required = 0;
						ch_gsm->flags.pin_accepted = 1;
						// get iccid
						if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
							pg_atcommand_queue_append(ch_gsm, AT_SIM300_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
						else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
							pg_atcommand_queue_append(ch_gsm, AT_SIM900_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
						else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
							pg_atcommand_queue_append(ch_gsm, AT_M10_QCCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
						//
						if (ch_gsm->flags.suspend_now) {
							// stop all timers
							memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
							ch_gsm->flags.suspend_now = 0;
							pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
							ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
							ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
							// start waitsuspend timer
							x_timer_set(ch_gsm->timers.waitsuspend, waitsuspend_timeout);
						} else if (ch_gsm->flags.sim_change) {
							pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
							ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
							ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
							// start simpoll timer
							x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
						} else {
							ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_CALL_READY;
							ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
							// start callready timer
							x_timer_set(ch_gsm->timers.callready, callready_timeout);
						}
					} else if (!strcasecmp(r_buf, "+CPIN: SIM PIN")) {
						// - PIN is required
						if (!ch_gsm->flags.sim_inserted)
							ast_verbose("Polygator: GSM channel=\"%s\": SIM inserted\n", ch_gsm->alias);
						// set SIM present flag
						ch_gsm->flags.sim_startup = 1;
						ch_gsm->flags.sim_inserted = 1;
						ch_gsm->flags.pin_required = 1;
						ch_gsm->flags.puk_required = 0;
						ch_gsm->flags.pin_accepted = 0;
						// get iccid
						if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
							pg_atcommand_queue_append(ch_gsm, AT_SIM300_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
						else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
							pg_atcommand_queue_append(ch_gsm, AT_SIM900_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
						else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10) {
							// M10 can't got iccid
							if (ch_gsm->pin)
								pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\"", ch_gsm->pin);
							else
								ast_verbose("Polygator: GSM channel=\"%s\": PIN required\n", ch_gsm->alias);
						}
					} else if (!strcasecmp(r_buf, "+CPIN: SIM PUK")) {
						// - PUK is required
						if (!ch_gsm->flags.sim_inserted)
							ast_verbose("Polygator: GSM channel=\"%s\": SIM inserted\n", ch_gsm->alias);
						// set SIM present flag
						ch_gsm->flags.sim_startup = 1;
						ch_gsm->flags.sim_inserted = 1;
						ch_gsm->flags.pin_required = 0;
						ch_gsm->flags.puk_required = 1;
						ch_gsm->flags.pin_accepted = 0;
						// get iccid
						if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
							pg_atcommand_queue_append(ch_gsm, AT_SIM300_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
						else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
							pg_atcommand_queue_append(ch_gsm, AT_SIM900_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
						else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
							pg_atcommand_queue_append(ch_gsm, AT_M10_QCCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					} else if (!strcasecmp(r_buf, "+CPIN: SIM ERROR")) {
						// - SIM ERROR
						pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
						//
						ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
						ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						// start simpoll timer
						x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
					}
				} else if (ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_SUSPEND) {
					if (!strcasecmp(r_buf, "+CPIN: NOT READY")) {
						// stop waitsuspend timer
						x_timer_stop(ch_gsm->timers.waitsuspend);
						ast_verbose("Polygator: GSM channel=\"%s\": module switch to suspend state\n", ch_gsm->alias);
						ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
						ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						// reset registaration status
						ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
						// reset callwait state
						ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
						// reset clir state
						ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
						// reset rssi value
						ch_gsm->rssi = 99;
						// reset ber value
						ch_gsm->ber = 99;
					}
				}
			} // end of CPIN
			// Call Ready
			else if (!strcasecmp(r_buf, "Call Ready")) {
				// valid if mgmt state WAIT_CALL_READY
				if ((ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_CALL_READY) || (ch_gsm->state == PG_CHANNEL_GSM_STATE_CHECK_PIN)) {
					// stop callready timer
					x_timer_stop(ch_gsm->timers.callready);
					// stop pinwait timer
					x_timer_stop(ch_gsm->timers.pinwait);
					//
					if (ch_gsm->flags.suspend_now) {
						//
						ch_gsm->flags.suspend_now = 0;
						// try to set suspend state
						pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
						//
						ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_SUSPEND;
						ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						// start waitsuspend timer
						x_timer_set(ch_gsm->timers.waitsuspend, waitsuspend_timeout);
					} else {
						if (ch_gsm->flags.init) {
							// try to set initial settings
							ch_gsm->state = PG_CHANNEL_GSM_STATE_INIT;
							ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
							// get imsi
							pg_atcommand_queue_append(ch_gsm, AT_CIMI, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
						} else {
							// start runquartersecond timer
							x_timer_set(ch_gsm->timers.runquartersecond, onesec_timeout);
							// start runhalfsecond timer
							x_timer_set(ch_gsm->timers.runhalfsecond, onesec_timeout);
							// start runonesecond timer
							x_timer_set(ch_gsm->timers.runonesecond, onesec_timeout);
							// start runhalfminute timer
							x_timer_set(ch_gsm->timers.runhalfminute, onesec_timeout);
							// start runoneminute timer
							x_timer_set(ch_gsm->timers.runoneminute, onesec_timeout);
							// start runvifesecond timer
							x_timer_set(ch_gsm->timers.runfivesecond, runfivesecond_timeout);
							// start registering timer
							x_timer_set(ch_gsm->timers.registering, registering_timeout);
							// set run state
							ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
							ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						}
					}
				}
			}
			// CMT
			else if (strstr(r_buf, "+CMT:")) {
				if ((str0 = strrchr(r_buf, ','))) {
					str0++;
					if (is_str_digit(str0)) {
						ch_gsm->pdu_len = atoi(str0);
						ch_gsm->pdu_cmt_wait = 1;
					}
				}
			}
			// CMT PDU
			else if (ch_gsm->pdu_cmt_wait && is_str_xdigit(r_buf)) {
				// parsing PDU
				if ((pdu = pdu_parser(r_buf, r_buf_len, ch_gsm->pdu_len, curr_tv.tv_sec, &res))) {
					//
					ast_verb(2, "GSM channel=\"%s\": received message from \"%s%s\"\n", ch_gsm->alias, (pdu->raddr.type.full == 145)?("+"):(""), pdu->raddr.value);
					// SMS dialplan notification
					if (ch_gsm->config.sms_notify_enable) {
						// increment channel ID
						ast_mutex_lock(&pg_lock);
						ch_id = channel_id++;
						ast_mutex_unlock(&pg_lock);
						// prevent deadlock while asterisk channel is allocating
						ast_mutex_unlock(&ch_gsm->lock);
						// allocation channel in pbx spool
						sprintf(tmpbuf, "%s%s", (pdu->raddr.type.full == 145)?("+"):(""), pdu->raddr.value);
#if ASTERISK_VERSION_NUM < 10800
						ast_ch_tmp = ast_channel_alloc(0,													/* int needqueue */
														AST_STATE_DOWN,										/* int state */
														tmpbuf,												/* const char *cid_num */
														tmpbuf,												/* const char *cid_name */
														NULL,												/* const char *acctcode */
														S_OR(ch_gsm->config.sms_notify_extension, "s"),		/* const char *exten */
														S_OR(ch_gsm->config.sms_notify_context, "default"),	/* const char *context */
														0,													/* const int amaflag */
														"PGSMS/%s-%08x",									/* const char *name_fmt, ... */
														ch_gsm->alias, ch_id);
#else
						ast_ch_tmp = ast_channel_alloc(0,													/* int needqueue */
														AST_STATE_DOWN,										/* int state */
														tmpbuf,												/* const char *cid_num */
														tmpbuf,												/* const char *cid_name */
														NULL,												/* const char *acctcode */
														S_OR(ch_gsm->config.sms_notify_extension, "s"),		/* const char *exten */
														S_OR(ch_gsm->config.sms_notify_context, "default"),	/* const char *context */
														NULL,												/* const char *linkedid */
														0,													/* int amaflag */
														"PGSMS/%s-%08x",									/* const char *name_fmt, ... */
														ch_gsm->alias, ch_id);
#endif
						ast_mutex_lock(&ch_gsm->lock);

						// check channel
						if (ast_ch_tmp) {
							// set channel variables
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSCHANNEL", ch_gsm->alias); // Channel name
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSCENTERADDRESS", pdu->scaddr.value); // SMS Center Address
							sprintf(tmpbuf, "%d", pdu->scaddr.type.full);
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSCENTERADDRESSTYPE", tmpbuf); // SMS Center Address Type
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSORIGADDRESS", pdu->raddr.value); // SMS Originator Address
							sprintf(tmpbuf, "%d", pdu->raddr.type.full);
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSORIGADDRESSTYPE", tmpbuf); // SMS Originator Address Type
							sprintf(tmpbuf, "%d", pdu->concat_num);
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSPART", tmpbuf); // SMS current part number
							sprintf(tmpbuf, "%d", pdu->concat_cnt);
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSPARTOF", tmpbuf); // SMS parts count
							sprintf(tmpbuf, "%ld", pdu->sent);
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSSENT", tmpbuf); // Sent time (UNIX-time)
							pbx_builtin_setvar_helper(ast_ch_tmp, "PGSMSCONTENT", pdu->ud); // SMS content
							// start pbx
							if (ast_pbx_start(ast_ch_tmp)) {
								ast_log(LOG_ERROR, "GSM channel=\"%s\": unable to start pbx - SMS receiving notification from \"%s\" failed\n", ch_gsm->alias, tmpbuf);
								ast_hangup(ast_ch_tmp);
							}
						} else
							ast_log(LOG_ERROR, "GSM channel=\"%s\": unable to allocate channel - SMS receiving notification from \"%s\" failed\n", ch_gsm->alias, tmpbuf);
					}
					// get max message id
					int msgid = 0;
					ast_mutex_lock(&pg_sms_db_lock);
					str0 = sqlite3_mprintf("SELECT MAX(msgid) FROM '%q-inbox';", ch_gsm->iccid);
					while (1)
					{
						res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
						if (res == SQLITE_OK) {
							row = 0;
							while (1)
							{
								res = sqlite3_step(sql0);
								if (res == SQLITE_ROW) {
									row++;
									msgid = sqlite3_column_int(sql0, 0);
								} else if(res == SQLITE_DONE)
									break;
								else if(res == SQLITE_BUSY) {
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1000);
									ast_mutex_lock(&ch_gsm->lock);
									continue;
								} else {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
									break;
								}
							}
							sqlite3_finalize(sql0);
							break;
						} else if (res == SQLITE_BUSY) {
							ast_mutex_unlock(&ch_gsm->lock);
							usleep(1000);
							ast_mutex_lock(&ch_gsm->lock);
							continue;
						} else{
							ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_free(str0);
					ast_mutex_unlock(&pg_sms_db_lock);
					// check for concatenated message
					int concatmsgid = 0;
					if (pdu->concat_cnt > 1) {
						// is concatenated message
						ast_mutex_lock(&pg_sms_db_lock);
						str0 = sqlite3_mprintf("SELECT MAX(msgid) FROM '%q-inbox' "
												"WHERE "
												"scatype=%d AND "
												"scaname='%q' AND "
												"oatype=%d AND "
												"oaname='%q' AND "
												"partid=%d AND "
												"partof=%d;",
												ch_gsm->iccid,
												pdu->scaddr.type.full,
												pdu->scaddr.value,
												pdu->raddr.type.full,
												pdu->raddr.value,
												pdu->concat_ref,
												pdu->concat_cnt);
						while (1)
						{
							res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
							if(res == SQLITE_OK){
								row = 0;
								while(1)
								{
									res = sqlite3_step(sql0);
									if (res == SQLITE_ROW) {
										row++;
										concatmsgid = sqlite3_column_int(sql0, 0);
									} else if (res == SQLITE_DONE)
										break;
									else if(res == SQLITE_BUSY) {
										ast_mutex_unlock(&ch_gsm->lock);
										usleep(1000);
										ast_mutex_lock(&ch_gsm->lock);
										continue;
									} else {
										ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
										break;
									}
								}
								sqlite3_finalize(sql0);
								break;
							} else if (res == SQLITE_BUSY) {
								ast_mutex_unlock(&ch_gsm->lock);
								usleep(1000);
								ast_mutex_lock(&ch_gsm->lock);
								continue;
							} else {
								ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
								break;
							}
						}
						sqlite3_free(str0);
						ast_mutex_unlock(&pg_sms_db_lock);
						// check for concatenated message is complete
						if (concatmsgid) {
							ast_mutex_lock(&pg_sms_db_lock);
							str0 = sqlite3_mprintf("SELECT msgno FROM '%q-inbox' WHERE msgid=%d;", ch_gsm->iccid, concatmsgid);
							while (1)
							{
								res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
								row = 0;
								if (res == SQLITE_OK) {
									while (1)
									{
										res = sqlite3_step(sql0);
										if (res == SQLITE_ROW)
											row++;
										else if (res == SQLITE_DONE)
											break;
										else if (res == SQLITE_BUSY) {
											ast_mutex_unlock(&ch_gsm->lock);
											usleep(1000);
											ast_mutex_lock(&ch_gsm->lock);
											continue;
										} else {
											ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
											break;
										}
									}
									sqlite3_finalize(sql0);
									break;
								} else if (res == SQLITE_BUSY) {
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1);
									ast_mutex_lock(&ch_gsm->lock);
									continue;
								} else {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
									break;
								}
							}
							sqlite3_free(str0);
							ast_mutex_unlock(&pg_sms_db_lock);
							// check part of concatenated message
							if (row < pdu->concat_cnt)
								msgid = concatmsgid;
							else if (row == pdu->concat_cnt)
								msgid++;
							else {
								ast_log(LOG_ERROR, "GSM channel=\"%s\": inbox message=%d has too more parts (%d of %d)\n",
										ch_gsm->alias, concatmsgid, row, pdu->concat_cnt);
								msgid++;
								}
						} else 
							msgid++; // increment max id number
					} else 
						msgid++; // increment max id number
					// insert new message into sms database
					ast_mutex_lock(&pg_sms_db_lock);
					str0 = sqlite3_mprintf("INSERT INTO '%q-inbox' ("
											"pdu, " // TEXT
											"msgid, " // INTEGER
											"status, " // INTEGER
											"scatype, " // INTEGER
											"scaname, " // TEXT
											"oatype, " // INTEGER
											"oaname, " // TEXT
											"dcs, " // INTEGER
											"sent, " // INTEGER
											"received," // INTEGER
											"partid, " // INTEGER,
											"partof, " // INTEGER,
											"part, " // INTEGER,
											"content" // TEXT
											") VALUES ("
											"'%q', " // pdu TEXT
											"%d, " // msgid INTEGER
											 "%d, " // status INTEGER
											"%d, " // scatype INTEGER
											"'%q', " // scaname TEXT
											"%d, " // oatype INTEGER
											"'%q', " // oaname TEXT
											"%d, " // dcs INTEGER
											"%ld, " // sent INTEGER
											"%ld, " // received INTEGER
											"%d, " // partid INTEGER,
											"%d, " // partof INTEGER,
											"%d, " // part INTEGER,
											"'%q');", // content TEXT
											ch_gsm->iccid,
											r_buf, // pdu TEXT
											msgid, // msgid INTEGER
											1, // status INTEGER
											pdu->scaddr.type.full, // scatype INTEGER
											pdu->scaddr.value, // scaname TEXT
											pdu->raddr.type.full, // oatype INTEGER
											pdu->raddr.value, // oaname TEXT
											pdu->dacosc, // dcs INTEGER
											pdu->sent, // sent INTEGER
											pdu->delivered, // received INTEGER
											pdu->concat_ref, // partid INTEGER,
											pdu->concat_cnt, // partof INTEGER,
											pdu->concat_num, // part INTEGER,
											pdu->ud); // content TEXT
					while (1)
					{
						res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
						if (res == SQLITE_OK) {
							row = 0;
							while (1)
							{
								res = sqlite3_step(sql0);
								if (res == SQLITE_ROW)
									row++;
								else if (res == SQLITE_DONE)
									break;
								else if (res == SQLITE_BUSY) {
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1000);
									ast_mutex_lock(&ch_gsm->lock);
									continue;
								} else {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
									break;
								}
							}
							sqlite3_finalize(sql0);
							break;
						} else if (res == SQLITE_BUSY) {
							ast_mutex_unlock(&ch_gsm->lock);
							usleep(1000);
							ast_mutex_lock(&ch_gsm->lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_free(str0);
					ast_mutex_unlock(&pg_sms_db_lock);
					pdu_free(pdu);
				} else {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": PDU parsing error - pdu_parser(%d)\n", ch_gsm->alias, res);
					ast_mutex_lock(&pg_sms_db_lock);
					str0 = sqlite3_mprintf("INSERT INTO '%q-inbox' (pdu) VALUES ('%q');", ch_gsm->iccid, r_buf);
					while (1)
					{
						res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
						if (res == SQLITE_OK) {
							row = 0;
							while (1)
							{
								res = sqlite3_step(sql0);
								if (res == SQLITE_ROW)
									row++;
								else if (res == SQLITE_DONE)
									break;
								else if (res == SQLITE_BUSY) {
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1000);
									ast_mutex_lock(&ch_gsm->lock);
									continue;
								} else {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
									break;
								}
							}
							sqlite3_finalize(sql0);
							break;
						} else if (res == SQLITE_BUSY) {
							ast_mutex_unlock(&ch_gsm->lock);
							usleep(1000);
							ast_mutex_lock(&ch_gsm->lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_free(str0);
					ast_mutex_unlock(&pg_sms_db_lock);
				}
				// reset wait flag
				ch_gsm->pdu_cmt_wait = 0;
			}
			// CDS
			else if (strstr(r_buf, "+CDS:")) {
				if ((str0 = strchr(r_buf, SP))) {
					str0++;
					if (is_str_digit(str0)) {
						ch_gsm->pdu_len = atoi(str0);
						ch_gsm->pdu_cds_wait = 1;
					}
				}
			}
			// CDS PDU
			else if (ch_gsm->pdu_cds_wait && is_str_xdigit(r_buf)) {
				// parsing PDU
				if ((pdu = pdu_parser(r_buf, r_buf_len, ch_gsm->pdu_len, 0, &res))) {
					if (!pdu->status) {
						ast_verb(2, "GSM channel=\"%s\": message delivered to \"%s%s\"\n", ch_gsm->alias, (pdu->raddr.type.full == 145)?("+"):(""), pdu->raddr.value);
						// update sent database
						ast_mutex_lock(&pg_sms_db_lock);
						str0 = sqlite3_mprintf("UPDATE '%q-sent' SET status=1, received=%ld, stareppdulen=%d, stareppdu='%q' WHERE mr=%d AND daname='%q';",
												ch_gsm->iccid,
												pdu->delivered,
												ch_gsm->pdu_len,
												r_buf,
												pdu->mr,
												pdu->raddr.value);
						while (1)
						{
							res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
							if (res == SQLITE_OK) {
								row = 0;
								while (1)
								{
									res = sqlite3_step(sql0);
									if (res == SQLITE_ROW)
										row++;
									else if (res == SQLITE_DONE)
										break;
									else if (res == SQLITE_BUSY) {
										ast_mutex_unlock(&ch_gsm->lock);
										usleep(1000);
										ast_mutex_lock(&ch_gsm->lock);
										continue;
									} else {
										ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
										break;
									}
								}
								sqlite3_finalize(sql0);
								break;
							} else if (res == SQLITE_BUSY) {
								ast_mutex_unlock(&ch_gsm->lock);
								usleep(1000);
								ast_mutex_lock(&ch_gsm->lock);
								continue;
							} else {
								ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
								break;
							}
						}
						sqlite3_free(str0);
						ast_mutex_unlock(&pg_sms_db_lock);
					} else
						ast_verb(2, "GSM channel=\"%s\": message undelivered to \"%s%s\" - status=%d\n",
									ch_gsm->alias, (pdu->raddr.type.full == 145)?("+"):(""), pdu->raddr.value, pdu->status);
					pdu_free(pdu);
				} else
					ast_log(LOG_ERROR, "GSM channel=\"%s\": PDU parsing error - pdu_parser(%d)\n", ch_gsm->alias, res);
				// reset wait flag
				ch_gsm->pdu_cds_wait = 0;
			}
			//------------------------------------------------------------------
			if (!strstr(r_buf, "+CMT:") && !strstr(r_buf, "+CDS:"))
				r_buf_active = 0;
			r_buf[0] = '\0';
			r_cptr = r_buf;
			r_buf_len = 0;
			r_buf_valid = 0;
		}

		// try to send at command
		if (pg_atcommand_trysend(ch_gsm) < 0) {
			// set restart flag
			if (!ch_gsm->flags.shutdown_now && !ch_gsm->flags.restart_now) {
				ch_gsm->flags.restart = 1;
				ch_gsm->flags.restart_now = 1;
			}
		}

		// handle timers
		// waitrdy
		if (is_x_timer_enable(ch_gsm->timers.waitrdy)) {
			if (is_x_timer_fired(ch_gsm->timers.waitrdy)) {
				// stop waitrdy timer
				x_timer_stop(ch_gsm->timers.waitrdy);
				// waitrdy timer fired
				ast_verb(4, "GSM channel=\"%s\": waitrdy timer fired\n", ch_gsm->alias);
				// check for VIO status signal
				if ((ch_gsm->vio = pg_channel_gsm_vio_get(ch_gsm)) < 0) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": can't get channel power VIO status\n", ch_gsm->alias);
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				if (!ch_gsm->vio) {
					ast_log(LOG_WARNING, "GSM channel=\"%s\": GSM module power supply can't started\n", ch_gsm->alias);
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				ast_verb(4, "GSM channel=\"%s\": run functionality test\n", ch_gsm->alias);
				// run functionality test
				ch_gsm->flags.func_test_run = 1;
				ch_gsm->flags.func_test_done = 0;
				ch_gsm->baudrate_test = ch_gsm->baudrate;
				ch_gsm->baudrate = 0;
				ch_gsm->state = PG_CHANNEL_GSM_STATE_TEST_FUN;
				ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				// flush at command queue
				if (ch_gsm->at_cmd) {
					ast_free(ch_gsm->at_cmd);
					ch_gsm->at_cmd = NULL;
				}
				pg_atcommand_queue_flush(ch_gsm);
				// run testfun timer
				x_timer_set(ch_gsm->timers.testfun, testfun_timeout);
				// run testfunsend timer
				x_timer_set(ch_gsm->timers.testfunsend, testfunsend_timeout);
			}
		}
		// callready
		if (is_x_timer_enable(ch_gsm->timers.callready)) {
			if (is_x_timer_fired(ch_gsm->timers.callready)) {
				// callready timer fired
				ast_log(LOG_WARNING, "GSM channel=\"%s\": callready timer fired\n", ch_gsm->alias);
				// set restart flag
				if (!ch_gsm->flags.shutdown_now && !ch_gsm->flags.restart_now) {
					ch_gsm->flags.restart = 1;
					ch_gsm->flags.restart_now = 1;
				}
				// stop callready timer
				x_timer_stop(ch_gsm->timers.callready);
			}
		}
		// registering
		if (is_x_timer_enable(ch_gsm->timers.registering)) {
			if (is_x_timer_fired(ch_gsm->timers.registering)) {
				// registering timer fired
				ast_verb(5, "GSM channel=\"%s\": registering timer fired\n", ch_gsm->alias);
				// stop registering timer
				x_timer_stop(ch_gsm->timers.registering);
				// try to disable GSM module full functionality
				pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
				// check is sim present
				if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
					pg_atcommand_queue_append(ch_gsm, AT_SIM300_CSMINS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
					pg_atcommand_queue_append(ch_gsm, AT_SIM900_CSMINS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				else if(ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
					pg_atcommand_queue_append(ch_gsm, AT_M10_QSIMSTAT, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				//
				if (ch_gsm->flags.suspend_now) {
					// stop all timers
					memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
					ast_verbose("GSM channel=\"%s\": module switch to suspend state\n", ch_gsm->alias);
					ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
					ch_gsm->flags.suspend_now = 0;
				}
				// start simpoll timer
				if(ch_gsm->state != PG_CHANNEL_GSM_STATE_SUSPEND)
					x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
			}
		}
		// testfun
		if (is_x_timer_enable(ch_gsm->timers.testfun)) {
			if (is_x_timer_fired(ch_gsm->timers.testfun)) {
				// stop testfun timer
				x_timer_stop(ch_gsm->timers.testfun);
				// stop testfunsend timer
				x_timer_stop(ch_gsm->timers.testfunsend);

				// select test baudrate
				if (ch_gsm->baudrate_test == 115200)
					ch_gsm->baudrate_test = 9600;
				else
					ch_gsm->baudrate_test = 0;

				if (ch_gsm->baudrate_test) {
					// set hardware baudrate
					if (!tcgetattr(ch_gsm->tty_fd, &termios)) {
						switch (ch_gsm->baudrate_test)
						{
							case 9600:
								baudrate = B9600;
								break;
							default:
								baudrate = B115200;
								break;
						}
						cfsetispeed(&termios, baudrate);
						cfsetospeed(&termios, baudrate);
						if (tcsetattr(ch_gsm->tty_fd, TCSANOW, &termios) < 0) {
							ast_log(LOG_ERROR, "GSM channel=\"%s\": tcsetattr() error: %s\n", ch_gsm->alias, strerror(errno));
							ast_mutex_unlock(&ch_gsm->lock);
							goto pg_channel_gsm_workthread_end;
						}
					} else {
						ast_log(LOG_ERROR, "GSM channel=\"%s\": tcgetattr() error: %s\n", ch_gsm->alias, strerror(errno));
						ast_mutex_unlock(&ch_gsm->lock);
						goto pg_channel_gsm_workthread_end;
					}
					ast_verb(4, "GSM channel=\"%s\": probe serial port speed = %d baud\n", ch_gsm->alias, ch_gsm->baudrate_test);
					// run testfun timer
					x_timer_set(ch_gsm->timers.testfun, testfun_timeout);
					// run testfunsend timer
					x_timer_set(ch_gsm->timers.testfunsend, testfunsend_timeout);
				} else {
					ch_gsm->baudrate = ch_gsm->baudrate_test = ch_gsm->config.baudrate;
					// set restart flag
					if (!ch_gsm->flags.shutdown_now && !ch_gsm->flags.restart_now) {
						ch_gsm->flags.restart = 1;
						ch_gsm->flags.restart_now = 1;
					}
				}
			}
		}
		// testfunsend
		if (is_x_timer_enable(ch_gsm->timers.testfunsend)) {
			if (is_x_timer_fired(ch_gsm->timers.testfunsend)) {
				// testfunsend timer fired - send cfun read status command
				pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				// restart testfunsend timer
				x_timer_set(ch_gsm->timers.testfunsend, testfunsend_timeout);
			}
		}
		// runquartersecond
		if (is_x_timer_enable(ch_gsm->timers.runquartersecond)) {
			if (is_x_timer_fired(ch_gsm->timers.runquartersecond)) {
				// restart runquartersecond timer
				x_timer_set(ch_gsm->timers.runquartersecond, runquartersecond_timeout);
				if (pg_is_channel_gsm_has_calls(ch_gsm)) {
					// get current call list
					pg_atcommand_queue_append(ch_gsm, AT_CLCC, AT_OPER_EXEC, 0, 1, 0, NULL);
				}
			}
		}
		// runhalfsecond
		if (is_x_timer_enable(ch_gsm->timers.runhalfsecond)) {
			if (is_x_timer_fired(ch_gsm->timers.runhalfsecond)) {
				// restart runhalfsecond timer
				x_timer_set(ch_gsm->timers.runhalfsecond, runhalfsecond_timeout);
			}
		}
		// runonesecond
		if (is_x_timer_enable(ch_gsm->timers.runonesecond)) {
			if (is_x_timer_fired(ch_gsm->timers.runonesecond)) {
				// runonesecond timer fired
				// request registration status
				pg_atcommand_queue_append(ch_gsm, AT_CREG, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				// get signal quality
				pg_atcommand_queue_append(ch_gsm, AT_CSQ, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
				// call line contest increase
				AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
				{
					if (call->line) call->contest++;
				}
				// restart runonesecond timer
				x_timer_set(ch_gsm->timers.runonesecond, runonesecond_timeout);
			}
		}
		// runfivesecond
		if (is_x_timer_enable(ch_gsm->timers.runfivesecond)) {
			if (is_x_timer_fired(ch_gsm->timers.runfivesecond)) {
				// runfivesecond timer fired
				if (!pg_is_channel_gsm_has_calls(ch_gsm)) {
					// test functionality status
					pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					// check SIM status
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
						pg_atcommand_queue_append(ch_gsm, AT_SIM300_CSMINS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_append(ch_gsm, AT_SIM900_CSMINS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
						pg_atcommand_queue_append(ch_gsm, AT_M10_QSIMSTAT, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				}
				// test vio
				if ((ch_gsm->vio = pg_channel_gsm_vio_get(ch_gsm)) < 0) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": can't get channel power VIO status\n", ch_gsm->alias);
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				if (!ch_gsm->vio) {
					ast_verb(4, "GSM channel=\"%s\": VIO set to down unexpectedly\n", ch_gsm->alias);
					// set restart flag
					if (!ch_gsm->flags.shutdown_now && !ch_gsm->flags.restart_now) {
						ch_gsm->flags.restart = 1;
						ch_gsm->flags.restart_now = 1;
					}
				}
				// restart runfivesecond timer
				x_timer_set(ch_gsm->timers.runfivesecond, runfivesecond_timeout);
			}
		}
		// runhalfminute
		if (is_x_timer_enable(ch_gsm->timers.runhalfminute)) {
			if (is_x_timer_fired(ch_gsm->timers.runhalfminute)) {
				// runhalfminute timer fired
				if (!pg_is_channel_gsm_has_calls(ch_gsm)) {
					// get subscriber number
					pg_atcommand_queue_append(ch_gsm, AT_CNUM, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					// get imei
					pg_atcommand_queue_append(ch_gsm, AT_GSN, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					// get imsi
					pg_atcommand_queue_append(ch_gsm, AT_CIMI, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					// get iccid
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
						pg_atcommand_queue_append(ch_gsm, AT_SIM300_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_append(ch_gsm, AT_SIM900_CCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
						pg_atcommand_queue_append(ch_gsm, AT_M10_QCCID, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					// request input gain level (loudspeaker)
					pg_atcommand_queue_append(ch_gsm, AT_CLVL, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					// request output gain level (microphone)
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
						pg_atcommand_queue_append(ch_gsm, AT_SIM300_CMIC, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_append(ch_gsm, AT_SIM900_CMIC, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
						pg_atcommand_queue_append(ch_gsm, AT_M10_QMIC, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					// get operator
					// name
					if ((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING)) {
						pg_atcommand_queue_append(ch_gsm, AT_COPS, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "3,0");
						pg_atcommand_queue_append(ch_gsm, AT_COPS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					}
					// code
					if ((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING)) {
						pg_atcommand_queue_append(ch_gsm, AT_COPS, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "3,2");
						pg_atcommand_queue_append(ch_gsm, AT_COPS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					}
				}
				// restart runhalfminute timer
				x_timer_set(ch_gsm->timers.runhalfminute, halfminute_timeout);
			}
		}
		// runoneminute
		if (is_x_timer_enable(ch_gsm->timers.runoneminute)) {
			if (is_x_timer_fired(ch_gsm->timers.runoneminute)) {
				// runoneminute timer fired
				if (!pg_is_channel_gsm_has_calls(ch_gsm)) {
					// get SMS center address
					pg_atcommand_queue_append(ch_gsm, AT_CSCA, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				}
				// restart runoneminute timer
				x_timer_set(ch_gsm->timers.runoneminute, runoneminute_timeout);
			}
		}
		// waitsuspend
		if (is_x_timer_enable(ch_gsm->timers.waitsuspend)) {
			if (is_x_timer_fired(ch_gsm->timers.waitsuspend)) {
				// waitsuspend timer fired
				ast_log(LOG_ERROR, "GSM channel=\"%s\": can't switch in suspend mode\n", ch_gsm->alias);
				// stop waitsuspend timer
				x_timer_stop(ch_gsm->timers.waitsuspend);
				//
				if(ch_gsm->flags.sim_inserted) // start pinwait timer
					x_timer_set(ch_gsm->timers.pinwait, pinwait_timeout);
				else // start simpoll timer
					x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
				}
			}
		// call timers
		AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
		{
			// dial
			if (is_x_timer_enable(call->timers.dial)) {
				if (is_x_timer_fired(call->timers.dial)) {
					// dial timer fired
					ast_verb(4, "GSM channel=\"%s\":  call line=%d dialing timeout=%ld.%06ld expired\n",
								ch_gsm->alias,
			  					call->line,
								call->timers.dial.timeout.tv_sec,
								call->timers.dial.timeout.tv_usec);
					// run call sm
					pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NO_ANSWER);
					// hangup gsm channel
					if (pg_channel_gsm_get_calls_count(ch_gsm) > 1) {
						pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1%d", call->line);
					} else {
						if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "0");
						else
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					}
					// stop dial timer
					x_timer_stop(call->timers.dial);
				}
			}
			// proceeding
			if (is_x_timer_enable(call->timers.proceeding)) {
				if (is_x_timer_fired(call->timers.proceeding)) {
					// proceeding timer fired
					ast_verb(4, "GSM channel=\"%s\":  call line=%d proceeding timeout expired\n", ch_gsm->alias, call->line);
					// run call sm
					pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NORMAL_TEMPORARY_FAILURE);
					// hangup gsm channel
					if (pg_channel_gsm_get_calls_count(ch_gsm) > 1) {
						pg_atcommand_queue_prepend(ch_gsm, AT_CHLD, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1%d", call->line);
					} else {
						if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "0");
						else
							pg_atcommand_queue_prepend(ch_gsm, AT_H, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, NULL);
					}
					// stop proceeding timer
					x_timer_stop(call->timers.proceeding);
				}
			}
		}
		// simpoll
		if (is_x_timer_enable(ch_gsm->timers.simpoll)) {
			if (is_x_timer_fired(ch_gsm->timers.simpoll)) {
				// simpoll timer fired
				ast_verb(5, "GSM channel=\"%s\": simpoll timer fired\n", ch_gsm->alias);
				// stop simpoll timer
				x_timer_stop(ch_gsm->timers.simpoll);
				//
				if (ch_gsm->flags.resume_now) {
					ast_verbose("GSM channel=\"%s\": module return from suspend state\n", ch_gsm->alias);
					ch_gsm->flags.resume_now = 0;
				}
				// check is sim present
				if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
					pg_atcommand_queue_append(ch_gsm, AT_SIM300_CSMINS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
					pg_atcommand_queue_append(ch_gsm, AT_SIM900_CSMINS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
					pg_atcommand_queue_append(ch_gsm, AT_M10_QSIMSTAT, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				// try to enable GSM module full functionality
				pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1");
				pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				// next mgmt state -- check for SIM is READY
				if (ch_gsm->state != PG_CHANNEL_GSM_STATE_CHECK_PIN) {
					ch_gsm->state = PG_CHANNEL_GSM_STATE_CHECK_PIN;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				}
				// start pinwait timer
				x_timer_set(ch_gsm->timers.pinwait, pinwait_timeout);
			}
		}
		// pinwait
		if (is_x_timer_enable(ch_gsm->timers.pinwait)) {
			if (is_x_timer_fired(ch_gsm->timers.pinwait)) {
				// pinwait timer fired
				ast_verb(5, "GSM channel=\"%s\": pinwait timer fired\n", ch_gsm->alias);
				// stop pinwait timer
				x_timer_stop(ch_gsm->timers.pinwait);
				// try to disable GSM module full functionality
				pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
				// check is sim present
				if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
					pg_atcommand_queue_append(ch_gsm, AT_SIM300_CSMINS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
					pg_atcommand_queue_append(ch_gsm, AT_SIM900_CSMINS, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				else if(ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
					pg_atcommand_queue_append(ch_gsm, AT_M10_QSIMSTAT, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
				//
				if (ch_gsm->flags.suspend_now) {
					// stop all timers
					memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
					ast_verbose("GSM channel=\"%s\": module switch to suspend state\n", ch_gsm->alias);
					ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
					ch_gsm->flags.suspend_now = 0;
				}
				// start simpoll timer
				if(ch_gsm->state != PG_CHANNEL_GSM_STATE_SUSPEND)
					x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
			}
		}
		// smssend
		if (is_x_timer_enable(ch_gsm->timers.smssend)) {
			if (is_x_timer_fired(ch_gsm->timers.smssend)) {
				// smssend timer fired
				ast_verb(9, "GSM channel=\"%s\": sms send timer fired %ld.%ld\n", ch_gsm->alias, curr_tv.tv_sec, curr_tv.tv_usec);
				// prepare new pdu for sending from outbox
				// get maxmsgid from preparing
				int i;
				int maxmsgid = 0;
				ast_mutex_lock(&pg_sms_db_lock);
				str0 = sqlite3_mprintf("SELECT MAX(msgid) FROM '%q-preparing';", ch_gsm->iccid);
				while (1)
				{
					res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
					if (res == SQLITE_OK) {
						row = 0;
						while (1)
						{
							res = sqlite3_step(sql0);
							if (res == SQLITE_ROW) {
								row++;
								maxmsgid = sqlite3_column_int(sql0, 0);
							} else if(res == SQLITE_DONE)
								break;
							else if (res == SQLITE_BUSY) {
								ast_mutex_unlock(&ch_gsm->lock);
								usleep(1000);
								ast_mutex_lock(&ch_gsm->lock);
								continue;
							} else {
								ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
								break;
							}
						}
						sqlite3_finalize(sql0);
						break;
					} else if (res == SQLITE_BUSY) {
						ast_mutex_unlock(&ch_gsm->lock);
						usleep(1000);
						ast_mutex_lock(&ch_gsm->lock);
						continue;
					} else {
						ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
						break;
					}
				}
				sqlite3_free(str0);
				ast_mutex_unlock(&pg_sms_db_lock);
				// increment maxmsgid
				maxmsgid++;
				// get message from outbox
				char *o2p_msgdest = NULL;
				char *o2p_msgcontent = NULL;
				char o2p_msghash[40];
				int o2p_msgflash = 0;
				int o2p_msgno = 0;
				char hsec[24]; // for hash calc
				char husec[8]; // for hash calc
				struct MD5Context Md5Ctx; // for hash calc
				unsigned char hashbin[16]; // for hash calc
				o2p_msghash[0] = '\0';
				ast_mutex_lock(&pg_sms_db_lock);
				str0 = sqlite3_mprintf("SELECT msgno,destination,content,flash,hash FROM '%q-outbox' ORDER BY enqueued;", ch_gsm->iccid);
				while (1)
				{
					res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
					if (res == SQLITE_OK) {
						row = 0;
						while (1)
						{
							res = sqlite3_step(sql0);
							if (res == SQLITE_ROW) {
								row++;
								if (!o2p_msgno) {
									// msgno
									o2p_msgno = sqlite3_column_int(sql0, 0);
									// destination
									o2p_msgdest = ast_strdup((sqlite3_column_text(sql0, 1))?((char *)sqlite3_column_text(sql0, 1)):("null"));
									// content
									o2p_msgcontent = ast_strdup((sqlite3_column_text(sql0, 2))?((char *)sqlite3_column_text(sql0, 2)):(""));
									// flash
									o2p_msgflash = sqlite3_column_int(sql0, 3);
									// hash
									ast_copy_string(o2p_msghash, (sqlite3_column_text(sql0, 4))?((char *)sqlite3_column_text(sql0, 4)):(""), sizeof(o2p_msghash));
									if (!strlen(o2p_msghash)) {
										//
										sprintf(hsec, "%ld", curr_tv.tv_sec);
										sprintf(husec, "%ld", curr_tv.tv_usec);
										//
										MD5Init(&Md5Ctx);
										MD5Update(&Md5Ctx, (unsigned char *)ch_gsm->alias, strlen(ch_gsm->alias));
										MD5Update(&Md5Ctx, (unsigned char *)o2p_msgdest, strlen(o2p_msgdest));
										MD5Update(&Md5Ctx, (unsigned char *)o2p_msgcontent, strlen(o2p_msgcontent));
										MD5Update(&Md5Ctx, (unsigned char *)hsec, strlen(hsec));
										MD5Update(&Md5Ctx, (unsigned char *)husec, strlen(husec));
										MD5Final(hashbin, &Md5Ctx);
										i = olen = 0;
										for (i=0; i<16; i++)
											olen += sprintf(o2p_msghash+olen, "%02x", (unsigned char)hashbin[i]);
									}
								} else
									break;
							} else if (res == SQLITE_DONE)
								break;
							else if (res == SQLITE_BUSY) {
								ast_mutex_unlock(&ch_gsm->lock);
								usleep(1000);
								ast_mutex_lock(&ch_gsm->lock);
								continue;
							} else {
								ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
								break;
							}
						}
						sqlite3_finalize(sql0);
						break;
					} else if (res == SQLITE_BUSY) {
						ast_mutex_unlock(&ch_gsm->lock);
						usleep(1000);
						ast_mutex_lock(&ch_gsm->lock);
						continue;
					} else {
						ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
						break;
					}
				}
				sqlite3_free(str0);
				ast_mutex_unlock(&pg_sms_db_lock);
				// check for outbox is not empty
				if (o2p_msgno > 0) {
					// calc pdu
					pdu = calc_submit_pdu(o2p_msgcontent, o2p_msgdest, o2p_msgflash, &ch_gsm->smsc_number, ch_gsm->sms_ref++);
					if (pdu) {
						curr = pdu;
						i=0;
						while (curr)
						{
							i++;
							curr = curr->next;
						}
						// check for max parts count
						if ((i <= ch_gsm->config.sms_max_part) && (is_address_string(o2p_msgdest))) {
							curr = pdu;
							while (curr)
							{
								// enqueue new pdu into queue sent
								memset(tmpbuf, 0, 512);
								res = 0;
								for (i=0; i<curr->full_len; i++)
									res += sprintf(tmpbuf+res, "%02X", (unsigned char)curr->buf[i]);
								// insert new pdu
								ast_mutex_lock(&pg_sms_db_lock);
								str0 = sqlite3_mprintf("INSERT INTO '%q-preparing' ("
													"owner, " // TEXT
													"msgid, " // INTEGER
													"status, " // INTEGER
													"scatype, " // INTEGER
													"scaname, " // TEXT
													"datype, " // INTEGER
													"daname, " // TEXT
													"dcs, " // INTEGER
													"partid, " // INTEGER
													"partof, " // INTEGER
													"part, " // INTEGER
													"submitpdulen, " // INTEGER
													"submitpdu, " // TEXT
													"attempt, " // INTEGER
													"hash, " // VARCHAR(32)
													"flash, " // INTEGER
													"content" // TEXT
												") VALUES ("
													"'%q', " // owner TEXT
													"%d, " // msgid INTEGER
													"%d, " // status INTEGER
													"%d, " // scatype INTEGER
													"'%q', " // scaname TEXT
													"%d, " // datype INTEGER
													"'%q', " // daname TEXT
													"%d, " // dcs INTEGER
													"%d, " // partid INTEGER
													"%d, " // partof INTEGER
													"%d, " // part INTEGER
													"%d, " // submitpdulen INTEGER
													"'%q', " // submitpdu TEXT
													"%d, " // attempt INTEGER
													"'%q', " // hash VARCHAR(32)
													"%d, " // flash INTEGER
													"'%q'" // content TEXT
													");",
											ch_gsm->iccid,
											"this", // owner TEXT
											maxmsgid, // msgid INTEGER
											0, // status INTEGER
											curr->scaddr.type.full, // scatype INTEGER
											curr->scaddr.value, // scaname TEXT
											curr->raddr.type.full, // datype INTEGER
											curr->raddr.value, // daname TEXT
											curr->dacosc, // dcs INTEGER
											curr->concat_ref, // partid INTEGER
											curr->concat_cnt, // partof INTEGER
											curr->concat_num, // part INTEGER
											curr->len, // submitpdulen INTEGER
											tmpbuf, // submitpdu TEXT
											0, // attempt INTEGER
											o2p_msghash, // hash VARCHAR(32)
											o2p_msgflash, // flash INTEGER
											curr->ud); // content TEXT
								while (1)
								{
									res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
									if (res == SQLITE_OK) {
										row = 0;
										while (1)
										{
											res = sqlite3_step(sql0);
											if (res == SQLITE_ROW)
												row++;
											else if (res == SQLITE_DONE)
												break;
											else if (res == SQLITE_BUSY) {
												ast_mutex_unlock(&ch_gsm->lock);
												usleep(1000);
												ast_mutex_lock(&ch_gsm->lock);
												continue;
											} else {
												ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
												break;
											}
										}
										sqlite3_finalize(sql0);
										break;
									} else if (res == SQLITE_BUSY) {
										ast_mutex_unlock(&ch_gsm->lock);
										usleep(1000);
										ast_mutex_lock(&ch_gsm->lock);
										continue;
									} else {
										ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
										break;
									}
								}
								sqlite3_free(str0);
								ast_mutex_unlock(&pg_sms_db_lock);
								// try next
								curr = curr->next;
							}
						} else { // too many parts or invalid destination
							if (is_address_string(o2p_msgdest))
								ast_log(LOG_NOTICE, "GSM channel=\"%s\": message fragmented in %d parts - but max part count is %d\n", ch_gsm->alias, i, ch_gsm->config.sms_max_part);
							else
								ast_log(LOG_NOTICE, "GSM channel=\"%s\": invalid destination \"%s\"\n", ch_gsm->alias, o2p_msgdest);
							// move to discard
							ast_mutex_lock(&pg_sms_db_lock);
							str0 = sqlite3_mprintf("INSERT INTO '%q-discard' ("
																"destination, " // TEXT
																"content, " // TEXT
																"flash, " // INTEGER
																"cause, " // TEXT
																"timestamp, " // INTEGER
																"hash" // VARCHAR(32) UNIQUE
															") VALUES ("
																"'%q', " // destination TEXT
																"'%q', " // content TEXT
																"%d, " // flash INTEGER
																"'%q', " // cause TEXT
																"%ld, " // timestamp INTEGER
																"'%q');", // hash  VARCHAR(32) UNIQUE
															ch_gsm->iccid,
															o2p_msgdest, // destination TEXT
															o2p_msgcontent, // content TEXT
															o2p_msgflash, // flash TEXT
															(is_address_string(o2p_msgdest))?("too many parts"):("invalid destination"), // cause TEXT
															curr_tv.tv_sec, // timestamp INTEGER
															o2p_msghash); // hash  VARCHAR(32) UNIQUE
							while (1)
							{
								res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
								if (res == SQLITE_OK) {
									row = 0;
									while (1)
									{
										res = sqlite3_step(sql0);
										if (res == SQLITE_ROW)
											row++;
										else if (res == SQLITE_DONE)
											break;
										else if (res == SQLITE_BUSY) {
											ast_mutex_unlock(&ch_gsm->lock);
											usleep(1000);
											ast_mutex_lock(&ch_gsm->lock);
											continue;
										} else {
											ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
											break;
										}
									}
									sqlite3_finalize(sql0);
									break;
								} else if (res == SQLITE_BUSY) {
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1000);
									ast_mutex_lock(&ch_gsm->lock);
									continue;
								} else {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
									break;
								}
							}
							sqlite3_free(str0);
							ast_mutex_unlock(&pg_sms_db_lock);
						} // end too many parts
						pdu_free(pdu);
					}
					if (o2p_msgdest) ast_free(o2p_msgdest);
					if (o2p_msgcontent) ast_free(o2p_msgcontent);
					// delete message from outbox queue
					ast_mutex_lock(&pg_sms_db_lock);
					str0 = sqlite3_mprintf("DELETE FROM '%q-outbox' WHERE msgno=%d;", ch_gsm->iccid, o2p_msgno);
					while (1)
					{
						res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
						if (res == SQLITE_OK) {
							row = 0;
							while (1)
							{
								res = sqlite3_step(sql0);
								if (res == SQLITE_ROW)
									row++;
								else if (res == SQLITE_DONE)
									break;
								else if (res == SQLITE_BUSY) {
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1000);
									ast_mutex_lock(&ch_gsm->lock);
									continue;
								} else {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
									break;
								}
							}
							sqlite3_finalize(sql0);
							break;
						} else if (res == SQLITE_BUSY) {
							ast_mutex_unlock(&ch_gsm->lock);
							usleep(1000);
							ast_mutex_lock(&ch_gsm->lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_free(str0);
					ast_mutex_unlock(&pg_sms_db_lock);
				} // end of o2p_msgno > 0
				// try to sending pdu
				if (!ch_gsm->pdu_send_id) {
					// get pdu from database with unsent status
					ast_mutex_lock(&pg_sms_db_lock);
					str0 = sqlite3_mprintf("SELECT attempt,msgno,submitpdu,submitpdulen,datype,daname FROM '%q-preparing' WHERE status=0 ORDER BY msgno;", ch_gsm->iccid);
					while (1)
					{
						res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
						if (res == SQLITE_OK) {
							row = 0;
							while (1)
							{
								res = sqlite3_step(sql0);
								if (res == SQLITE_ROW) {
									row++;
									if (!ch_gsm->pdu_send_id) {
										ch_gsm->pdu_send_attempt = sqlite3_column_int(sql0, 0);
										// check attempts counter
										if (ch_gsm->pdu_send_attempt < ch_gsm->config.sms_send_attempt) {
											// set now send pdu id
											ch_gsm->pdu_send_id = sqlite3_column_int(sql0, 1);
											// copy submit pdu
											ast_copy_string(ch_gsm->pdu_send_buf, (char *)sqlite3_column_text(sql0, 2), 512);
											ch_gsm->pdu_send_len = strlen(ch_gsm->pdu_send_buf);
											// send message command
											pg_atcommand_queue_append(ch_gsm, AT_CMGS, AT_OPER_WRITE, 0, 30, 0, "%d", sqlite3_column_int(sql0, 3));
											ast_verb(4, "GSM channel=\"%s\": send pdu to \"%s%s\"\n", ch_gsm->alias, (sqlite3_column_int(sql0, 4) == 145)?("+"):(""), sqlite3_column_text(sql0, 5));
										}
									}
								} else if(res == SQLITE_DONE)
									break;
								else if (res == SQLITE_BUSY) {
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1000);
									ast_mutex_lock(&ch_gsm->lock);
									continue;
								} else {
									ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
									break;
								}
							}
							sqlite3_finalize(sql0);
							break;
						} else if (res == SQLITE_BUSY) {
							ast_mutex_unlock(&ch_gsm->lock);
							usleep(1000);
							ast_mutex_lock(&ch_gsm->lock);
							continue;
						} else {
							ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
							break;
						}
					}
					sqlite3_free(str0);
					ast_mutex_unlock(&pg_sms_db_lock);
					// update send attempts counter
					if (ch_gsm->pdu_send_id) {
						ast_mutex_lock(&pg_sms_db_lock);
						str0 = sqlite3_mprintf("UPDATE '%q-preparing' SET attempt=%d WHERE msgno=%d;", ch_gsm->iccid, ch_gsm->pdu_send_attempt+1, ch_gsm->pdu_send_id);
						while (1)
						{
							res = sqlite3_prepare_fun(pg_sms_db, str0, strlen(str0), &sql0, NULL);
							if (res == SQLITE_OK) {
								row = 0;
								while (1)
								{
									res = sqlite3_step(sql0);
									if (res == SQLITE_ROW)
										row++;
									else if (res == SQLITE_DONE)
										break;
									else if (res == SQLITE_BUSY) {
										ast_mutex_unlock(&ch_gsm->lock);
										usleep(1000);
										ast_mutex_lock(&ch_gsm->lock);
										continue;
									} else {
										ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_step(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
										break;
									}
								}
								sqlite3_finalize(sql0);
								break;
							} else if (res == SQLITE_BUSY) {
								ast_mutex_unlock(&ch_gsm->lock);
								usleep(1000);
								ast_mutex_lock(&ch_gsm->lock);
								continue;
							} else {
								ast_log(LOG_ERROR, "GSM channel=\"%s\": sqlite3_prepare_fun(): %d: %s\n", ch_gsm->alias, res, sqlite3_errmsg(pg_sms_db));
								break;
							}
						}
						sqlite3_free(str0);
						ast_mutex_unlock(&pg_sms_db_lock);
					}
				}
				// restart smssend timer
				x_timer_set(ch_gsm->timers.smssend, ch_gsm->config.sms_send_interval);
			}
		}
		// waitviodown
		if (is_x_timer_enable(ch_gsm->timers.waitviodown) && is_x_timer_fired(ch_gsm->timers.waitviodown)) {
			// waitviodown timer fired
			ast_log(LOG_ERROR, "GSM channel=\"%s\": power status VIO not turn to down\n", ch_gsm->alias);
			// stop waitviodown timer
			x_timer_stop(ch_gsm->timers.waitviodown);
			// stop testviodown timer
			x_timer_stop(ch_gsm->timers.testviodown);
			// check for restart
			if (!ch_gsm->flags.restart_now) {
				// shutdown channel
				ast_mutex_unlock(&ch_gsm->lock);
				goto pg_channel_gsm_workthread_end;
			}
			ch_gsm->state = PG_CHANNEL_GSM_STATE_DISABLE;
			ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
			// disable power suply
			if (pg_channel_gsm_power_set(ch_gsm, 0)) {
				ast_log(LOG_ERROR, "GSM channel=\"%s\": can't set GSM power suply to off: %s\n", ch_gsm->alias, strerror(errno));
				ast_mutex_unlock(&ch_gsm->lock);
				goto pg_channel_gsm_workthread_end;
			}
			ch_gsm->flags.power = 0;
			//
			ast_mutex_unlock(&ch_gsm->lock);
			sleep(3);
			ast_mutex_lock(&ch_gsm->lock);
			//
			res = ch_gsm->power_sequence_number = pg_get_channel_gsm_power_sequence_number();
			ast_mutex_unlock(&ch_gsm->lock);
			if (res > 0)
				usleep(799999 * res);
			ast_mutex_lock(&ch_gsm->lock);
			ch_gsm->power_sequence_number = -1;
			// enable power suply
			if (pg_channel_gsm_power_set(ch_gsm, 1)) {
				ast_log(LOG_ERROR, "GSM channel=\"%s\": can't set GSM power suply to on: %s\n", ch_gsm->alias, strerror(errno));
				ast_mutex_unlock(&ch_gsm->lock);
				goto pg_channel_gsm_workthread_end;
			}
			ch_gsm->flags.power = 1;
			//
			ast_mutex_unlock(&ch_gsm->lock);
			sleep(3);
			ast_mutex_lock(&ch_gsm->lock);
		}
		// testviodown
		if (is_x_timer_enable(ch_gsm->timers.testviodown) && is_x_timer_fired(ch_gsm->timers.testviodown)) {
			// testviodown timer fired
			if ((ch_gsm->vio = pg_channel_gsm_vio_get(ch_gsm)) < 0) {
				ast_log(LOG_ERROR, "GSM channel=\"%s\": can't get channel power VIO status\n", ch_gsm->alias);
				ast_mutex_unlock(&ch_gsm->lock);
				goto pg_channel_gsm_workthread_end;
			}
			// power status is down -- successfull
			if (!ch_gsm->vio) {
				ast_verb(4, "GSM channel=\"%s\": power status VIO set to down\n", ch_gsm->alias);
				ch_gsm->state = PG_CHANNEL_GSM_STATE_DISABLE;
				ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				// stop testviodown timer
				x_timer_stop(ch_gsm->timers.testviodown);
				// stop waitviodown timer
				x_timer_stop(ch_gsm->timers.waitviodown);
				// check for restart
				if (!ch_gsm->flags.restart_now) {
					// shutdown channel
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				//
				ast_mutex_unlock(&ch_gsm->lock);
				sleep(3);
				ast_mutex_lock(&ch_gsm->lock);
			} else {
				// restart testviodown timer
				x_timer_set(ch_gsm->timers.testviodown, testviodown_timeout);
			}
		}

		// handle signals
		// shutdown
		if (ch_gsm->flags.shutdown) {
			ch_gsm->flags.shutdown = 0;
			ast_verb(4, "GSM channel=\"%s\" received shutdown signal\n", ch_gsm->alias);
			// reset restart flag
			ch_gsm->flags.restart = 0;
			ch_gsm->flags.restart_now = 0;
			// hangup active calls
			AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
				pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NORMAL_CLEARING);
			while (ch_gsm->call_list.first)
			{
				ast_mutex_unlock(&ch_gsm->lock);
				usleep(1000);
				ast_mutex_lock(&ch_gsm->lock);
			}
			// flush AT command queue
			if (ch_gsm->at_cmd) {
				ast_free(ch_gsm->at_cmd);
				ch_gsm->at_cmd = NULL;
			}
			pg_atcommand_queue_flush(ch_gsm);
			// stop all timers
			memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
			// disable GSM module - key press imitation
			// key press
			if (pg_channel_gsm_key_press(ch_gsm, 1) < 0) {
				ast_log(LOG_ERROR, "GSM channel=\"%s\": key press failure: %s\n", ch_gsm->alias, strerror(errno));
				ast_mutex_unlock(&ch_gsm->lock);
				goto pg_channel_gsm_workthread_end;
			}
			ast_mutex_unlock(&ch_gsm->lock);
			usleep(1999999);
			ast_mutex_lock(&ch_gsm->lock);
			// key release
			if (pg_channel_gsm_key_press(ch_gsm, 0) < 0) {
				ast_log(LOG_ERROR, "GSM channel=\"%s\": key release failure: %s\n", ch_gsm->alias, strerror(errno));
				ast_mutex_unlock(&ch_gsm->lock);
				goto pg_channel_gsm_workthread_end;
			}
			// check power status for complete power down procedure
			ch_gsm->vio = 1;
			ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_VIO_DOWN;
			ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
			// start testviodown timer
			x_timer_set(ch_gsm->timers.testviodown, testviodown_timeout);
			// start waitviodown timer
			x_timer_set(ch_gsm->timers.waitviodown, waitviodown_timeout);
		} // end of shutdown
		// restart
		if (ch_gsm->flags.restart && !ch_gsm->flags.shutdown_now) {
			ch_gsm->flags.restart = 0;
			ast_verb(4, "GSM channel=\"%s\" received restart signal\n", ch_gsm->alias);
			// hangup active calls
			AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
				pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NORMAL_CLEARING);
			while (ch_gsm->call_list.first)
			{
				ast_mutex_unlock(&ch_gsm->lock);
				usleep(1000);
				ast_mutex_lock(&ch_gsm->lock);
			}
			// flush AT command queue
			if (ch_gsm->at_cmd) {
				ast_free(ch_gsm->at_cmd);
				ch_gsm->at_cmd = NULL;
			}
			pg_atcommand_queue_flush(ch_gsm);
			// stop all timers
			memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
			// set init signal
			ch_gsm->flags.init = 1;
			// disable GSM module - key press imitation
			// key press
			if (pg_channel_gsm_key_press(ch_gsm, 1) < 0) {
				ast_log(LOG_ERROR, "GSM channel=\"%s\": key press failure: %s\n", ch_gsm->alias, strerror(errno));
				ast_mutex_unlock(&ch_gsm->lock);
				goto pg_channel_gsm_workthread_end;
			}
			ast_mutex_unlock(&ch_gsm->lock);
			usleep(1999999);
			ast_mutex_lock(&ch_gsm->lock);
			// key release
			if (pg_channel_gsm_key_press(ch_gsm, 0) < 0) {
				ast_log(LOG_ERROR, "GSM channel=\"%s\": key release failure: %s\n", ch_gsm->alias, strerror(errno));
				ast_mutex_unlock(&ch_gsm->lock);
				goto pg_channel_gsm_workthread_end;
			}
			// check power status for complete power down procedure
			ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_VIO_DOWN;
			ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
			// start testviodown timer
			x_timer_set(ch_gsm->timers.testviodown, testviodown_timeout);
			// start waitviodown timer
			x_timer_set(ch_gsm->timers.waitviodown, waitviodown_timeout);
		} // end of restart

		switch (ch_gsm->state)
		{
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			case PG_CHANNEL_GSM_STATE_DISABLE:
				// reset registaration status
				ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
				// reset callwait state
				ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
				// reset clir state
				ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
				// reset rssi value
				ch_gsm->rssi = 99;
				// reset ber value
				ch_gsm->ber = 99;
				// reset restart_now flag
				if (ch_gsm->flags.restart_now)
					ch_gsm->flags.restart_now = 0;
				// set hardware baudrate
				if (!tcgetattr(ch_gsm->tty_fd, &termios)) {
					switch (ch_gsm->baudrate)
					{
						case 9600:
							baudrate = B9600;
							break;
						default:
							baudrate = B115200;
							break;
					}
					cfsetispeed(&termios, baudrate);
					cfsetospeed(&termios, baudrate);
					if (tcsetattr(ch_gsm->tty_fd, TCSANOW, &termios) < 0) {
						ast_log(LOG_ERROR, "GSM channel=\"%s\": tcsetattr() error: %s\n", ch_gsm->alias, strerror(errno));
						ast_mutex_unlock(&ch_gsm->lock);
						goto pg_channel_gsm_workthread_end;
					}
				} else {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": tcgetattr() error: %s\n", ch_gsm->alias, strerror(errno));
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				// flush at command queue
				if (ch_gsm->at_cmd) {
					ast_free(ch_gsm->at_cmd);
					ch_gsm->at_cmd = NULL;
				}
				pg_atcommand_queue_flush(ch_gsm);
				ch_gsm->cmd_done = 1;
				if (tcflush(ch_gsm->tty_fd, TCIOFLUSH) < 0)
					ast_log(LOG_ERROR, "GSM channel=\"%s\": can't flush tty device: %s\n", ch_gsm->alias, strerror(errno));
				// check VIO status
				if ((ch_gsm->vio = pg_channel_gsm_vio_get(ch_gsm)) < 0) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": can't get channel power VIO status\n", ch_gsm->alias);
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				if (ch_gsm->vio) {
					// init test functionality
					ast_verb(4, "GSM channel=\"%s\": run functionality test\n", ch_gsm->alias);
					// run functionality test
					ch_gsm->flags.func_test_run = 1;
					ch_gsm->flags.func_test_done = 0;
					ch_gsm->baudrate_test = ch_gsm->baudrate;
					ch_gsm->baudrate = 0;
					ch_gsm->state = PG_CHANNEL_GSM_STATE_TEST_FUN;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
					// flush at command queue
					if (ch_gsm->at_cmd) {
						ast_free(ch_gsm->at_cmd);
						ch_gsm->at_cmd = NULL;
					}
					pg_atcommand_queue_flush(ch_gsm);
					// run testfun timer
					x_timer_set(ch_gsm->timers.testfun, testfun_timeout);
					// run testfunsend timer
					x_timer_set(ch_gsm->timers.testfunsend, testfunsend_timeout);
				} else {
					// reset functionality test flags
					ch_gsm->flags.func_test_run = 0;
					ch_gsm->flags.func_test_done = 1;
					// enable GSM module - key press imitation
					// key press
					if (pg_channel_gsm_key_press(ch_gsm, 1) < 0) {
						ast_log(LOG_ERROR, "GSM channel=\"%s\": key press failure: %s\n", ch_gsm->alias, strerror(errno));
						ast_mutex_unlock(&ch_gsm->lock);
						goto pg_channel_gsm_workthread_end;
					}
					ast_mutex_unlock(&ch_gsm->lock);
					usleep(1999999);
					ast_mutex_lock(&ch_gsm->lock);
					// key release
					if (pg_channel_gsm_key_press(ch_gsm, 0) < 0) {
						ast_log(LOG_ERROR, "GSM channel=\"%s\": key release failure: %s\n", ch_gsm->alias, strerror(errno));
						ast_mutex_unlock(&ch_gsm->lock);
						goto pg_channel_gsm_workthread_end;
					}
					// run waitrdy timer
					x_timer_set(ch_gsm->timers.waitrdy, waitrdy_timeout);
					ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_RDY;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				}
				break;
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			case PG_CHANNEL_GSM_STATE_WAIT_RDY:
				break;
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			case PG_CHANNEL_GSM_STATE_TEST_FUN:
				// check if test functionality is done
				if (!ch_gsm->flags.func_test_done) break;
				// stop testfun timer
				x_timer_stop(ch_gsm->timers.testfun);
				// stop testfunsend timer
				x_timer_stop(ch_gsm->timers.testfunsend);

				ch_gsm->baudrate = ch_gsm->baudrate_test;
				ast_verb(4, "GSM channel=\"%s\": serial port work at speed = %d baud\n", ch_gsm->alias, ch_gsm->baudrate);
// 				pg_atcommand_queue_append(ch_gsm, AT_UNKNOWN, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "AT+IPR=%d;&W", ch_gsm->baudrate);

				// disable GSM module functionality
				pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");

				// try to run normal functionality
				ch_gsm->state = PG_CHANNEL_GSM_STATE_SUSPEND;
				ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				// start simpoll timer
				x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
				break;
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			case PG_CHANNEL_GSM_STATE_INIT:
				if (!ch_gsm->cmd_done)
					break;
				if (ch_gsm->init.echo) {
					pg_atcommand_queue_append(ch_gsm, AT_E, AT_OPER_EXEC, 0, pg_at_response_timeout, 0, "%d", 0);
					ch_gsm->init.echo = 0;
					break;
				}
				if (ch_gsm->init.cscs) {
					pg_atcommand_queue_append(ch_gsm, AT_CSCS, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%s", "\"HEX\"");
					ch_gsm->init.cscs = 0;
					break;
				}
				if (ch_gsm->init.clip) {
					pg_atcommand_queue_append(ch_gsm, AT_CLIP, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 1);
					ch_gsm->init.clip = 0;
					break;
				}
				if (ch_gsm->init.chfa) {
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
						pg_atcommand_queue_append(ch_gsm, AT_SIM300_CHFA, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 0);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_append(ch_gsm, AT_SIM900_CHFA, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 0);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
						pg_atcommand_queue_append(ch_gsm, AT_M10_QAUDCH, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 1);
					ch_gsm->init.chfa = 0;
					break;
				}
				if (ch_gsm->init.clvl) {
					pg_atcommand_queue_append(ch_gsm, AT_CLVL, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", ch_gsm->config.gainin);
					ch_gsm->init.clvl = 0;
					break;
				}
				if (ch_gsm->init.cmic) {
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
						pg_atcommand_queue_append(ch_gsm, AT_SIM300_CMIC, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0,%d", ch_gsm->config.gainout);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_append(ch_gsm, AT_SIM900_CMIC, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0,%d", ch_gsm->config.gainout);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
						pg_atcommand_queue_append(ch_gsm, AT_M10_QMIC, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1,%d", ch_gsm->config.gainout);
					ch_gsm->init.cmic = 0;
					break;
				}
				if (ch_gsm->init.cmgf) {
					pg_atcommand_queue_append(ch_gsm, AT_CMGF, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
					ch_gsm->init.cmgf = 0;
					break;
				}
				if (ch_gsm->init.cnmi) {
					pg_atcommand_queue_append(ch_gsm, AT_CNMI, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d,%d,%d,%d,%d", 2, 2, 0, 1, 0);
					ch_gsm->init.cnmi = 0;
					break;
				}
				if (ch_gsm->init.cmee) {
					pg_atcommand_queue_append(ch_gsm, AT_CMEE, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 1);
					ch_gsm->init.cmee = 0;
					break;
				}
				if (ch_gsm->init.ceer) {
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_append(ch_gsm, AT_CEER, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", 1);
					ch_gsm->init.ceer = 0;
					break;
				}
				if (ch_gsm->init.cclk) {
					if ((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
						pg_atcommand_queue_append(ch_gsm, AT_CCLK, AT_OPER_WRITE, 0, pg_at_response_timeout, 0,
							"\"%02d/%02d/%02d,%02d:%02d:%02d%c%02d\"",
								tm_ptr->tm_year%100,
								tm_ptr->tm_mon+1,
								tm_ptr->tm_mday,
								tm_ptr->tm_hour,
								tm_ptr->tm_min,
								tm_ptr->tm_sec,
								(curr_tz.tz_minuteswest > 0)?('-'):('+'),
								abs(curr_tz.tz_minuteswest)/15);
					else
						ast_log(LOG_WARNING, "GSM channel=\"%s\": can't set module clock\n", ch_gsm->alias);
					ch_gsm->init.cclk = 0;
					break;
				}
				ch_gsm->flags.init = 0;
				// start runquartersecond timer
				x_timer_set(ch_gsm->timers.runquartersecond, onesec_timeout);
				// start runhalfsecond timer
				x_timer_set(ch_gsm->timers.runhalfsecond, onesec_timeout);
				// start runonesecond timer
				x_timer_set(ch_gsm->timers.runonesecond, onesec_timeout);
				// start runhalfminute timer
				x_timer_set(ch_gsm->timers.runhalfminute, onesec_timeout);
				// start runoneminute timer
				x_timer_set(ch_gsm->timers.runoneminute, onesec_timeout);
				// start runvifesecond timer
				x_timer_set(ch_gsm->timers.runfivesecond, runfivesecond_timeout);
				// start registering timer
				x_timer_set(ch_gsm->timers.registering, registering_timeout);
				// set run state
				ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
				ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				break;
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			case PG_CHANNEL_GSM_STATE_RUN:
				// manage call wait status option
				if (((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING))
					 	&& (!pg_is_channel_gsm_has_calls(ch_gsm))) {
					// check callwait status
					if (ch_gsm->callwait == PG_CALLWAIT_STATE_UNKNOWN) {
						// request actual call wait status if not known
						ch_gsm->callwait = PG_CALLWAIT_STATE_QUERY;
						ch_gsm->state = PG_CHANNEL_GSM_STATE_SERVICE;
						ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						pg_atcommand_queue_append(ch_gsm, AT_CCWA, AT_OPER_WRITE, PG_AT_SUBCMD_CCWA_GET, 10, 0, "%d,%d", 0, 2);
						break;
					} else if ((ch_gsm->callwait != PG_CALLWAIT_STATE_QUERY) && (ch_gsm->callwait != ch_gsm->config.callwait)) {
						// set call wait status
						ch_gsm->callwait = PG_CALLWAIT_STATE_QUERY;
						ch_gsm->state = PG_CHANNEL_GSM_STATE_SERVICE;
						ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						pg_atcommand_queue_append(ch_gsm, AT_CCWA, AT_OPER_WRITE, PG_AT_SUBCMD_CCWA_SET, 10, 0, "%d,%d", 0, ch_gsm->config.callwait);
						break;
					}
					// check CLIR state
					if (ch_gsm->clir == PG_CLIR_STATE_UNKNOWN) {
						// request actual CLIR status if not known
						ch_gsm->clir = PG_CLIR_STATE_QUERY;
						ch_gsm->state = PG_CHANNEL_GSM_STATE_SERVICE;
						ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						pg_atcommand_queue_append(ch_gsm, AT_CLIR, AT_OPER_READ, 0, 10, 0, NULL);
						break;
					} else if ((ch_gsm->clir != PG_CLIR_STATE_QUERY) && (ch_gsm->clir != ch_gsm->config.clir)) {
						// set line presentation restriction mode
						ch_gsm->clir = PG_CLIR_STATE_QUERY;
						ch_gsm->state = PG_CHANNEL_GSM_STATE_SERVICE;
						ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
						pg_atcommand_queue_append(ch_gsm, AT_CLIR, AT_OPER_WRITE, 0, 10, 0, "%d", ch_gsm->config.clir);
						break;
					}
					// get balance
					if ((ch_gsm->flags.balance_req) &&
					 		(ch_gsm->config.balance_request) &&
					 			(strlen(ch_gsm->config.balance_request))) {
						memset(tmpbuf, 0, 256);
						ip = ch_gsm->config.balance_request;
						ilen = strlen(ch_gsm->config.balance_request);
						op = tmpbuf;
						olen = 256;
						if (!str_bin_to_hex(&ip, &ilen, &op, &olen)) {
							ch_gsm->state = PG_CHANNEL_GSM_STATE_SERVICE;
							ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
							ch_gsm->ussd_sub_cmd = PG_AT_SUBCMD_CUSD_GET_BALANCE;
							pg_atcommand_queue_append(ch_gsm, AT_CUSD, AT_OPER_WRITE, PG_AT_SUBCMD_CUSD_GET_BALANCE, 30, 0, "%d,\"%s\"", 1, tmpbuf);
						} else
							ast_log(LOG_ERROR, "GSM channel=\"%s\": fail convert USSD \"%s\" to hex\n", ch_gsm->alias, ch_gsm->config.balance_request);
						ch_gsm->flags.balance_req = 0;
						break;
					}
				}
				// adjust gainin
				if ((ch_gsm->gainin >= 0) && (ch_gsm->gainin != ch_gsm->config.gainin)) {
					pg_atcommand_queue_append(ch_gsm, AT_CLVL, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "%d", ch_gsm->config.gainin);
					ch_gsm->gainin = -1;
				}
				// adjust gainout
				if ((ch_gsm->gainout >= 0) && (ch_gsm->gainout != ch_gsm->config.gainout)) {
					if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300)
						pg_atcommand_queue_append(ch_gsm, AT_SIM300_CMIC, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0,%d", ch_gsm->config.gainout);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900)
						pg_atcommand_queue_append(ch_gsm, AT_SIM900_CMIC, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0,%d", ch_gsm->config.gainout);
					else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10)
						pg_atcommand_queue_append(ch_gsm, AT_M10_QMIC, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1,%d", ch_gsm->config.gainout);
					ch_gsm->gainout = -1;
				}
				break;
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			case PG_CHANNEL_GSM_STATE_SUSPEND:
				break;
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			case PG_CHANNEL_GSM_STATE_WAIT_VIO_DOWN:
				break;
			//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			default:
				break;
		}

		ast_mutex_unlock(&ch_gsm->lock);
	}

pg_channel_gsm_workthread_end:
	ast_mutex_lock(&ch_gsm->lock);

	// reset channel phone number
	ast_copy_string(ch_gsm->subscriber_number.value, "unknown", MAX_ADDRESS_LENGTH);
	ch_gsm->subscriber_number.length = strlen(ch_gsm->subscriber_number.value);
	ch_gsm->subscriber_number.type.bits.reserved = 1;
	ch_gsm->subscriber_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
	ch_gsm->subscriber_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;
	// reset SMS center number
	ast_copy_string(ch_gsm->smsc_number.value, "unknown", MAX_ADDRESS_LENGTH);
	ch_gsm->smsc_number.length = strlen(ch_gsm->smsc_number.value);
	ch_gsm->smsc_number.type.bits.reserved = 1;
	ch_gsm->smsc_number.type.bits.numbplan = NUMBERING_PLAN_UNKNOWN;
	ch_gsm->smsc_number.type.bits.typenumb = TYPE_OF_NUMBER_SUBSCRIBER;

	if (ch_gsm->operator_name) ast_free(ch_gsm->operator_name); ch_gsm->operator_name = NULL;
	if (ch_gsm->operator_code) ast_free(ch_gsm->operator_code); ch_gsm->operator_code = NULL;
	if (ch_gsm->imsi) ast_free(ch_gsm->imsi); ch_gsm->imsi = NULL;
	if (ch_gsm->iccid) ast_free(ch_gsm->iccid); ch_gsm->iccid = NULL;
// 	if (ch_gsm->pin) ast_free(ch_gsm->pin); ch_gsm->pin = NULL;
// 	if (ch_gsm->puk) ast_free(ch_gsm->puk); ch_gsm->puk = NULL;
	if (ch_gsm->config.balance_request) ast_free(ch_gsm->config.balance_request); ch_gsm->config.balance_request = NULL;

	// reset registaration status
	ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
	// reset callwait state
	ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
	// reset clir state
	ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
	// reset rssi value
	ch_gsm->rssi = 99;
	// reset ber value
	ch_gsm->ber = 99;

	// flush AT command queue
	if (ch_gsm->at_cmd) {
		ast_free(ch_gsm->at_cmd);
		ch_gsm->at_cmd = NULL;
	}
	pg_atcommand_queue_flush(ch_gsm);

	// close TTY device
	close(ch_gsm->tty_fd);

	// disable power suply
	pg_channel_gsm_power_set(ch_gsm, 0);

	// reset GSM channel flags
	memset(&ch_gsm->flags, 0, sizeof(struct pg_channel_gsm_flags));

	// stop receiver debug session
	if (ch_gsm->debug.receiver) {
		ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
		if (ch_gsm->debug.receiver_debug_fp) {
			fprintf(ch_gsm->debug.receiver_debug_fp, "\nchannel now disabled\n");
			fflush(ch_gsm->debug.receiver_debug_fp);
			fclose(ch_gsm->debug.receiver_debug_fp);
			ch_gsm->debug.receiver_debug_fp = NULL;
		}
	}
	// stop at debug session
	if (ch_gsm->debug.at) {
		ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
		if (ch_gsm->debug.at_debug_fp) {
			fprintf(ch_gsm->debug.at_debug_fp, "\nchannel now disabled\n");
			fflush(ch_gsm->debug.at_debug_fp);
			fclose(ch_gsm->debug.at_debug_fp);
			ch_gsm->debug.at_debug_fp = NULL;
		}
	}
	ch_gsm->thread = AST_PTHREADT_NULL;
	ast_debug(4, "GSM channel=\"%s\": thread stop\n", ch_gsm->alias);
	ast_verbose("Polygator: GSM channel=\"%s\" disabled\n", ch_gsm->alias);
	ast_mutex_unlock(&ch_gsm->lock);
	return NULL;
}
//------------------------------------------------------------------------------
// end of pg_channel_gsm_workthread()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_vinetic_workthread()
//------------------------------------------------------------------------------
static void *pg_vinetic_workthread(void *data)
{
	size_t i;
	struct pg_vinetic *vin = (struct pg_vinetic *)data;

	ast_debug(4, "vinetic=\"%s\": thread start\n", vin->name);

	while (vin->run)
	{
		ast_mutex_lock(&vin->lock);

		switch (vin->state)
		{
			case PG_VINETIC_STATE_INIT:
				ast_debug(3, "vinetic=\"%s\": init\n", vin->name);

				vin_init(&vin->context, "%s", vin->path);
				vin_set_dev_name(&vin->context, vin->name);
				if (vin_set_pram(&vin->context, "%s/polygator/edspPRAMfw_%s.bin", ast_config_AST_DATA_DIR, vin->firmware) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_pram(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				if (vin_set_dram(&vin->context, "%s/polygator/edspDRAMfw_%s.bin", ast_config_AST_DATA_DIR, vin->firmware) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_dram(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				if (vin_set_alm_dsp_ab(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->almab) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_alm_dsp_ab(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				if (vin_set_alm_dsp_cd(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->almcd) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_alm_dsp_cd(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				if (vin_set_cram(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->cram) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_cram(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// open
				if (vin_open(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_open(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				vin->state = PG_VINETIC_STATE_IDLE;
				break;
			case PG_VINETIC_STATE_IDLE:
				ast_debug(3, "vinetic=\"%s\": idle\n", vin->name);
				ast_verb(3, "vinetic=\"%s\": start downloading firmware\n", vin->name);
				// disable polling
				if (vin_poll_disable(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_poll_disable(): %s\n", vin->name, vin_error_str(&vin->context));
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// reset
				if (vin_reset(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset(): %s\n", vin->name, vin_error_str(&vin->context));
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// reset rdyq
				if (vin_reset_rdyq(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_rdyq(): %s\n", vin->name, vin_error_str(&vin->context));
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// check rdyq status
				for (i=0; i<5000; i++)
				{
					if (!vin_is_not_ready(&vin->context)) break;
					usleep(1000);
				}
				if (i == 5000) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": not ready\n", vin->name);
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// disable all interrupt
				if (vin_phi_disable_interrupt(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_phi_disable_interrupt(): %s\n", vin->name, vin_error_str(&vin->context));
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// revision
				if (!vin_phi_revision(&vin->context)) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_phi_revision(): %s\n", vin->name, vin_error_str(&vin->context));
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				ast_verb(3, "vinetic=\"%s\": revision %s\n", vin->name, vin_revision_str(&vin->context));
				// download EDSP firmware
				if (vin_download_edsp_firmware(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_edsp_firmware(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_edsp_firmware(): line %d\n", vin->name, vin->context.errorline);
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// enable polling
				if (vin_poll_enable(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_poll_enable(): %s\n", vin->name, vin_error_str(&vin->context));
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// firmware version
				if (vin_read_fw_version(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_read_fw_version(): %s\n", vin->name, vin_error_str(&vin->context));
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				ast_verb(3, "vinetic=\"%s\": EDSP firmware version %u.%u.%u\n", vin->name,
					(vin->context.edsp_sw_version_register.mv << 13) +
					(vin->context.edsp_sw_version_register.prt << 12) +
					(vin->context.edsp_sw_version_register.features << 0),
					vin->context.edsp_sw_version_register.main_version,
					vin->context.edsp_sw_version_register.release);
				// set unsupported resources as busy for version 12.14.14
				if ((vin->context.edsp_sw_version_register.mv == 0) &&
					(vin->context.edsp_sw_version_register.prt == 0) &&
						(vin->context.edsp_sw_version_register.features == 12) &&
							(vin->context.edsp_sw_version_register.main_version == 14) &&
								(vin->context.edsp_sw_version_register.release == 14)) {
					vin->context.resources[4] = 1;
					vin->context.resources[5] = 1;
					vin->context.resources[6] = 1;
					vin->context.resources[7] = 1;
				}
#if ASTERISK_VERSION_NUM >= 100000
				vin->capabilities = ast_format_cap_destroy(vin->capabilities);
#else
				vin->capabilities = 0;
#endif
				if ((vin->context.edsp_sw_version_register.mv == 0) &&
						(vin->context.edsp_sw_version_register.prt == 0)) {
#if ASTERISK_VERSION_NUM >= 100000
					struct ast_format tmpfmt;
					vin->capabilities = ast_format_cap_alloc();
					switch (vin->context.edsp_sw_version_register.features)
					{
						case 0:
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G723_1, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G726, 0));
							break;
						case 4:
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G723_1, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G726, 0));
							break;
						case 12:
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G726, 0));
							break;
						case 16:
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G726, 0));
							break;
						case 20:
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0));
							break;
						case 24:
							ast_format_cap_add(vin->capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G723_1, 0));
							break;
						default:
							break;
					}
					if (vin->capabilities)
						ast_format_cap_append(pg_gsm_tech.capabilities, vin->capabilities);
#else
					switch (vin->context.edsp_sw_version_register.features)
					{
						case 0:
							vin->capabilities |= AST_FORMAT_G729A;
							vin->capabilities |= AST_FORMAT_G723_1;
							vin->capabilities |= AST_FORMAT_ALAW;
							vin->capabilities |= AST_FORMAT_ULAW;
							vin->capabilities |= AST_FORMAT_G726;
							break;
						case 4:
							vin->capabilities |= AST_FORMAT_G729A;
							vin->capabilities |= AST_FORMAT_G723_1;
							vin->capabilities |= AST_FORMAT_ALAW;
							vin->capabilities |= AST_FORMAT_ULAW;
							vin->capabilities |= AST_FORMAT_G726;
							break;
						case 12:
							vin->capabilities |= AST_FORMAT_G729A;
							vin->capabilities |= AST_FORMAT_ALAW;
							vin->capabilities |= AST_FORMAT_ULAW;
							vin->capabilities |= AST_FORMAT_G726;
							break;
						case 16:
							vin->capabilities |= AST_FORMAT_G729A;
							vin->capabilities |= AST_FORMAT_ALAW;
							vin->capabilities |= AST_FORMAT_ULAW;
							vin->capabilities |= AST_FORMAT_G726;
							break;
						case 20:
							vin->capabilities |= AST_FORMAT_G729A;
							break;
						case 24:
							vin->capabilities |= AST_FORMAT_G723_1;
							break;
						default:
							break;
					}
					pg_gsm_tech.capabilities |= vin->capabilities;
#endif
				}
				// download ALM DSP AB firmware
				if (vin_download_alm_dsp(&vin->context, vin->context.alm_dsp_ab_path) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_alm_dsp(AB): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_alm_dsp(AB): line %d\n", vin->name, vin->context.errorline);
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				if (vin_jump_alm_dsp(&vin->context, 0) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_jump_alm_dsp(AB): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_jump_alm_dsp(AB): line %d\n", vin->name, vin->context.errorline);
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// download ALM DSP CD firmware...", vin_dev_name(&vin)); 
				if (vin_download_alm_dsp(&vin->context, vin->context.alm_dsp_cd_path) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_alm_dsp(CD): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_alm_dsp(CD): line %d\n", vin->name, vin->context.errorline);
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				if (vin_jump_alm_dsp(&vin->context, 2) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_jump_alm_dsp(CD): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_jump_alm_dsp(CD): line %d\n", vin->name, vin->context.errorline);
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				// download CRAM
				for (i=0; i<4; i++)
				{
					if (vin_download_cram(&vin->context, i, vin->context.cram_path) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_cram(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_cram(): line %d\n", vin->name, vin->context.errorline);
						vin->state = PG_VINETIC_STATE_IDLE;
						break;
					}
					if (vin_write_sop_generic(&vin->context, i, 0x07, 0x2001) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): line %d\n", vin->name, vin->context.errorline);
						vin->state = PG_VINETIC_STATE_IDLE;
						break;
					}
					if (vin_write_sop_generic(&vin->context, i, 0x08, 0x4000) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): line %d\n", vin->name, vin->context.errorline);
						vin->state = PG_VINETIC_STATE_IDLE;
						break;
					}
				}
				// patch ALM to work with GSM module direct
				for (i=0; i<4; i++)
				{
					if (vin->patch_alm_gsm[i]) {
						if (vin_write_sop_generic(&vin->context, i, 0x07, 0x2011) < 0) {
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): %s\n", vin->name, vin_error_str(&vin->context));
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): line %d\n", vin->name, vin->context.errorline);
							vin->state = PG_VINETIC_STATE_IDLE;
							break;
						}
						if (vin_write_sop_generic(&vin->context, i, 0x08, 0x40C0) < 0) {
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): %s\n", vin->name, vin_error_str(&vin->context));
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): line %d\n", vin->name, vin->context.errorline);
							vin->state = PG_VINETIC_STATE_IDLE;
							break;
						}
						if (vin_write_sop_generic(&vin->context, i, 0x09, 0x0000) < 0) {
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): %s\n", vin->name, vin_error_str(&vin->context));
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): line %d\n", vin->name, vin->context.errorline);
							vin->state = PG_VINETIC_STATE_IDLE;
							break;
						}
						if (vin_write_sop_generic(&vin->context, i, 0x0A, 0x0000) < 0) {
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): %s\n", vin->name, vin_error_str(&vin->context));
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): line %d\n", vin->name, vin->context.errorline);
							vin->state = PG_VINETIC_STATE_IDLE;
							break;
						}
						if (vin_write_sop_generic(&vin->context, i, 0x0F, 0x000C) < 0) {
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): %s\n", vin->name, vin_error_str(&vin->context));
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): line %d\n", vin->name, vin->context.errorline);
							vin->state = PG_VINETIC_STATE_IDLE;
							break;
						}
						if (vin_write_sop_generic(&vin->context, i, 0x10, 0x0400) < 0) {
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): %s\n", vin->name, vin_error_str(&vin->context));
							ast_log(LOG_ERROR, "vinetic=\"%s\": vin_write_sop_generic(): line %d\n", vin->name, vin->context.errorline);
							vin->state = PG_VINETIC_STATE_IDLE;
							break;
						}
					}
				}
				// switch to little endian mode
				if (vin_set_little_endian_mode(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_little_endian_mode(): %s\n", vin->name, vin_error_str(&vin->context));
					vin->state = PG_VINETIC_STATE_IDLE;
					break;
				}
				ast_verb(3, "vinetic=\"%s\": firmware downloading succeeded\n", vin->name);
				vin->state = PG_VINETIC_STATE_RUN;
				ast_debug(3, "vinetic=\"%s\": run\n", vin->name);
				// set vinetic's module state to previous state - for fallback purpose after reset
				// ALI module
				if (is_vin_ali_enabled(vin->context)) {
					// enable vinetic ALI module
					if (vin_ali_enable(vin->context) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_enable(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_enable(): line %d\n", vin->name, vin->context.errorline);
						vin->state = PG_VINETIC_STATE_IDLE;
						break;
					}
					for (i=0; i<4 ; i++)
					{
						if (is_vin_ali_channel_enabled(vin->context, i)) {
							// enable vinetic ALI channel
							if (vin_ali_channel_enable(vin->context, i) < 0) {
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_channel_enable(): line %d\n", vin->name, vin->context.errorline);
								vin->state = PG_VINETIC_STATE_IDLE;
								break;
							}
							// enable vinetic ALI Near End LEC
							if (vin_ali_near_end_lec_enable(vin->context, i) < 0) {
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): line %d\n", vin->name, vin->context.errorline);
								vin->state = PG_VINETIC_STATE_IDLE;
								break;
							}
							// set ALI channel operation mode ACTIVE_HIGH_VBATH
							if (vin_set_opmode(&vin->context, i, vin->context.ali_opmode[i]) < 0) {
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_opmode(): %s\n", vin->name, vin_error_str(&vin->context));
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_opmode(): line %d\n", vin->name, vin->context.errorline);
								vin->state = PG_VINETIC_STATE_IDLE;
								break;
							}
						}
					}
				}
				// signaling module
				if (is_vin_signaling_enabled(vin->context)) {
					// enable coder module
					if (vin_signaling_enable(vin->context) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_enable(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_enable(): line %d\n", vin->name, vin->context.errorline);
						vin->state = PG_VINETIC_STATE_IDLE;
						break;
					}
					for (i=0; i<4 ; i++)
					{
						if (is_vin_signaling_channel_enabled(vin->context, i)) {
							// enable signaling channel
							if (vin_signaling_channel_enable(vin->context, i) < 0) {
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_enable(): line %d\n", vin->name, vin->context.errorline);
								vin->state = PG_VINETIC_STATE_IDLE;
								break;
							}
							// enable DTMF receiver
							if (vin_dtmf_receiver_enable(vin->context, i) < 0) {
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_dtmf_receiver_enable(): %s\n", vin->name, vin_error_str(&vin->context));
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_dtmf_receiver_enable(): line %d\n", vin->name, vin->context.errorline);
								vin->state = PG_VINETIC_STATE_IDLE;
								break;
							}
							// enable signalling channel RTP
							if (vin_signaling_channel_config_rtp(vin->context, i) < 0) {
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_config_rtp(): line %d\n", vin->name, vin->context.errorline);
								vin->state = PG_VINETIC_STATE_IDLE;
								break;
							}
						}
					}
				}
				// coder module
				if (is_vin_coder_enabled(vin->context)) {
					// enable coder module
					if ((vin_coder_enable(vin->context)) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_enable(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_enable(): line %d\n", vin->name, vin->context.errorline);
						vin->state = PG_VINETIC_STATE_IDLE;
						break;
					}
					// set coder configuration RTP
					if (vin_coder_config_rtp(vin->context) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_config_rtp(): line %d\n", vin->name, vin->context.errorline);
						vin->state = PG_VINETIC_STATE_IDLE;
						break;
					}
					for (i=0; i<4 ; i++)
					{
						if (is_vin_coder_channel_enabled(vin->context, i)) {
							// set coder channel RTP
							if (vin_coder_channel_config_rtp(vin->context, i) < 0) {
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_config_rtp(): line %d\n", vin->name, vin->context.errorline);
								vin->state = PG_VINETIC_STATE_IDLE;
								break;
							}
							// enable coder channel speech compression
							if (vin_coder_channel_enable(vin->context, i) < 0) {
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
								ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_enable(): line %d\n", vin->name, vin->context.errorline);
								vin->state = PG_VINETIC_STATE_IDLE;
								break;
							}
						}
					}
				}
				break;
			case PG_VINETIC_STATE_RUN:
				ast_mutex_unlock(&vin->lock);
				if (vin_get_status(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_get_status()\n", vin->name);
					ast_mutex_lock(&vin->lock);
					vin->state = PG_VINETIC_STATE_IDLE;
				} else {
					ast_mutex_lock(&vin->lock);
					if (vin->context.status.custom.bits.timeout) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": ready timeout\n", vin->name);
						vin->state = PG_VINETIC_STATE_IDLE;
					}
				}
				break;
			default:
				ast_log(LOG_ERROR, "vinetic=\"%s\": unknown state=%d\n", vin->name, vin->state);
				vin->state = PG_VINETIC_STATE_IDLE;
				break;
		}
		ast_mutex_unlock(&vin->lock);
	}

pg_vinetic_workthread_end:
	ast_mutex_lock(&vin->lock);
	vin_poll_disable(&vin->context);
	vin_close(&vin->context);
	vin->thread = AST_PTHREADT_NULL;
	ast_mutex_unlock(&vin->lock);
	ast_debug(4, "vinetic=\"%s\": thread stop\n", vin->name);
	return NULL;
}
//------------------------------------------------------------------------------
// end of pg_vinetic_workthread()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_config_file_build()
//------------------------------------------------------------------------------
static int pg_config_file_build(char *filename)
{
	FILE *fp;
	char path[PATH_MAX];
	int len = 0;
	struct timeval curtime;

	struct pg_board *brd;
	struct pg_channel_gsm *ch_gsm;
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold;
	struct pg_vinetic *vin;

	sprintf(path, "%s/%s", ast_config_AST_CONFIG_DIR, filename);
	if (!(fp = fopen(path, "w"))) {
		ast_log(LOG_ERROR, "fopen(%s): %s\n", filename, strerror(errno));
		return -1;
	}

#undef FORMAT_SEPARATOR_LINE
#define FORMAT_SEPARATOR_LINE \
";-------------------------------------------------------------------------------\n"

	ast_mutex_lock(&pg_lock);

	// build header
	len += fprintf(fp, FORMAT_SEPARATOR_LINE);
	len += fprintf(fp, "; file: %s\n", filename);
	len += fprintf(fp, FORMAT_SEPARATOR_LINE);
	gettimeofday(&curtime, NULL);
	len += fprintf(fp, "; created at: %s", ctime(&curtime.tv_sec));
	len += fprintf(fp, "; polygator version: %s\n", VERSION);
	len += fprintf(fp, "; asterisk version: %s\n", ASTERISK_VERSION);
	len += fprintf(fp, FORMAT_SEPARATOR_LINE);

	// build general category
	len += fprintf(fp, "[general]\n");
	// at.timeout.response
	len += fprintf(fp, "at.timeout.response=%d\n", pg_at_response_timeout);

	len += fprintf(fp, FORMAT_SEPARATOR_LINE);

	// build vinetics categories
	AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
	{
		ast_mutex_lock(&brd->lock);
		AST_LIST_TRAVERSE(&brd->vinetic_list, vin, pg_board_vinetic_list_entry)
		{
			ast_mutex_lock(&vin->lock);
			// vinetic category
			len += fprintf(fp, "[%s]\n", vin->name);
			// firmware
			len += fprintf(fp, "firmware=%s\n", vin->firmware);
			// almab
			len += fprintf(fp, "almab=%s\n", vin->almab);
			// almcd
			len += fprintf(fp, "almcd=%s\n", vin->almcd);
			// cram
			len += fprintf(fp, "cram=%s\n", vin->cram);

			// place separator
			len += fprintf(fp, FORMAT_SEPARATOR_LINE);
			
			ast_mutex_unlock(&vin->lock);
		}
		ast_mutex_unlock(&brd->lock);
	}

	// build GSM channels categories
	AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
	{
		ast_mutex_lock(&brd->lock);
		AST_LIST_TRAVERSE(&brd->channel_gsm_list, ch_gsm, pg_board_channel_gsm_list_entry)
		{
			ast_mutex_lock(&ch_gsm->lock);
			// GSM channel category
			len += fprintf(fp, "[%s-gsm%u]\n", ch_gsm->board->name, ch_gsm->position_on_board);
			// alias
			len += fprintf(fp, "alias=%s\n", ch_gsm->alias);
			// enable
			len += fprintf(fp, "enable=%s\n", ch_gsm->flags.enable ? "yes" : "no");
			// pin
			if (ch_gsm->pin)
				len += fprintf(fp, "pin=%s\n", ch_gsm->pin);
			// baudrate
			len += fprintf(fp, "baudrate=%u\n", ch_gsm->config.baudrate);
			// regattempt
			if (ch_gsm->config.reg_try_count < 0)
				len += fprintf(fp, "regattempt=forever\n");
			else
				len += fprintf(fp, "regattempt=%d\n", ch_gsm->config.reg_try_count);
			// callwait
			len += fprintf(fp, "callwait=%s\n", ch_gsm->config.callwait ? "yes" : "no");
			// clir
			len += fprintf(fp, "clir=%s\n", pg_clir_state_to_string(ch_gsm->config.clir));
			// outgoing
			len += fprintf(fp, "outgoing=%s\n", pg_call_gsm_outgoing_to_string(ch_gsm->config.outgoing_type));
			// incoming
			len += fprintf(fp, "incoming=%s\n", pg_call_gsm_incoming_type_to_string(ch_gsm->config.incoming_type));
			// incomingto
			if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_SPEC)
				len += fprintf(fp, "incomingto=%s\n", ch_gsm->config.gsm_call_extension);
			// dcrttl
			else if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_DYN)
				len += fprintf(fp, "dcrttl=%ld\n", (long int)ch_gsm->config.dcrttl);
			// context
			if (strlen(ch_gsm->config.gsm_call_context))
				len += fprintf(fp, "context=%s\n", ch_gsm->config.gsm_call_context);
			// progress
			len += fprintf(fp, "progress=%s\n", pg_call_gsm_progress_to_string(ch_gsm->config.progress));
			// language
			if (strlen(ch_gsm->config.language))
				len += fprintf(fp, "language=%s\n", ch_gsm->config.language);
			// mohinterpret
			if (strlen(ch_gsm->config.mohinterpret))
				len += fprintf(fp, "mohinterpret=%s\n", ch_gsm->config.mohinterpret);
			// trunk
			AST_LIST_TRAVERSE(&ch_gsm->trunk_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_channel_list_entry)
				len += fprintf(fp, "trunk=%s\n", ch_gsm_fold->name);
			// trunkonly
			if (ch_gsm->trunk_list.first)
				len += fprintf(fp, "trunkonly=%s\n", ch_gsm->config.trunkonly?"yes":"no");
			// confallow
			len += fprintf(fp, "confallow=%s\n", ch_gsm->config.conference_allowed?"yes":"no");
			// gainin
			len += fprintf(fp, "gainin=%d\n", ch_gsm->config.gainin);
			// gainout
			len += fprintf(fp, "gainout=%d\n", ch_gsm->config.gainout);
			// gain1
			len += fprintf(fp, "gain1=%02x\n", ch_gsm->config.gain1);
			// gain2
			len += fprintf(fp, "gain2=%02x\n", ch_gsm->config.gain2);
			// gainX
			len += fprintf(fp, "gainx=%02x\n", ch_gsm->config.gainx);
			// gainR
			len += fprintf(fp, "gainr=%02x\n", ch_gsm->config.gainr);

			// sms.send.interval
			len += fprintf(fp, "sms.send.interval=%ld\n", ch_gsm->config.sms_send_interval.tv_sec);
			// sms.max.attempt
			len += fprintf(fp, "sms.send.attempt=%d\n", ch_gsm->config.sms_send_attempt);
			// sms.max.part
			len += fprintf(fp, "sms.max.part=%d\n", ch_gsm->config.sms_max_part);

			// sms.notify.enable
			len += fprintf(fp, "sms.notify.enable=%s\n", ch_gsm->config.sms_notify_enable?"yes":"no");
			// sms.notify.context
			len += fprintf(fp, "sms.notify.context=%s\n", ch_gsm->config.sms_notify_context);
			// sms.notify.extension
			len += fprintf(fp, "sms.notify.extension=%s\n", ch_gsm->config.sms_notify_extension);

			// ali.nelec
			len += fprintf(fp, "ali.nelec=%s\n", (ch_gsm->config.ali_nelec == VIN_DIS)?"inactive":"active");
			// ali.nelec.tm
			len += fprintf(fp, "ali.nelec.tm=%s\n", (ch_gsm->config.ali_nelec_tm == VIN_DTM_ON)?"on":"off");
			// ali.nelec.oldc
			len += fprintf(fp, "ali.nelec.oldc=%s\n", (ch_gsm->config.ali_nelec_oldc == VIN_OLDC_ZERO)?"zero":"old");
			// ali.nelec.as
			len += fprintf(fp, "ali.nelec.as=%s\n", (ch_gsm->config.ali_nelec_as == VIN_AS_RUN)?"run":"stop");
			// ali.nelec.nlp
			len += fprintf(fp, "ali.nelec.nlp=%s\n", (ch_gsm->config.ali_nelec_nlp == VIN_OFF)?"off":"on");
			// ali.nelec.nlpm
			len += fprintf(fp, "ali.nelec.nlpm=%s\n", pg_vinetic_ali_nelec_nlpm_to_string(ch_gsm->config.ali_nelec_nlpm));

			// place separator
			len += fprintf(fp, FORMAT_SEPARATOR_LINE);
			
			ast_mutex_unlock(&ch_gsm->lock);
		}
		ast_mutex_unlock(&brd->lock);
	}

	ast_mutex_unlock(&pg_lock);

	fflush(fp);
	fclose(fp);
	return len;
}
//------------------------------------------------------------------------------
// end of pg_config_file_build()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_config_file_copy()
//------------------------------------------------------------------------------
static int pg_config_file_copy(char *dst, char *src)
{
	FILE *src_fp;
	FILE *dst_fp;
	char src_path[PATH_MAX];
	char dst_path[PATH_MAX];

	struct timeval curtime;

	char buf[256];

	// check source/destination path collision
	if (!strcmp(src, dst)) {
		ast_log(LOG_ERROR, "source=\"%s\" equal destination=\"%s\"\n", src, dst);
		return -1;
	}
	// open source file
	sprintf(src_path, "%s/%s", ast_config_AST_CONFIG_DIR, src);
	if (!(src_fp = fopen(src_path, "r"))) {
		ast_log(LOG_ERROR, "fopen(%s): %s\n", src, strerror(errno));
		return -1;
	}
	// open destination file
	sprintf(dst_path, "%s/%s", ast_config_AST_CONFIG_DIR, dst);
	if (!(dst_fp = fopen(dst_path, "w"))) {
		ast_log(LOG_ERROR, "fopen(%s): %s\n", dst, strerror(errno));
		return -1;
	}

#undef FORMAT_SEPARATOR_LINE
#define FORMAT_SEPARATOR_LINE \
";-------------------------------------------------------------------------------\n"

	ast_mutex_lock(&pg_lock);

	// build copying header
	fprintf(dst_fp, FORMAT_SEPARATOR_LINE);
	gettimeofday(&curtime, NULL);
	fprintf(dst_fp, "; %s copyed from file: %s at %s", dst, src, ctime(&curtime.tv_sec));

	// copying source file to destination file
	while (fgets(buf, sizeof(buf), src_fp))
		fprintf(dst_fp, "%s", buf);

	ast_mutex_unlock(&pg_lock);

	fclose(src_fp);
	fclose(dst_fp);
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_config_file_copy()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_requester()
//------------------------------------------------------------------------------
#if ASTERISK_VERSION_NUM >= 100000
static struct ast_channel *pg_gsm_requester(const char *type, struct ast_format_cap *format, const struct ast_channel *requestor, void *data, int *cause)
#elif ASTERISK_VERSION_NUM >= 10800
static struct ast_channel *pg_gsm_requester(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause)
#else
static struct ast_channel *pg_gsm_requester(const char *type, int format, void *data, int *cause)
#endif
{
	char *cpd;
	char trunk[256];
	char channel[256];
#if ASTERISK_VERSION_NUM >= 10800
	char conference[256];
#endif	
	char flags[32];
	char called_name[MAX_ADDRESS_LENGTH];
#if ASTERISK_VERSION_NUM >= 100000
	struct ast_format_cap *joint = NULL;
#elif ASTERISK_VERSION_NUM >= 10800
	format_t joint = 0;
#else
	int joint;
#endif
	ssize_t res;
	struct ast_channel *ast_ch;
	struct pg_trunk_gsm *tr_gsm;
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold, *ch_gsm_fold_start;
	struct pg_channel_gsm *ch_gsm, *ch_gsm_start;
	struct pg_call_gsm *call;
	struct pg_vinetic *vin;
	struct pg_channel_rtp *rtp;
	u_int32_t ch_id;

	ch_gsm = NULL;
	call = NULL;
	vin = NULL;
	rtp = NULL;

	if (!data) {
		ast_log(LOG_WARNING, "requester data not present\n");
		*cause = AST_CAUSE_INCOMPATIBLE_DESTINATION;
		return NULL;
	}

	cpd = ast_strdupa((char *)data);
	trunk[0] = '\0';
	channel[0] = '\0';
#if ASTERISK_VERSION_NUM >= 10800
	conference[0] = '\0';
#endif
	flags[0] = '\0';
	if ((sscanf(cpd, "TR[%[0-9A-Za-z-_]]/%[0-9+]", trunk, called_name) != 2) &&
		(sscanf(cpd, "TR[%[0-9A-Za-z-_]]/%[A-Za-z]/%[0-9+]", trunk, flags, called_name) != 3) &&
		(sscanf(cpd, "TRUNK[%[0-9A-Za-z-_]]/%[0-9+]", trunk, called_name) != 2) &&
		(sscanf(cpd, "TRUNK[%[0-9A-Za-z-_]]/%[A-Za-z]/%[0-9+]", trunk, flags, called_name) != 3) &&
			(sscanf(cpd, "CH[%[0-9A-Za-z-_]]/%[0-9+]", channel, called_name) != 2) &&
			(sscanf(cpd, "CHANNEL[%[0-9A-Za-z-_]]/%[0-9+]", channel, called_name) != 2) &&
#if ASTERISK_VERSION_NUM >= 10800
				(sscanf(cpd, "CONF[%[0-9A-Za-z-_]]/%[0-9+]", conference, called_name) != 2) &&
				(sscanf(cpd, "CONFERENCE[%[0-9A-Za-z-_]]/%[0-9+]", conference, called_name) != 2) &&
#endif
					(sscanf(cpd, "%[0-9+]", called_name) != 1) &&
					(sscanf(cpd, "%[A-Za-z]/%[0-9+]", flags, called_name) != 2)) {
		ast_log(LOG_WARNING, "can't parse request data=\"%s\"\n", (char *)data);
		*cause = AST_CAUSE_INCOMPATIBLE_DESTINATION;
		return NULL;
	}

	if (strlen(trunk)) {
		ast_mutex_lock(&pg_lock);
		// get requested trunk from general trunk list
		if ((tr_gsm = pg_get_trunk_gsm_by_name(trunk))) {
			ch_gsm_fold_start = NULL;
			if (tr_gsm->channel_gsm_last)
				ch_gsm_fold_start = tr_gsm->channel_gsm_last->pg_trunk_gsm_channel_gsm_fold_trunk_list_entry.next;
			if (!ch_gsm_fold_start)
				ch_gsm_fold_start = tr_gsm->channel_gsm_list.first;
			ch_gsm_fold = ch_gsm_fold_start;
			// traverse trunk channel list
			while (ch_gsm_fold)
			{
				ch_gsm = ch_gsm_fold->channel_gsm;
				if (ch_gsm) {
					ast_mutex_lock(&ch_gsm->lock);
					if (
						(ch_gsm->state == PG_CHANNEL_GSM_STATE_RUN) &&
						((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING)) &&
						(ch_gsm->config.outgoing_type == PG_CALL_GSM_OUTGOING_TYPE_ALLOW) &&
						(!pg_is_channel_gsm_has_calls(ch_gsm)) &&
						((vin = pg_get_vinetic_from_board(ch_gsm->board, ch_gsm->position_on_board/4))) &&
						(pg_is_vinetic_run(vin)) &&
#if ASTERISK_VERSION_NUM >= 100000
						((joint = ast_format_cap_joint(format, vin->capabilities))) &&
#else
						((joint = format & vin->capabilities)) &&
#endif
						((rtp = pg_get_channel_rtp(vin)))
					) {
						// set new empty call to prevent missing ownership
						if ((call = pg_channel_gsm_get_new_call(ch_gsm))) {
							call->direction = PG_CALL_GSM_DIRECTION_OUTGOING;
							call->channel_rtp = rtp;
							tr_gsm->channel_gsm_last = ch_gsm_fold;
							ast_verb(3, "Polygator: got GSM channel=\"%s\" from trunk=\"%s\"\n", ch_gsm->alias, trunk);
							break;
						}
					}
					ast_mutex_unlock(&ch_gsm->lock);
				}
				ch_gsm_fold = ch_gsm_fold->pg_trunk_gsm_channel_gsm_fold_trunk_list_entry.next;
				if (!ch_gsm_fold)
					ch_gsm_fold = tr_gsm->channel_gsm_list.first;
				if (ch_gsm_fold == ch_gsm_fold_start) {
					ch_gsm = NULL;
					ch_gsm_fold = NULL;
					break;
				}
			}  // traverse trunk channel list
			// congestion -- free channel not found
			if (!ch_gsm) {
				*cause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
				ast_verb(3, "Polygator: free GSM channel from trunk=\"%s\" not found\n", trunk);
			}
		} else { // trunk not found
			*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
			ast_verb(3, "Polygator: requested GSM trunk=\"%s\" not found\n", trunk);
		}
		ast_mutex_unlock(&pg_lock);
	} else if (strlen(channel)) {
		// get requested channel from general channel list
		if ((ch_gsm = pg_get_channel_gsm_by_name(channel))) {
			ast_mutex_lock(&ch_gsm->lock);
			if (
				(ch_gsm->state == PG_CHANNEL_GSM_STATE_RUN) &&
				((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING)) &&
				(ch_gsm->config.outgoing_type == PG_CALL_GSM_OUTGOING_TYPE_ALLOW) &&
				(!ch_gsm->config.trunkonly) &&
				(!pg_is_channel_gsm_has_calls(ch_gsm)) &&
				((vin = pg_get_vinetic_from_board(ch_gsm->board, ch_gsm->position_on_board/4))) &&
				(pg_is_vinetic_run(vin)) &&
#if ASTERISK_VERSION_NUM >= 100000
				((joint = ast_format_cap_joint(format, vin->capabilities))) &&
#else
				((joint = format & vin->capabilities)) &&
#endif
				((rtp = pg_get_channel_rtp(vin))) &&
				((call = pg_channel_gsm_get_new_call(ch_gsm)))
			) {
				call->direction = PG_CALL_GSM_DIRECTION_OUTGOING;
				call->channel_rtp = rtp;
				ast_verb(3, "Polygator: got requested GSM channel=\"%s\"\n", ch_gsm->alias);
			} else {
				ast_mutex_unlock(&ch_gsm->lock);
				ch_gsm = NULL;
				*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
				ast_verb(3, "Polygator: requested GSM channel=\"%s\" busy\n", channel);
			}
		} else {
			*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
			ast_verb(3, "Polygator: requested GSM channel=\"%s\" not found\n", channel);
		}
#if ASTERISK_VERSION_NUM >= 10800
	} else if (strlen(conference)) {
		// get requested conference channel from general channel list
		if ((ch_gsm = pg_get_channel_gsm_by_name(conference))) {
			ast_mutex_lock(&ch_gsm->lock);
			if (
				(ch_gsm->state == PG_CHANNEL_GSM_STATE_RUN) &&
				((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING)) &&
				(ch_gsm->config.outgoing_type == PG_CALL_GSM_OUTGOING_TYPE_ALLOW) &&
				(!ch_gsm->config.trunkonly) &&
				(ch_gsm->config.conference_allowed) &&
				(pg_is_channel_gsm_has_same_requestor(ch_gsm, requestor->caller.id.number.str)) &&
				(!pg_is_channel_gsm_has_active_calls(ch_gsm))
			) {
				if (ch_gsm->channel_rtp) {
					rtp = ch_gsm->channel_rtp;
					vin = rtp->vinetic;
				} else if ((!(vin = pg_get_vinetic_from_board(ch_gsm->board, ch_gsm->position_on_board/4))) ||
						(!pg_is_vinetic_run(vin)) ||
#if ASTERISK_VERSION_NUM >= 100000
						(!(joint = ast_format_cap_joint(format, vin->capabilities))) ||
#else
						(!(joint = format & vin->capabilities)) ||
#endif
						(!(rtp = pg_get_channel_rtp(vin)))) {
					ast_mutex_unlock(&ch_gsm->lock);
					ch_gsm = NULL;
					*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
					ast_verb(3, "Polygator: requested conference GSM channel=\"%s\" can't get RTP\n", conference);
				}
				if ((call = pg_channel_gsm_get_new_call(ch_gsm))) {
					call->direction = PG_CALL_GSM_DIRECTION_OUTGOING;
					call->channel_rtp = rtp;
					ast_verb(3, "Polygator: got requested conference GSM channel=\"%s\"\n", ch_gsm->alias);
				} else {
					ast_mutex_unlock(&ch_gsm->lock);
					ch_gsm = NULL;
					*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
					ast_verb(3, "Polygator: requested conference GSM channel=\"%s\" can't create outgoing call\n", conference);
				}
			} else {
				ast_mutex_unlock(&ch_gsm->lock);
				ch_gsm = NULL;
				*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
				ast_verb(3, "Polygator: requested conference GSM channel=\"%s\" busy\n", conference);
			}
		} else {
			*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
			ast_verb(3, "Polygator: requested conference GSM channel=\"%s\" not found\n", conference);
		}
#endif
	} else {
		ast_mutex_lock(&pg_lock);
		// search free channel in general channel list
		ch_gsm_start = NULL;
		if (pg_channel_gsm_last)
			ch_gsm_start = pg_channel_gsm_last->pg_general_channel_gsm_list_entry.next;
		if (!ch_gsm_start)
			ch_gsm_start = pg_general_channel_gsm_list.first;
		ch_gsm = ch_gsm_start;
		// traverse channel list
		while (ch_gsm)
		{
			ast_mutex_lock(&ch_gsm->lock);
			if (
				(ch_gsm->state == PG_CHANNEL_GSM_STATE_RUN) &&
				((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING)) &&
				(ch_gsm->config.outgoing_type == PG_CALL_GSM_OUTGOING_TYPE_ALLOW) &&
				(!ch_gsm->config.trunkonly) &&
				(!pg_is_channel_gsm_has_calls(ch_gsm)) &&
				((vin = pg_get_vinetic_from_board(ch_gsm->board, ch_gsm->position_on_board/4))) &&
				(pg_is_vinetic_run(vin)) &&
#if ASTERISK_VERSION_NUM >= 100000
				((joint = ast_format_cap_joint(format, vin->capabilities))) &&
#else
				((joint = format & vin->capabilities)) &&
#endif
				((rtp = pg_get_channel_rtp(vin)))
			) {
				// set new empty call to prevent missing ownership
				if ((call = pg_channel_gsm_get_new_call(ch_gsm))) {
					call->direction = PG_CALL_GSM_DIRECTION_OUTGOING;
					call->channel_rtp = rtp;
					pg_channel_gsm_last = ch_gsm;
					ast_verb(3, "Polygator: got GSM channel=\"%s\"\n", ch_gsm->alias);
					break;
				}
			}
			ast_mutex_unlock(&ch_gsm->lock);

			ch_gsm = ch_gsm->pg_general_channel_gsm_list_entry.next;
			if (!ch_gsm)
				ch_gsm = pg_general_channel_gsm_list.first;
			if (ch_gsm == ch_gsm_start) {
				ch_gsm = NULL;
				break;
			}
		} // end of traverse channel list
		ast_mutex_unlock(&pg_lock);
		// congestion -- free channel not found
		if (!ch_gsm) {
			*cause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
			ast_verb(3, "Polygator: free GSM channel not found\n");
		}
	}

	if (!ch_gsm) {
#if ASTERISK_VERSION_NUM >= 100000
		ast_format_cap_destroy(joint);
#endif
		return NULL;
	}

	if (!ch_gsm->channel_rtp_usage) {

		rtp->loc_ssrc = ast_random();
		rtp->rem_ssrc = ast_random();
		rtp->loc_timestamp = ast_random();
		rtp->loc_timestamp |= 160;
		rtp->loc_seq_num = ast_random() & 0xffff;

		rtp->recv_ssrc = 0;
		rtp->recv_timestamp = 0;
		rtp->recv_seq_num = 0;

		rtp->event_is_now_recv = 0;

#if ASTERISK_VERSION_NUM >= 100000
		if (!ast_best_codec(joint, &rtp->format)) {
			*cause = AST_CAUSE_BEARERCAPABILITY_NOTIMPL;
			pg_put_channel_rtp(rtp);
			pg_channel_gsm_put_call(ch_gsm, call);
			ast_format_cap_destroy(joint);
			ast_mutex_unlock(&ch_gsm->lock);
			return NULL;
		}
#else
		if (!(rtp->format = ast_best_codec(joint))) {
			*cause = AST_CAUSE_BEARERCAPABILITY_NOTIMPL;
			pg_put_channel_rtp(rtp);
			pg_channel_gsm_put_call(ch_gsm, call);
			ast_mutex_unlock(&ch_gsm->lock);
			return NULL;
		}
#endif

#if ASTERISK_VERSION_NUM >= 100000
		switch (rtp->format.id)
#else
		switch (rtp->format)
#endif
		{
			case AST_FORMAT_G723_1:
				/*! G.723.1 compression */
				rtp->payload_type = RTP_PT_G723;
				rtp->encoder_packet_time = VIN_PTE_30;
				rtp->encoder_algorithm = VIN_ENC_G7231_5_3;
				break;
			case AST_FORMAT_GSM:
				/*! GSM compression */
				rtp->payload_type = RTP_PT_GSM;
				break;
			case AST_FORMAT_ULAW:
				/*! Raw mu-law data (G.711) */
				rtp->payload_type = RTP_PT_PCMU;
				rtp->encoder_packet_time = VIN_PTE_20;
				rtp->encoder_algorithm = VIN_ENC_G711_MLAW;
				break;
			case AST_FORMAT_ALAW:
				/*! Raw A-law data (G.711) */
				rtp->payload_type = RTP_PT_PCMA;
				rtp->encoder_packet_time = VIN_PTE_20;
				rtp->encoder_algorithm = VIN_ENC_G711_ALAW;
				break;
			case AST_FORMAT_G726_AAL2:
				/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_ADPCM:
				/*! ADPCM (IMA) */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_SLINEAR:
				/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_LPC10:
				/*! LPC10, 180 samples/frame */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_G729A:
				/*! G.729A audio */
				rtp->payload_type = RTP_PT_G729;
				rtp->encoder_packet_time = VIN_PTE_20;
				rtp->encoder_algorithm = VIN_ENC_G729AB_8;
				break;
			case AST_FORMAT_SPEEX:
				/*! SpeeX Free Compression */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_ILBC:
				/*! iLBC Free Compression */
				rtp->payload_type = RTP_PT_DYNAMIC;
				rtp->encoder_algorithm = VIN_ENC_ILBC_15_2;
				break;
			case AST_FORMAT_G726:
				/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
				rtp->payload_type = 2; // from vinetic defaults
				rtp->encoder_packet_time = VIN_PTE_20;
				rtp->encoder_algorithm = VIN_ENC_G726_32;
				break;
			case AST_FORMAT_G722:
				/*! G.722 */
				rtp->payload_type = -1;
				break;
			case AST_FORMAT_SLINEAR16:
				/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
				rtp->payload_type = -1;
				break;
			default:
				rtp->payload_type = -1;
#if ASTERISK_VERSION_NUM >= 100000
				ast_log(LOG_ERROR, "unknown asterisk frame format=%s\n", ast_getformatname(&rtp->format));
#else
				ast_log(LOG_ERROR, "unknown asterisk frame format=%s\n", ast_getformatname(rtp->format));
#endif
				break;
		}
		if (rtp->payload_type < 0) {
#if ASTERISK_VERSION_NUM >= 100000
			ast_log(LOG_WARNING, "can't assign frame format=%s with RTP payload type\n", ast_getformatname(&rtp->format));
#else
			ast_log(LOG_WARNING, "can't assign frame format=%s with RTP payload type\n", ast_getformatname(rtp->format));
#endif
			*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
			pg_put_channel_rtp(rtp);
			pg_channel_gsm_put_call(ch_gsm, call);
#if ASTERISK_VERSION_NUM >= 100000
			ast_format_cap_destroy(joint);
#endif
			ast_mutex_unlock(&ch_gsm->lock);
			return NULL;
		}
		rtp->payload_type &= 0x7f;
		rtp->event_payload_type = 107;

		// set vinetic audio path
		ast_mutex_lock(&vin->lock);
		// unblock vinetic
		if ((res = vin_reset_status(&vin->context)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_status(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_requester_vinetic_end;
		}
		// ALI module
		if (ch_gsm->vinetic_alm_slot >= 0) {
			if (!is_vin_ali_enabled(vin->context)) {
				// enable vinetic ALI module
				if ((res = vin_ali_enable(vin->context)) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_enable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_enable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_gsm_requester_vinetic_end;
				}
			}
			// enable vinetic ALI channel
			vin_ali_channel_set_input_sig_b(vin->context, ch_gsm->vinetic_alm_slot, 1, rtp->position_on_vinetic);
			vin_ali_channel_set_gainr(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.gainr);
			vin_ali_channel_set_gainx(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.gainx);
			if ((res = vin_ali_channel_enable(vin->context, ch_gsm->vinetic_alm_slot)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_channel_enable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_requester_vinetic_end;
			}
			if (ch_gsm->config.ali_nelec == VIN_EN) {
				// enable vinetic ALI Near End LEC
				vin_ali_near_end_lec_set_dtm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_tm);
				vin_ali_near_end_lec_set_oldc(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_oldc);
				vin_ali_near_end_lec_set_as(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_as);
				vin_ali_near_end_lec_set_nlp(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlp);
				vin_ali_near_end_lec_set_nlpm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlpm);
				if ((res = vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot)) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_gsm_requester_vinetic_end;
				}
			} else {
				// disable vinetic ALI Near End LEC
				if ((res = vin_ali_near_end_lec_disable(vin->context, ch_gsm->vinetic_alm_slot)) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_disable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_near_end_lec_disable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_gsm_requester_vinetic_end;
				}
			}
			// set ALI channel operation mode ACTIVE_HIGH_VBATH
			if ((res = vin_set_opmode(&vin->context, ch_gsm->vinetic_alm_slot, VIN_OP_MODE_ACTIVE_HIGH_VBATH)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_opmode(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_opmode(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_requester_vinetic_end;
			}
		}
		// signaling module
		if (!is_vin_signaling_enabled(vin->context)) {
			// enable coder module
			if ((res = vin_signaling_enable(vin->context)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_enable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_enable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_requester_vinetic_end;
			}
		}
		// set signaling channel
		vin_signaling_channel_set_input_ali(vin->context, rtp->position_on_vinetic, 1, ch_gsm->vinetic_alm_slot);
		vin_signaling_channel_set_input_coder(vin->context, rtp->position_on_vinetic, 2, rtp->position_on_vinetic);
		if ((res = vin_signaling_channel_enable(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_enable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_requester_vinetic_end;
		}
		// set DTMF receiver
		vin_dtmf_receiver_set_as(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_dtmf_receiver_set_is(vin->context, rtp->position_on_vinetic, VIN_IS_SIGINA);
		vin_dtmf_receiver_set_et(vin->context, rtp->position_on_vinetic, VIN_ACTIVE);
		if ((res = vin_dtmf_receiver_enable(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_dtmf_receiver_enable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_dtmf_receiver_enable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_requester_vinetic_end;
		}
		// set signalling channel RTP
		vin_signaling_channel_config_rtp_set_ssrc(vin->context, rtp->position_on_vinetic, rtp->rem_ssrc);
		vin_signaling_channel_config_rtp_set_evt_pt(vin->context, rtp->position_on_vinetic, rtp->event_payload_type);
		if ((res = vin_signaling_channel_config_rtp(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_config_rtp(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_requester_vinetic_end;
		}
		// coder module
		if (!is_vin_coder_enabled(vin->context)) {
			// enable coder module
			if ((res = vin_coder_enable(vin->context)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_enable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_enable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_requester_vinetic_end;
			}
			// set coder configuration RTP
			vin_coder_config_rtp_set_timestamp(vin->context, 0);
			if ((res = vin_coder_config_rtp(vin->context)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_config_rtp(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_requester_vinetic_end;
			}
		}
		// set coder channel RTP
		vin_coder_channel_config_rtp_set_ssrc(vin->context, rtp->position_on_vinetic, rtp->rem_ssrc);
		vin_coder_channel_config_rtp_set_seq_nr(vin->context, rtp->position_on_vinetic, 0);
		if ((res = vin_coder_channel_config_rtp(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_config_rtp(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_config_rtp(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_requester_vinetic_end;
		}
		// set coder channel speech compression
		vin_coder_channel_set_ns(vin->context, rtp->position_on_vinetic, VIN_NS_INACTIVE);
		vin_coder_channel_set_hp(vin->context, rtp->position_on_vinetic, VIN_INACTIVE);
		vin_coder_channel_set_pf(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_cng(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_bfi(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_dec(vin->context, rtp->position_on_vinetic, VIN_ACTIVE);
		vin_coder_channel_set_im(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_pst(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_sic(vin->context, rtp->position_on_vinetic, VIN_OFF);
		vin_coder_channel_set_pte(vin->context, rtp->position_on_vinetic, rtp->encoder_packet_time);
		vin_coder_channel_set_enc(vin->context, rtp->position_on_vinetic, rtp->encoder_algorithm);
		vin_coder_channel_set_gain1(vin->context, rtp->position_on_vinetic, ch_gsm->config.gain1);
		vin_coder_channel_set_gain2(vin->context, rtp->position_on_vinetic, ch_gsm->config.gain2);
		vin_coder_channel_set_input_sig_a(vin->context, rtp->position_on_vinetic, 1, rtp->position_on_vinetic);
		if ((res = vin_coder_channel_enable(vin->context, rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_enable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_requester_vinetic_end;
		}
pg_gsm_requester_vinetic_end:
		if (res < 0) {
			vin->state = PG_VINETIC_STATE_IDLE;
			ast_mutex_unlock(&vin->lock);
			*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
			pg_put_channel_rtp(rtp);
			pg_channel_gsm_put_call(ch_gsm, call);
#if ASTERISK_VERSION_NUM >= 100000
			ast_format_cap_destroy(joint);
#endif
			ast_mutex_unlock(&ch_gsm->lock);
			return NULL;
		} else {
			ast_mutex_unlock(&vin->lock);
		}
	}
	ch_gsm->channel_rtp_usage++;
	ch_gsm->channel_rtp = rtp;

	// prevent deadlock while asterisk channel is allocating
	ast_mutex_unlock(&ch_gsm->lock);
	// increment channel ID
	ast_mutex_lock(&pg_lock);
	ch_id = channel_id++;
	ast_mutex_unlock(&pg_lock);
	// allocation channel in pbx spool
#if ASTERISK_VERSION_NUM < 10800
	ast_ch = ast_channel_alloc(1,						/* int needqueue */
								AST_STATE_DOWN,			/* int state */
								"",						/* const char *cid_num */
								"",						/* const char *cid_name */
								"",						/* const char *acctcode */
								"",						/* const char *exten */
								"",						/* const char *context */
								0,						/* const int amaflag */
								"PGGSM/%s-%08x",		/* const char *name_fmt, ... */
								ch_gsm->alias, ch_id);
#else
	ast_ch = ast_channel_alloc(1,						/* int needqueue */
								AST_STATE_DOWN,			/* int state */
								"",						/* const char *cid_num */
								"",						/* const char *cid_name */
								"",						/* const char *acctcode */
								"",						/* const char *exten */
								"",						/* const char *context */
								"",						/* const char *linkedid */
								0,						/* int amaflag */
								"PGGSM/%s-%08x",		/* const char *name_fmt, ... */
								ch_gsm->alias, ch_id);
#endif
	ast_mutex_lock(&ch_gsm->lock);

	// fail allocation channel
	if (!ast_ch) {
		ast_log(LOG_ERROR, "ast_channel_alloc() failed\n");
		*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
		if (!ch_gsm->channel_rtp_usage) pg_put_channel_rtp(rtp);
		pg_channel_gsm_put_call(ch_gsm, call);
#if ASTERISK_VERSION_NUM >= 100000
		ast_format_cap_destroy(joint);
#endif
		ast_mutex_unlock(&ch_gsm->lock);
		return NULL;
	}
	// init asterisk channel tag's
#if ASTERISK_VERSION_NUM >= 100000
	ast_format_cap_copy(ast_ch->nativeformats, vin->capabilities);
	ast_format_copy(&ast_ch->rawreadformat, &rtp->format);
	ast_format_copy(&ast_ch->rawwriteformat, &rtp->format);
	ast_format_copy(&ast_ch->writeformat, &rtp->format);
	ast_format_copy(&ast_ch->readformat, &rtp->format);
// 	ast_verb(3, "GSM channel=\"%s\": selected codec \"%s\"\n", ch_gsm->alias, ast_getformatname(&rtp->format));
#else
	ast_ch->nativeformats = vin->capabilities;
	ast_ch->rawreadformat = rtp->format;
	ast_ch->rawwriteformat = rtp->format;
	ast_ch->writeformat = rtp->format;
	ast_ch->readformat = rtp->format;
// 	ast_verb(3, "GSM channel=\"%s\": selected codec \"%s\"\n", ch_gsm->alias, ast_getformatname(rtp->format));
#endif
	ast_string_field_set(ast_ch, language, ch_gsm->config.language);

	ast_ch->tech = &pg_gsm_tech;
	ast_ch->tech_pvt = call;
	call->owner = ast_ch;

#if ASTERISK_VERSION_NUM >= 100000
	ast_format_cap_destroy(joint);
#endif
	ast_mutex_unlock(&ch_gsm->lock);
	return ast_ch;
}
//------------------------------------------------------------------------------
// pg_gsm_requester()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_call()
//------------------------------------------------------------------------------
static int pg_gsm_call(struct ast_channel *ast_ch, char *destination, int timeout)
{
	char *cpd;
	char device[256];
	char flags[32];
	char called_name[MAX_ADDRESS_LENGTH];
	struct timeval dial_timeout;
	struct pg_call_gsm *call = (struct pg_call_gsm *)ast_ch->tech_pvt;
	struct pg_channel_gsm *ch_gsm = call->channel_gsm;

	// parse destination
	cpd = ast_strdupa(destination);
	if ((sscanf(cpd, "TR[%[0-9A-Za-z-_]]/%[0-9+]", device, called_name) != 2) &&
		(sscanf(cpd, "TR[%[0-9A-Za-z-_]]/%[A-Za-z]/%[0-9+]", device, flags, called_name) != 3) &&
		(sscanf(cpd, "TRUNK[%[0-9A-Za-z-_]]/%[0-9+]", device, called_name) != 2) &&
		(sscanf(cpd, "TRUNK[%[0-9A-Za-z-_]]/%[A-Za-z]/%[0-9+]", device, flags, called_name) != 3) &&
			(sscanf(cpd, "CH[%[0-9A-Za-z-_]]/%[0-9+]", device, called_name) != 2) &&
			(sscanf(cpd, "CHANNEL[%[0-9A-Za-z-_]]/%[0-9+]", device, called_name) != 2) &&
				(sscanf(cpd, "CONF[%[0-9A-Za-z-_]]/%[0-9+]", device, called_name) != 2) &&
				(sscanf(cpd, "CONFERENCE[%[0-9A-Za-z-_]]/%[0-9+]", device, called_name) != 2) &&
					(sscanf(cpd, "%[0-9+]", called_name) != 1) &&
					(sscanf(cpd, "%[A-Za-z]/%[0-9+]", flags, called_name) != 2)) {
		ast_log(LOG_WARNING, "ast channel=\"%s\" has invalid called name=\"%s\"\n", ast_ch->name, called_name);
		return -1;
	}

	ast_mutex_lock(&ch_gsm->lock);

	// get called name
	address_classify(called_name, &call->called_name);
	// get calling name
#if ASTERISK_VERSION_NUM < 10800
	if (ast_ch->cid.cid_num)
		address_classify(ast_ch->cid.cid_num, &call->calling_name);
#else
	if (ast_ch->connected.id.number.str)
		address_classify(ast_ch->connected.id.number.str, &call->calling_name);
#endif
	else
		address_classify("s", &call->calling_name);

	if (pg_call_gsm_sm(call, PG_CALL_GSM_MSG_SETUP_REQ, 0) < 0) {
		ast_mutex_unlock(&ch_gsm->lock);
		return -1;
	}
	pg_dcr_table_update(ch_gsm->imsi, &call->called_name, &call->calling_name, &ch_gsm->lock);
	ast_verb(2, "GSM channel=\"%s\": outgoing call \"%s%s\" -> \"%s%s\"\n",
			 					ch_gsm->alias,
								(call->calling_name.type.full == 145)?("+"):(""), call->calling_name.value,
								(call->called_name.type.full == 145)?("+"):(""), call->called_name.value);
	// set dialing timeout
	if (timeout > 0) {
		dial_timeout.tv_sec = timeout;
		dial_timeout.tv_usec = 0;
	} else {
		dial_timeout.tv_sec = 180;
		dial_timeout.tv_usec = 0;
	}
	// start dial timer
	x_timer_set(call->timers.dial, dial_timeout);
	// start proceeding timer
	x_timer_set(call->timers.proceeding, proceeding_timeout);

	ast_channel_set_fd(ast_ch, 0, ch_gsm->channel_rtp->fd);

	ast_mutex_unlock(&ch_gsm->lock);
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_call()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_hangup()
//------------------------------------------------------------------------------
static int pg_gsm_hangup(struct ast_channel *ast_ch)
{
	struct pg_vinetic *vin;
	struct pg_call_gsm *call;
	struct pg_channel_gsm *ch_gsm;
	int res = 0;

	if (!(call = (struct pg_call_gsm *)ast_ch->tech_pvt))
		return res;

	ch_gsm = call->channel_gsm;
	
	ast_mutex_lock(&ch_gsm->lock);

	ast_verb(4, "GSM channel=\"%s\": call line=%d hangup\n", ch_gsm->alias, call->line);

	ast_setstate(ast_ch, AST_STATE_DOWN);
	res = pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_REQ, 0);
	ast_ch->tech_pvt = NULL;

	ch_gsm->channel_rtp_usage--;
	if (!ch_gsm->channel_rtp_usage) {
		vin = ch_gsm->channel_rtp->vinetic;
		ast_mutex_lock(&vin->lock);
		// unblock vinetic
		if ((res = vin_reset_status(&vin->context)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_status(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_hangup_vinetic_end;
		}
		// ALI module
		if (ch_gsm->vinetic_alm_slot >= 0) {
			// set ALI channel operation mode POWER_DOWN_HIGH_IMPEDANCE
			if ((res = vin_set_opmode(&vin->context, ch_gsm->vinetic_alm_slot, VIN_OP_MODE_POWER_DOWN_HIGH_IMPEDANCE)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_opmode(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_opmode(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_hangup_vinetic_end;
			}
			// disable vinetic ALI channel
			if ((res = vin_ali_channel_disable(vin->context, ch_gsm->vinetic_alm_slot)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_channel_disable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_channel_disable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_hangup_vinetic_end;
			}
			if (!is_vin_ali_used(vin->context)) {
				// disable vinetic ALI module
				if ((res = vin_ali_disable(vin->context)) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_disable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_ali_disable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_gsm_hangup_vinetic_end;
				}
			}
		}
		// signaling module
		// disable DTMF receiver
		if ((res = vin_dtmf_receiver_disable(vin->context, ch_gsm->channel_rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_dtmf_receiver_disable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_dtmf_receiver_disable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_hangup_vinetic_end;
		}
		// disable signaling channel
		if ((res = vin_signaling_channel_disable(vin->context, ch_gsm->channel_rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_disable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_channel_disable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_hangup_vinetic_end;
		}
		if (!is_vin_signaling_used(vin->context)) {
			// disable signaling module
			if ((res = vin_signaling_disable(vin->context)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_disable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_signaling_disable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_hangup_vinetic_end;
			}
		}
		// coder module
		// disable coder channel
		if ((res = vin_coder_channel_disable(vin->context, ch_gsm->channel_rtp->position_on_vinetic)) < 0) {
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_disable(): %s\n", vin->name, vin_error_str(&vin->context));
			ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_disable(): line %d\n", vin->name, vin->context.errorline);
			goto pg_gsm_hangup_vinetic_end;
		}
		if (!is_vin_coder_used(vin->context)) {
			// disable coder module
			if ((res = vin_coder_disable(vin->context)) < 0) {
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_disable(): %s\n", vin->name, vin_error_str(&vin->context));
				ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_disable(): line %d\n", vin->name, vin->context.errorline);
				goto pg_gsm_hangup_vinetic_end;
			}
		}
pg_gsm_hangup_vinetic_end:
		ast_mutex_unlock(&vin->lock);
		pg_put_channel_rtp(ch_gsm->channel_rtp);
		ch_gsm->channel_rtp = NULL;
	}

	ast_mutex_unlock(&ch_gsm->lock);

	return res;
}
//------------------------------------------------------------------------------
// end of pg_gsm_hangup()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_answer()
//------------------------------------------------------------------------------
static int pg_gsm_answer(struct ast_channel *ast_ch)
{
	int res;
	struct pg_call_gsm *call = (struct pg_call_gsm *)ast_ch->tech_pvt;
	struct pg_channel_gsm *ch_gsm = call->channel_gsm;

	ast_verb(4, "GSM channel=\"%s\": call line=%d answer\n", ch_gsm->alias, call->line);

	ast_mutex_lock(&ch_gsm->lock);
	res = pg_call_gsm_sm(call, PG_CALL_GSM_MSG_SETUP_RESPONSE, 0);
	ast_mutex_unlock(&ch_gsm->lock);

	ast_channel_set_fd(ast_ch, 0, ch_gsm->channel_rtp->fd);

	return res;
}
//------------------------------------------------------------------------------
// end of pg_gsm_answer()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_indicate()
//------------------------------------------------------------------------------
static int pg_gsm_indicate(struct ast_channel *ast_ch, int condition, const void *data, size_t datalen)
{
#if ASTERISK_VERSION_NUM >= 100000
	struct ast_format_cap *joint = NULL;
#elif ASTERISK_VERSION_NUM >= 10800
	format_t joint;
#else
	int joint;
#endif
	struct pg_channel_rtp *rtp;
	struct pg_vinetic *vin;
	struct ast_channel *bridge;
	int res = -1;
	struct pg_call_gsm *call = (struct pg_call_gsm *)ast_ch->tech_pvt;
	struct pg_channel_gsm *ch_gsm = call->channel_gsm;

	ast_mutex_lock(&ch_gsm->lock);

	switch (condition)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_RINGING:
			ast_debug(4, "GSM channel=\"%s\": call line=%d indicate ringing\n", ch_gsm->alias, call->line);
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_BUSY:
			if (ast_ch->_state != AST_STATE_UP) {
				ast_verb(4, "GSM channel=\"%s\": call line=%d indicate busy\n", ch_gsm->alias, call->line);
				ast_softhangup_nolock(ast_ch, AST_SOFTHANGUP_DEV);
				res = 0;
			} else
				ast_verb(4, "GSM channel=\"%s\": call line=%d indicate busy state UP\n", ch_gsm->alias, call->line);
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_CONGESTION:
			if (ast_ch->_state != AST_STATE_UP) {
				ast_verb(4, "GSM channel=\"%s\": call line=%d indicate congestion\n", ch_gsm->alias, call->line);
				ast_softhangup_nolock(ast_ch, AST_SOFTHANGUP_DEV);
				res = 0;
			} else
				ast_verb(4, "GSM channel=\"%s\": call line=%d indicate congestion state UP\n", ch_gsm->alias, call->line);
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_PROCEEDING:
			ast_verb(4, "GSM channel=\"%s\": call line=%d indicate proceeding\n", ch_gsm->alias, call->line);
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_PROGRESS:
			ast_debug(4, "GSM channel=\"%s\": call line=%d indicate progress\n", ch_gsm->alias, call->line);
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_HOLD:
			ast_verb(4, "GSM channel=\"%s\": call line=%d indicate hold\n", ch_gsm->alias, call->line);
			if ((ch_gsm->config.callwait == PG_CALLWAIT_STATE_ENABLE) || (ch_gsm->config.conference_allowed)) {
				rtp = ch_gsm->channel_rtp;
				res = pg_call_gsm_sm(call, PG_CALL_GSM_MSG_HOLD_REQ, 0);
				ast_channel_set_fd(ast_ch, 0, -1);
			} else
				ast_moh_start(ast_ch, data, ch_gsm->config.mohinterpret);
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_UNHOLD:
			ast_verb(4, "GSM channel=\"%s\": call line=%d indicate unhold\n", ch_gsm->alias, call->line);
			if ((ch_gsm->config.callwait == PG_CALLWAIT_STATE_ENABLE) || (ch_gsm->config.conference_allowed)) {
				rtp = ch_gsm->channel_rtp;
				res = pg_call_gsm_sm(call, PG_CALL_GSM_MSG_UNHOLD_REQ, 0);
				ast_channel_set_fd(ast_ch, 0, rtp->fd);
			} else {
				ast_moh_stop(ast_ch);
				res = 0;
			}
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_SRCUPDATE:
			ast_debug(4, "GSM channel=\"%s\": call line=%d src update\n", ch_gsm->alias, call->line);
			if (((bridge = ast_bridged_channel(call->owner))) &&
#if ASTERISK_VERSION_NUM >= 100000
					((joint = ast_format_cap_joint(bridge->nativeformats, ast_ch->nativeformats)))) {
#else
					((joint = bridge->nativeformats & ast_ch->nativeformats))) {
#endif
				rtp = ch_gsm->channel_rtp;
				vin = rtp->vinetic;
#if ASTERISK_VERSION_NUM >= 100000
				if (!ast_best_codec(joint, &rtp->format)) {
					ast_format_cap_destroy(joint);
					break;
				}
#else
				if (!(rtp->format = ast_best_codec(joint))) {
					break;
				}
#endif

#if ASTERISK_VERSION_NUM >= 100000
				switch (rtp->format.id)
#else
				switch (rtp->format)
#endif
				{
					case AST_FORMAT_G723_1:
						/*! G.723.1 compression */
						rtp->payload_type = RTP_PT_G723;
						rtp->encoder_packet_time = VIN_PTE_30;
						rtp->encoder_algorithm = VIN_ENC_G7231_5_3;
						break;
					case AST_FORMAT_GSM:
						/*! GSM compression */
						rtp->payload_type = RTP_PT_GSM;
						break;
					case AST_FORMAT_ULAW:
						/*! Raw mu-law data (G.711) */
						rtp->payload_type = RTP_PT_PCMU;
						rtp->encoder_packet_time = VIN_PTE_20;
						rtp->encoder_algorithm = VIN_ENC_G711_MLAW;
						break;
					case AST_FORMAT_ALAW:
						/*! Raw A-law data (G.711) */
						rtp->payload_type = RTP_PT_PCMA;
						rtp->encoder_packet_time = VIN_PTE_20;
						rtp->encoder_algorithm = VIN_ENC_G711_ALAW;
						break;
					case AST_FORMAT_G726_AAL2:
						/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
						rtp->payload_type = -1;
						break;
					case AST_FORMAT_ADPCM:
						/*! ADPCM (IMA) */
						rtp->payload_type = -1;
						break;
					case AST_FORMAT_SLINEAR:
						/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
						rtp->payload_type = -1;
						break;
					case AST_FORMAT_LPC10:
						/*! LPC10, 180 samples/frame */
						rtp->payload_type = -1;
						break;
					case AST_FORMAT_G729A:
						/*! G.729A audio */
						rtp->payload_type = RTP_PT_G729;
						rtp->encoder_packet_time = VIN_PTE_20;
						rtp->encoder_algorithm = VIN_ENC_G729AB_8;
						break;
					case AST_FORMAT_SPEEX:
						/*! SpeeX Free Compression */
						rtp->payload_type = -1;
						break;
					case AST_FORMAT_ILBC:
						/*! iLBC Free Compression */
						rtp->payload_type = RTP_PT_DYNAMIC;
						rtp->encoder_algorithm = VIN_ENC_ILBC_15_2;
						break;
					case AST_FORMAT_G726:
						/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
						rtp->payload_type = 2; // from vinetic defaults
						rtp->encoder_packet_time = VIN_PTE_20;
						rtp->encoder_algorithm = VIN_ENC_G726_32;
						break;
					case AST_FORMAT_G722:
						/*! G.722 */
						rtp->payload_type = -1;
						break;
					case AST_FORMAT_SLINEAR16:
						/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
						rtp->payload_type = -1;
						break;
					default:
						rtp->payload_type = -1;
#if ASTERISK_VERSION_NUM >= 100000
						ast_log(LOG_ERROR, "unknown asterisk frame format=%s\n", ast_getformatname(&rtp->format));
#else
						ast_log(LOG_ERROR, "unknown asterisk frame format=%s\n", ast_getformatname(rtp->format));
#endif
						break;
				}
				if (rtp->payload_type < 0) {
#if ASTERISK_VERSION_NUM >= 100000
					ast_log(LOG_WARNING, "can't assign frame format=%s with RTP payload type\n", ast_getformatname(&rtp->format));
#else
					ast_log(LOG_WARNING, "can't assign frame format=%s with RTP payload type\n", ast_getformatname(rtp->format));
#endif
#if ASTERISK_VERSION_NUM >= 100000
					ast_format_cap_destroy(joint);
#endif
					break;;
				}
				rtp->payload_type &= 0x7f;
				rtp->event_payload_type = 107;

				ast_mutex_lock(&vin->lock);
				// unblock vinetic
				if (vin_reset_status(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_status(): line %d\n", vin->name, vin->context.errorline);
					goto pg_gsm_indicate_srcupdate_end;
				}
				// diasble coder channel speech compression
				if (vin_coder_channel_disable(vin->context, rtp->position_on_vinetic) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_disable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_disable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_gsm_indicate_srcupdate_end;
				}
				// enable coder channel speech compression
				vin_coder_channel_set_pte(vin->context, rtp->position_on_vinetic, rtp->encoder_packet_time);
				vin_coder_channel_set_enc(vin->context, rtp->position_on_vinetic, rtp->encoder_algorithm);
				if (vin_coder_channel_enable(vin->context, rtp->position_on_vinetic) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_coder_channel_enable(): line %d\n", vin->name, vin->context.errorline);
					goto pg_gsm_indicate_srcupdate_end;
				}
pg_gsm_indicate_srcupdate_end:
				ast_mutex_unlock(&vin->lock);
#if ASTERISK_VERSION_NUM >= 100000
				ast_format_cap_copy(ast_ch->nativeformats, vin->capabilities);
				ast_format_copy(&ast_ch->rawreadformat, &rtp->format);
				ast_format_copy(&ast_ch->rawwriteformat, &rtp->format);
				ast_format_copy(&ast_ch->writeformat, &rtp->format);
				ast_format_copy(&ast_ch->readformat, &rtp->format);
// 				ast_verb(3, "GSM channel=\"%s\": change codec to \"%s\"\n", ch_gsm->alias, ast_getformatname(&rtp->format));
#else
				ast_ch->nativeformats = vin->capabilities;
				ast_ch->rawreadformat = rtp->format;
				ast_ch->rawwriteformat = rtp->format;
				ast_ch->writeformat = rtp->format;
				ast_ch->readformat = rtp->format;
// 				ast_verb(3, "GSM channel=\"%s\": change codec to \"%s\"\n", ch_gsm->alias, ast_getformatname(rtp->format));
#endif
#if ASTERISK_VERSION_NUM >= 100000
				ast_format_cap_destroy(joint);
#endif
			}
			res = 0;
			break;
#if 0
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case AST_CONTROL_SRCCHANGE:
			ast_debug(4, "GSM channel=\"%s\": call line=%d src change\n", ch_gsm->alias, call->line);
			res = 0;
			break;
#endif
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case -1:
			res = 0;
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_verb(4, "GSM channel=\"%s\": call line=%d unknown=%d\n", ch_gsm->alias, call->line, condition);
			res = -1;
			break;
	}
	ast_mutex_unlock(&ch_gsm->lock);
	return res;
}
//------------------------------------------------------------------------------
// end of pg_gsm_indicate()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_write()
//------------------------------------------------------------------------------
static int pg_gsm_write(struct ast_channel *ast_ch, struct ast_frame *frame)
{
	struct rtp_hdr *rtp_hdr_ptr;
	char *data_ptr;
	int rc;
	int send_len;
	struct pg_call_gsm *call = (struct pg_call_gsm *)ast_ch->tech_pvt;
	struct pg_channel_rtp *rtp = call->channel_rtp;

	ast_mutex_lock(&rtp->lock);

	if (!call->owner) {
		ast_log(LOG_DEBUG, "pvt channel has't owner\n");
		ast_mutex_unlock(&rtp->lock);
		return 0;
	}
	// check for frame present
	if (!frame) {
		ast_log(LOG_ERROR, "RTP Channel=\"%s\": frame expected\n", rtp->name);
		ast_mutex_unlock(&rtp->lock);
		return 0;
	}
	// check for frame type
	if (frame->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_ERROR, "RTP Channel=\"%s\": unsupported frame type = [%d]\n", rtp->name, frame->frametype);
		ast_mutex_unlock(&rtp->lock);
		return 0;
	}
	// get buffer for rtp header in AST_FRIENDLY_OFFSET
	if (frame->offset < sizeof(struct rtp_hdr)) {
		ast_log(LOG_DEBUG, "RTP Channel=\"%s\": not free space=%d in frame data for RTP header\n", rtp->name, frame->offset);
		ast_mutex_unlock(&rtp->lock);
		return 0;
	}
	// check for valid frame datalen - SID packet was droped
	if (frame->datalen <= 4) {
		rtp->send_sid_count++;
		ast_mutex_unlock(&rtp->lock);
		return 0;
	}
#if ASTERISK_VERSION_NUM == 10600
	if (!frame->data) {
#else
	if (!frame->data.ptr) {
#endif
		ast_log(LOG_ERROR, "RTP Channel=\"%s\": frame without data\n", rtp->name);
		ast_mutex_unlock(&rtp->lock);
		return 0;
	}
#if ASTERISK_VERSION_NUM == 10600
	data_ptr = (char *)frame->data/* + frame->offset*/;
#else
	data_ptr = (char *)frame->data.ptr/* + frame->offset*/;
#endif
// 	memset(data_ptr, 0, frame->datalen);
	data_ptr -= sizeof(struct rtp_hdr);
	rtp_hdr_ptr = (struct rtp_hdr *)data_ptr ;

	// fill RTP header fields
	rtp_hdr_ptr->version = RTP_VERSION;
	rtp_hdr_ptr->padding = 0;
	rtp_hdr_ptr->extension = 0;
	rtp_hdr_ptr->csrc_count = 0;
	rtp_hdr_ptr->marker = 0;
	rtp_hdr_ptr->payload_type = rtp->payload_type;

	rtp->loc_seq_num++;
	rtp_hdr_ptr->sequence_number = htons(rtp->loc_seq_num);
	rtp->loc_timestamp += frame->samples;
	rtp_hdr_ptr->timestamp = htonl(rtp->loc_timestamp);

	rtp_hdr_ptr->ssrc = htonl(rtp->loc_ssrc);

	send_len = frame->datalen + sizeof(struct rtp_hdr);

// 	ast_log(LOG_DEBUG, "<%s>: write: %d bytes\n", chnl->name, send_len);
	if ((rc = write(rtp->fd, data_ptr, send_len)) < 0) {
		rtp->send_drop_count++;
		if (errno != EAGAIN)
			ast_log(LOG_ERROR, "RTP Channel=\"%s\": can't write frame: %s\n", rtp->name, strerror(errno));
		ast_mutex_unlock(&rtp->lock);
		return 0;
	}
	if (rc != send_len)
		ast_log(LOG_ERROR, "RTP Channel=\"%s\": can't write frame: rc=%d send_len=%d\n", rtp->name, rc, send_len);

	rtp->send_frame_count++;

	ast_mutex_unlock(&rtp->lock);
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_write()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_read()
//------------------------------------------------------------------------------
static struct ast_frame * pg_gsm_read(struct ast_channel *ast_ch)
{
	int read_len;
	char *read_ptr;
	char *data_ptr;
	int data_len;
	int samples;

	struct rtp_hdr *rtp_hdr_ptr;
	int hdr_len;
	int pad_len;

	struct rfc2833_event_payload *event_ptr;
	char dtmf_sym;

	unsigned short seq_num;
	unsigned int timestamp;
	unsigned int ssrc;

	struct pg_call_gsm *call = (struct pg_call_gsm *)ast_ch->tech_pvt;
	struct pg_channel_rtp *rtp = call->channel_rtp;

	ast_mutex_lock(&rtp->lock);

	if (!call->owner) {
		ast_log(LOG_DEBUG, "call has't owner\n");
		ast_mutex_unlock(&rtp->lock);
		return &ast_null_frame;
	}
	// read data from voice channel driver
	read_ptr = rtp->voice_recv_buf + AST_FRIENDLY_OFFSET;
	if ((read_len = read(rtp->fd, read_ptr, PG_VOICE_BUF_LEN)) < 0) {
		if (errno != EAGAIN) {
			ast_log(LOG_ERROR, "read error=%d:(%s)\n", errno, strerror(errno));
		}
		ast_mutex_unlock(&rtp->lock);
		return &ast_null_frame;
	}
	if (read_len == 0) {
		ast_mutex_unlock(&rtp->lock);
		return &ast_null_frame;
	}
	// parsing rtp header
	rtp_hdr_ptr = (struct rtp_hdr *)read_ptr;
	hdr_len = sizeof(struct rtp_hdr);
	pad_len = 0;
	// check rtp version
	if (rtp_hdr_ptr->version != RTP_VERSION) {
		ast_log(LOG_ERROR, "wrong RTP version=%d\n", rtp_hdr_ptr->version);
		ast_mutex_unlock(&rtp->lock);
		return &ast_null_frame;
	}
	// test for padding ? get last octet == padding length
	if (rtp_hdr_ptr->padding)
		pad_len = (*(read_ptr + read_len - 1)) & (0xff);
	// check rtp header extension
	if (rtp_hdr_ptr->extension) {
		ast_log(LOG_WARNING, "RTP header extension not processed !!! -- code must be fixed _ :( _\n");
		ast_mutex_unlock(&rtp->lock);
		return &ast_null_frame;
	}
	// check CSRC count ? add heder length in according CSRC count 32bit word
	if (rtp_hdr_ptr->csrc_count)
		hdr_len += rtp_hdr_ptr->csrc_count * 4;
	// set data pointer to start voice frame position
	data_ptr = read_ptr + hdr_len;
	// calc data length
	data_len = read_len - hdr_len - pad_len;
	// check for event payload type
	if (rtp_hdr_ptr->payload_type == rtp->event_payload_type) {
		// check sequence number for monotonic increasing
		seq_num = ntohs(rtp_hdr_ptr->sequence_number);
		if ((seq_num) && (seq_num <= rtp->recv_seq_num)) {
			ast_verb(5, "RTP Channel=\"%s\": sequence number=%u is less then or equal previous received=%u\n", rtp->name, seq_num, rtp->recv_seq_num);
			ast_mutex_unlock(&rtp->lock);
			return &ast_null_frame;
		}
		// store current sequence number
		rtp->recv_seq_num = seq_num;
		// get RTP event payload
		event_ptr = (struct rfc2833_event_payload *)data_ptr;
		// get DTMF symbol from event
		dtmf_sym = pg_event_to_char(event_ptr->event);
		// check for start event
		if ((!rtp->event_is_now_recv) && (rtp_hdr_ptr->marker) && (!event_ptr->end)) {
			// now event start
			rtp->event_is_now_recv = 1;
			// chek DTMF symbol
			if (dtmf_sym < 0) {
				// unsupported symbol
				ast_log(LOG_ERROR, "RTP Channel=\"%s\": unsupported event code = [%u]\n", rtp->name, event_ptr->event);
				rtp->event_is_now_recv = 0;
				ast_mutex_unlock(&rtp->lock);
				return &ast_null_frame;
			}
			ast_verb(4, "RTP Channel=\"%s\": receiving DTMF [%c] begin\n", rtp->name, dtmf_sym);
				// store DTMF symbol into temporary buffer
			if ((rtp->dtmfptr - rtp->dtmfbuf) < PG_MAX_DTMF_LEN) {
				*rtp->dtmfptr++ = dtmf_sym;
				*rtp->dtmfptr = '\0';
			}
			// send DTMF FRAME BEGIN
			memset(&rtp->frame, 0, sizeof(struct ast_frame));
			if (dtmf_sym == 'X') {
				rtp->frame.frametype = AST_FRAME_CONTROL;
#if ASTERISK_VERSION_NUM < 10800
				rtp->frame.subclass = AST_CONTROL_FLASH;
#else
				rtp->frame.subclass.integer = AST_CONTROL_FLASH;
#endif
			} else {
				rtp->frame.frametype = AST_FRAME_DTMF;
#if ASTERISK_VERSION_NUM < 10800
				rtp->frame.subclass = dtmf_sym;
#else
				rtp->frame.subclass.integer = dtmf_sym;
#endif
			}
			rtp->frame.datalen = 0;
			rtp->frame.samples = 0;
			rtp->frame.mallocd = 0;
			rtp->frame.src = "Polygator";
			rtp->frame.len = ast_tvdiff_ms(ast_samp2tv(200, 1000), ast_tv(0, 0));
			ast_mutex_unlock(&rtp->lock);
			return &rtp->frame;
		}
		// check for stop event
		if ((rtp->event_is_now_recv) && (event_ptr->end)) {
			// now event end
			rtp->event_is_now_recv = 0;
			ast_verb(4, "RTP Channel=\"%s\": receiving DTMF [%c] end\n", rtp->name, dtmf_sym);
			ast_mutex_unlock(&rtp->lock);
			return &ast_null_frame;
		}
		ast_mutex_unlock(&rtp->lock);
		return &ast_null_frame;
	} else if (rtp_hdr_ptr->payload_type != rtp->payload_type) {
// 		ast_verb(4, "RTP Channel=\"%s\": unknown pt=%u\n", rtp->name, rtp_hdr_ptr->payload_type);
		ast_mutex_unlock(&rtp->lock);
		return &ast_null_frame;
	}
	// check sequence number for monotonic increasing
	seq_num = ntohs(rtp_hdr_ptr->sequence_number);
	// if rtp session just started
	if (!rtp->recv_seq_num && !rtp->recv_frame_count) {
		// init start sequence number
		ast_verb(4, "RTP Channel=\"%s\": staring sequence number=%u\n", rtp->name, seq_num);
		rtp->recv_seq_num = seq_num - 1;
		
	}
	if ((seq_num) && (seq_num <= rtp->recv_seq_num)) {
		ast_verb(5, "RTP Channel=\"%s\": sequence number=%u is less then or equal previous received=%u\n", rtp->name, seq_num, rtp->recv_seq_num);
		ast_mutex_unlock(&rtp->lock);
		return &ast_null_frame;
	}
	// check timestamp value
	timestamp = ntohl(rtp_hdr_ptr->timestamp);
	// if rtp session just started
	if (!rtp->recv_timestamp && !rtp->recv_frame_count) {
		// init start timestamp
		ast_verb(4, "RTP Channel=\"%s\": starting timestamp=%u\n", rtp->name, timestamp);
		rtp->recv_timestamp = timestamp - 160;
	}
	// check ssrc value
	ssrc = ntohl(rtp_hdr_ptr->ssrc);
	// if rtp session just started
	if (!rtp->recv_ssrc && !rtp->recv_frame_count) {
		// store SSRC
		ast_verb(4, "RTP Channel=\"%s\": SSRC=0x%08x\n", rtp->name, ssrc);
		rtp->recv_ssrc = ssrc;
		
	}
	if (rtp->recv_ssrc != ssrc)
		ast_verb(4, "RTP Channel=\"%s\": SSRC 0x%08x changed to 0x%08x \n", rtp->name, rtp->recv_ssrc, ssrc);
	// store SSRC
	rtp->recv_ssrc = ssrc;
	// store sequence number and timestamp
	rtp->recv_seq_num = seq_num;
	rtp->recv_timestamp  = timestamp;
	// if is first packet -- drop because don't calc true samples count
	samples = timestamp - rtp->recv_timestamp;
	if ((samples < 64) || (samples > 240)) {
		samples = 160;
	}
// 	ast_log(LOG_DEBUG, "<%s>: read %d bytes - %d samples\n", chnl->name, data_len, samples);
// 	memset(&chnl->frame, 0x00, sizeof(struct ast_frame));
	rtp->frame.frametype = AST_FRAME_VOICE;
#if ASTERISK_VERSION_NUM >= 100000
	ast_format_copy(&rtp->frame.subclass.format, &rtp->format);
#elif ASTERISK_VERSION_NUM >= 10800
	rtp->frame.subclass.codec = rtp->format;
#else
	rtp->frame.subclass = rtp->format;
#endif
	rtp->frame.datalen = data_len;
	rtp->frame.samples = samples;
	rtp->frame.src = "Polygator";
	rtp->frame.offset = AST_FRIENDLY_OFFSET + hdr_len;
	rtp->frame.mallocd = 0;
	rtp->frame.delivery.tv_sec = 0;
	rtp->frame.delivery.tv_usec = 0;
#if ASTERISK_VERSION_NUM == 10600
	rtp->frame.data = data_ptr;
#else
	rtp->frame.data.ptr = data_ptr;
#endif

	// increment statistic counter
	rtp->recv_frame_count++;

	ast_mutex_unlock(&rtp->lock);
	return &rtp->frame;
}
//------------------------------------------------------------------------------
// end of pg_gsm_read()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_fixup()
//------------------------------------------------------------------------------
static int pg_gsm_fixup(struct ast_channel *old_ast_ch, struct ast_channel *new_ast_ch)
{
	struct pg_call_gsm *call;
	struct pg_channel_gsm *ch_gsm;

	if (!old_ast_ch) {
		ast_log(LOG_ERROR, "bad fixup request - not old channel\n");
		return 0;
	}
	if (!new_ast_ch) {
		ast_log(LOG_ERROR, "bad fixup request - not new channel\n");
		return 0;
	}

	call = (struct pg_call_gsm *)old_ast_ch->tech_pvt;
	ch_gsm = call->channel_gsm;

	ast_mutex_lock(&ch_gsm->lock);

	if (call) {
		ast_verb(4, "GSM channel=\"%s\": call line=%d fixup \"%s\" -> \"%s\"\n", ch_gsm->alias, call->line, old_ast_ch->name, new_ast_ch->name);
		call->owner = new_ast_ch;
		new_ast_ch->tech_pvt = call;
	}

	ast_mutex_unlock(&ch_gsm->lock);

	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_fixup()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_dtmf_start()
//------------------------------------------------------------------------------
static int pg_gsm_dtmf_start(struct ast_channel *ast_ch, char digit)
{
	struct pg_call_gsm *call = (struct pg_call_gsm *)ast_ch->tech_pvt;
	struct pg_channel_gsm *ch_gsm = call->channel_gsm;

	ast_mutex_lock(&ch_gsm->lock);

	if (ch_gsm->dtmf_is_started) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": call line=%d DTMF just started\n", ch_gsm->alias, call->line);
		ast_mutex_unlock(&ch_gsm->lock);
		return -1;
	}
	// check for valid dtmf symbol
	switch (digit)
	{
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
		case '8': case '9': case '*': case '#':
		case 'A': case 'B': case 'C': case 'D':
			break;
		default:
			ast_log(LOG_NOTICE, "GSM channel=\"%s\": call line=%d unsupported dtmf symbol=%d\n", ch_gsm->alias, call->line, digit);
			ast_mutex_unlock(&ch_gsm->lock);
			return -1;
			break;
		}

	ast_verb(4, "GSM channel=\"%s\": call line=%d send DTMF [%c] started\n", ch_gsm->alias, call->line, digit);

	if (pg_atcommand_queue_append(ch_gsm, AT_VTS, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%c\"", digit) < 0) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": call line=%d DTMF [%c] send failed\n", ch_gsm->alias, call->line, digit);
		ast_mutex_unlock(&ch_gsm->lock);
		return -1;
	}
	ch_gsm->dtmf_is_started = 1;
	ast_mutex_unlock(&ch_gsm->lock);
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_dtmf_start()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_dtmf_end()
//------------------------------------------------------------------------------
static int pg_gsm_dtmf_end(struct ast_channel *ast_ch, char digit, unsigned int duration)
{
	struct pg_call_gsm *call = (struct pg_call_gsm *)ast_ch->tech_pvt;
	struct pg_channel_gsm *ch_gsm = call->channel_gsm;

	ast_mutex_lock(&ch_gsm->lock);

	if (!ch_gsm->dtmf_is_started) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": call line=%d DTMF sending is not started\n", ch_gsm->alias, call->line);
		ch_gsm->dtmf_is_started = 0;
		ast_mutex_unlock(&ch_gsm->lock);
		return -1;
	}
	ast_verb(4, "GSM channel=\"%s\": call line=%d sending DTMF [%c] end\n", ch_gsm->alias, call->line, digit);
	ch_gsm->dtmf_is_started = 0;
	ast_mutex_unlock(&ch_gsm->lock);
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_dtmf_end()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_board_type()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_board_type(const char *begin, int count)
{  
	struct pg_board *brd;
	char *res;
	int beginlen;
	int which;

	res = NULL;
	brd = NULL;
	which = 0;
	beginlen = strlen(begin);

	AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
	{
		ast_mutex_lock(&brd->lock);
		// compare begin of board type
		if ((!strncmp(begin, brd->type, beginlen)) && (++which > count))
		{
			res = ast_strdup(brd->type);
			ast_mutex_unlock(&brd->lock);
			break;
		}
		ast_mutex_unlock(&brd->lock);
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_board_type()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_board_name()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_board_name(const char *begin, int count)
{  
	struct pg_board *brd;
	char *res;
	int beginlen;
	int which;

	res = NULL;
	brd = NULL;
	which = 0;
	beginlen = strlen(begin);

	AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
	{
		ast_mutex_lock(&brd->lock);
		// compare begin of board name
		if ((!strncmp(begin, brd->name, beginlen)) && (++which > count))
		{
			res = ast_strdup(brd->name);
			ast_mutex_unlock(&brd->lock);
			break;
		}
		ast_mutex_unlock(&brd->lock);
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_board_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_vinetic_name()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_vinetic_name(const char *begin, int count)
{  
	struct pg_board *brd;
	struct pg_vinetic *vin;
	char *res;
	int beginlen;
	int which;

	res = NULL;
	brd = NULL;
	which = 0;
	beginlen = strlen(begin);

	AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
	{
		ast_mutex_lock(&brd->lock);
		AST_LIST_TRAVERSE(&brd->vinetic_list, vin, pg_board_vinetic_list_entry)
		{
			// compare begin of vinetic name
			if ((!strncmp(begin, vin->name, beginlen)) && (++which > count))
			{
				res = ast_strdup(vin->name);
				break;
			}
		}
		ast_mutex_unlock(&brd->lock);
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_vinetic_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_config_filename()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_config_filename(const char *begin, int count)
{  
	char *res = NULL;
	int which = 0;
	int beginlen;

	struct dirent *dirent;
	DIR *dir;

	beginlen = strlen(begin);
	
	if ((dir = opendir(ast_config_AST_CONFIG_DIR))) {
		while ((dirent = readdir(dir))) {
			if (strncasecmp(dirent->d_name, "polygator.", 6))
				continue;
			if ((!strncasecmp(dirent->d_name, begin, beginlen)) && (++which > count)) {
				res = ast_strdup(dirent->d_name);
				break;
			}
		}
		closedir(dir);
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_config_filename()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_name()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_name(const char *begin, int count, int all)
{  
	struct pg_channel_gsm *ch_gsm;
	char *res;
	int beginlen;
	int which;

	res = NULL;
	ch_gsm = NULL;
	which = 0;
	beginlen = strlen(begin);

	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		// compare begin of GSM channel alias
		if ((!strncmp(begin, ch_gsm->alias, beginlen)) && (++which > count))
		{
			res = ast_strdup(ch_gsm->alias);
			ast_mutex_unlock(&ch_gsm->lock);
			break;
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	// compare with special case "all"
	if ((!res) && (pg_general_channel_gsm_list.first) && (all)) {
		if ((!strncmp(begin, "all", beginlen)) && (++which > count))
			res = ast_strdup("all");
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_name()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_action()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_action(const char *begin, int count)
{
	char *res;
	int beginlen;
	int which;
	size_t i;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	for (i=0; i<PG_CLI_ACTION_HANDLERS_COUNT(pg_cli_channel_gsm_action_handlers); i++)
	{
		// get actions name
		if ((!strncmp(begin, pg_cli_channel_gsm_action_handlers[i].name, beginlen))
				&& (++which > count)){
			res = ast_strdup(pg_cli_channel_gsm_action_handlers[i].name);
			break;
		}
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_action()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_action_power_on_off()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_action_power_on_off(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->flags.power) {
			if((!strncmp(begin, "off", beginlen)) && (++which > count))
				res = ast_strdup("off");
		} else {
			if ((!strncmp(begin, "on", beginlen)) && (++which > count))
				res = ast_strdup("on");
		}
		ast_mutex_unlock(&ch_gsm->lock);
	} else if (!strcmp(channel_gsm, "all")) {
		if((!res) && (!strncmp(begin, "on", beginlen)) && (++which > count))
			res = ast_strdup("on");
		if((!res) && (!strncmp(begin, "off", beginlen)) && (++which > count))
			res = ast_strdup("off");
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_action_power_on_off()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_action_params()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_action_params(const char *begin, int count, const char *operation)
{
	char *res;
	int beginlen;
	int which;
	size_t i;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	int opid = pg_get_channel_gsm_param_operation(operation);

	for (i=0; i<PG_CHANNEL_GSM_PARAMS_COUNT(pg_channel_gsm_params); i++)
	{
		// get actions name
		if ((opid & pg_channel_gsm_params[i].ops) &&
				(!strncmp(begin, pg_channel_gsm_params[i].name, beginlen)) &&
					(++which > count)) {
			res = ast_strdup(pg_channel_gsm_params[i].name);
			break;
		}
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_action_params()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_action_debug_params()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_action_debug_params(const char *begin, int count)
{
	char *res;
	int beginlen;
	int which;
	size_t i;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_channel_gsm_debugs); i++)
	{
		// get actions name
		if ((!strncmp(begin, pg_channel_gsm_debugs[i].name, beginlen)) && (++which > count)) {
			res = ast_strdup(pg_channel_gsm_debugs[i].name);
			break;
		}
	}

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_action_debug_params()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_action_debug_param_on_off()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_action_debug_param_on_off(const char *begin, int count,
																			const char *channel_gsm, const char *debug_prm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		if (!strcmp(debug_prm, "at")) {
			if (ch_gsm->debug.at) {
				if((!strncmp(begin, "off", beginlen)) && (++which > count))
					res = ast_strdup("off");
			} else {
				if ((!strncmp(begin, "on", beginlen)) && (++which > count))
					res = ast_strdup("on");
			}
		} else if (!strcmp(debug_prm, "receiver")) {
			if (ch_gsm->debug.receiver) {
				if((!strncmp(begin, "off", beginlen)) && (++which > count))
					res = ast_strdup("off");
			} else {
				if ((!strncmp(begin, "on", beginlen)) && (++which > count))
					res = ast_strdup("on");
			}
		} else {
			if((!res) && (!strncmp(begin, "on", beginlen)) && (++which > count))
				res = ast_strdup("on");
			if((!res) && (!strncmp(begin, "off", beginlen)) && (++which > count))
				res = ast_strdup("off");
		}
		ast_mutex_unlock(&ch_gsm->lock);
	} else if (!strcmp(channel_gsm, "all")) {
		if((!res) && (!strncmp(begin, "on", beginlen)) && (++which > count))
			res = ast_strdup("on");
		if((!res) && (!strncmp(begin, "off", beginlen)) && (++which > count))
			res = ast_strdup("off");
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_action_debug_param_on_off()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_progress()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_progress(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	size_t i;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);

		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_progress_types); i++)
		{
			if ((pg_call_gsm_progress_types[i].id != ch_gsm->config.progress) &&
				(!strncmp(begin, pg_call_gsm_progress_types[i].name, beginlen)) &&
					(++which > count)) {
				res = ast_strdup(pg_call_gsm_progress_types[i].name);
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_progress()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_incoming()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_incoming(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	size_t i;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);

		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_incoming_types); i++)
		{
			if ((pg_call_gsm_incoming_types[i].id != ch_gsm->config.incoming_type) &&
				(!strncmp(begin, pg_call_gsm_incoming_types[i].name, beginlen)) &&
					(++which > count)) {
				res = ast_strdup(pg_call_gsm_incoming_types[i].name);
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_incoming()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_outgoing()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_outgoing(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	size_t i;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);

		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_call_gsm_outgoing_types); i++)
		{
			if ((pg_call_gsm_outgoing_types[i].id != ch_gsm->config.outgoing_type) &&
				(!strncmp(begin, pg_call_gsm_outgoing_types[i].name, beginlen)) &&
					(++which > count)) {
				res = ast_strdup(pg_call_gsm_outgoing_types[i].name);
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_outgoing()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_callwait()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_callwait(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	size_t i;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_callwait_states); i++)
		{
			if ((pg_callwait_states[i].id != ch_gsm->config.callwait) &&
				(pg_callwait_states[i].id != PG_CALLWAIT_STATE_QUERY) &&
				(!strncmp(begin, pg_callwait_states[i].name, beginlen)) &&
					(++which > count)) {
				res = ast_strdup(pg_callwait_states[i].name);
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_callwait()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_clir()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_clir(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	size_t i;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_clir_states); i++)
		{
			if ((pg_clir_states[i].id != ch_gsm->config.clir) &&
				(pg_clir_states[i].id != PG_CLIR_STATE_QUERY) &&
				(!strncmp(begin, pg_clir_states[i].name, beginlen)) &&
					(++which > count)) {
				res = ast_strdup(pg_clir_states[i].name);
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_clir()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_ali_nelec()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_ali_nelec(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->config.ali_nelec == VIN_DIS) {
			if((!strncmp(begin, "active", beginlen)) && (++which > count))
				res = ast_strdup("active");
		} else {
			if ((!strncmp(begin, "inactive", beginlen)) && (++which > count))
				res = ast_strdup("inactive");
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_ali_nelec()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_ali_nelec_tm()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_ali_nelec_tm(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->config.ali_nelec_tm == VIN_DTM_ON) {
			if((!strncmp(begin, "off", beginlen)) && (++which > count))
				res = ast_strdup("off");
		} else {
			if ((!strncmp(begin, "on", beginlen)) && (++which > count))
				res = ast_strdup("on");
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_ali_nelec_tm()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_ali_nelec_oldc()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_ali_nelec_oldc(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->config.ali_nelec_oldc == VIN_OLDC_ZERO) {
			if((!strncmp(begin, "oldc", beginlen)) && (++which > count))
				res = ast_strdup("oldc");
		} else {
			if ((!strncmp(begin, "zero", beginlen)) && (++which > count))
				res = ast_strdup("zero");
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_ali_nelec_oldc()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_ali_nelec_as()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_ali_nelec_as(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->config.ali_nelec_as == VIN_AS_RUN) {
			if((!strncmp(begin, "stop", beginlen)) && (++which > count))
				res = ast_strdup("stop");
		} else {
			if ((!strncmp(begin, "run", beginlen)) && (++which > count))
				res = ast_strdup("run");
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_ali_nelec_as()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_ali_nelec_nlp()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_ali_nelec_nlp(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->config.ali_nelec_nlp == VIN_OFF) {
			if((!strncmp(begin, "on", beginlen)) && (++which > count))
				res = ast_strdup("on");
		} else {
			if ((!strncmp(begin, "off", beginlen)) && (++which > count))
				res = ast_strdup("off");
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_ali_nelec_nlp()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_channel_gsm_ali_nelec_nlpm()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_channel_gsm_ali_nelec_nlpm(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	size_t i;
	struct pg_channel_gsm *ch_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		for (i=0; i<PG_GENERIC_PARAMS_COUNT(pg_vinetic_ali_nelec_nlpms); i++)
		{
			if ((pg_vinetic_ali_nelec_nlpms[i].id != ch_gsm->config.ali_nelec_nlpm) &&
				(!strncmp(begin, pg_vinetic_ali_nelec_nlpms[i].name, beginlen)) &&
					(++which > count)) {
				res = ast_strdup(pg_vinetic_ali_nelec_nlpms[i].name);
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_channel_gsm_ali_nelec_nlpm()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_trunk_gsm()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_trunk_gsm(const char *begin, int count)
{
	char *res;
	int beginlen;
	int which;
	struct pg_trunk_gsm *tr_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	ast_mutex_lock(&pg_lock);

	AST_LIST_TRAVERSE(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry)
	{
		if ((!strncmp(begin, tr_gsm->name, beginlen)) && (++which > count)) {
			res = ast_strdup(tr_gsm->name);
			break;
		}
	}

	ast_mutex_unlock(&pg_lock);

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_trunk_gsm()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_trunk_gsm_channel_set()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_trunk_gsm_channel_set(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_trunk_gsm *tr_gsm;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	ast_mutex_lock(&pg_lock);

	AST_LIST_TRAVERSE(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry)
	{
		if ((!strncmp(begin, tr_gsm->name, beginlen)) && (++which > count) && (!pg_get_channel_gsm_from_trunk_by_name(tr_gsm, channel_gsm))) {
			res = ast_strdup(tr_gsm->name);
			break;
		}
	}

	ast_mutex_unlock(&pg_lock);

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_trunk_gsm_channel_set()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_trunk_gsm_channel_delete()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_trunk_gsm_channel_delete(const char *begin, int count, const char *channel_gsm)
{
	char *res;
	int beginlen;
	int which;
	struct pg_channel_gsm *ch_gsm;
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if ((ch_gsm = pg_get_channel_gsm_by_name(channel_gsm))) {
		ast_mutex_lock(&ch_gsm->lock);
		AST_LIST_TRAVERSE(&ch_gsm->trunk_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_channel_list_entry)
		{
			if ((!strncmp(begin, ch_gsm_fold->name, beginlen)) && (++which > count)) {
				res = ast_strdup(ch_gsm_fold->name);
				break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_trunk_gsm_channel_delete()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_generate_complete_context()
//------------------------------------------------------------------------------
static char *pg_cli_generate_complete_context(const char *begin, int count)
{
	char *res;
	int beginlen;
	int which;
	struct ast_context *ctx = NULL;

	res = NULL;
	which = 0;
	beginlen = strlen(begin);

	if (ast_rdlock_contexts())
		return NULL;

	while ((ctx = ast_walk_contexts(ctx)))
	{
		if (!strncasecmp(begin, ast_get_context_name(ctx), beginlen) && (++which > count)) {
			res = ast_strdup(ast_get_context_name(ctx));
			break;
		}
	}
	ast_unlock_contexts();

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_context()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_modinfo()
//------------------------------------------------------------------------------
static char *pg_cli_show_modinfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct timeval curr_time_mark;
	char buf[32];

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show modinfo";
			e->usage = "Usage: polygator show modinfo\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	// -- module info
	ast_cli(a->fd, "  Polygator module Info:\n");
	// show polygator module version
	ast_cli(a->fd, "  -- module version: %s\n", VERSION);
	// show polygator asterisk version
	ast_cli(a->fd, "  -- asterisk version: %s\n", ASTERISK_VERSION);
	// show polygator module uptime
	gettimeofday(&curr_time_mark, NULL);

	ast_cli(a->fd, "  -- started: %s", ctime(&pg_start_time.tv_sec));
	curr_time_mark.tv_sec -= pg_start_time.tv_sec;
	ast_cli(a->fd, "  -- uptime: %s (%ld sec)\n",
			pg_second_to_dhms(buf, curr_time_mark.tv_sec), curr_time_mark.tv_sec);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_modinfo()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_boards()
//------------------------------------------------------------------------------
static char *pg_cli_show_boards(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	size_t count;
	struct pg_board *brd;

	char buf[32];
	int number_fl;
	int type_fl;
	int name_fl;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show boards";
			e->usage = "Usage: polygator show boards [<type>]\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			// try to generate complete board type
			if (a->pos == 3)
				return pg_cli_generate_complete_board_type(a->word, a->n);

			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	if (a->argv[3]) {
		count = 0;
		number_fl = strlen("#");
		name_fl = strlen("Name");
		AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
		{
			ast_mutex_lock(&brd->lock);
			if (!strcmp(a->argv[3], brd->type)) {
				number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count));
				name_fl = mmax(name_fl, strlen(brd->name));
				count++;
			}
			ast_mutex_unlock(&brd->lock);
		}
		if (count) {
			ast_cli(a->fd, "| %-*s | %-*s |\n",
					number_fl, "#",
					name_fl, "Name");
			count = 0;
			AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
			{
				// lock board
				ast_mutex_lock(&brd->lock);
				if (!strcmp(a->argv[3], brd->type)) {
					ast_cli(a->fd, "| %-*lu | %-*s |\n",
							number_fl, (unsigned long int)count++,
							name_fl, brd->name);
				}
				// unlock board
				ast_mutex_unlock(&brd->lock);
			}
			ast_cli(a->fd, "  Total %lu board%s\n", (unsigned long int)count, ESS(count));
		} else
			ast_cli(a->fd, "  No boards found\n");
	} else {
		count = 0;
		number_fl = strlen("#");
		type_fl = strlen("Type");
		name_fl = strlen("Name");
		AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
		{
			ast_mutex_lock(&brd->lock);
			number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count));
			type_fl = mmax(type_fl, strlen(brd->type));
			name_fl = mmax(name_fl, strlen(brd->name));
			count++;
			ast_mutex_unlock(&brd->lock);
		}
		if (count) {
			ast_cli(a->fd, "| %-*s | %-*s | %-*s |\n",
					number_fl, "#",
					type_fl, "Type",
					name_fl, "Name");
			count = 0;
			AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
			{
				// lock board
				ast_mutex_lock(&brd->lock);
				ast_cli(a->fd, "| %-*lu | %-*s | %-*s |\n",
						number_fl, (unsigned long int)count++,
						type_fl, brd->type,
						name_fl, brd->name);
				// unlock board
				ast_mutex_unlock(&brd->lock);
			}
			ast_cli(a->fd, "  Total %lu board%s\n", (unsigned long int)count, ESS(count));
		} else 
			ast_cli(a->fd, "  No boards found\n");
	}

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_boards()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_channels()
//------------------------------------------------------------------------------
static char *pg_cli_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	size_t count;
	size_t total;
	struct pg_channel_gsm *ch_gsm;

	char buf[20];
	int number_fl;
	int alias_fl;
	int device_fl;
	int module_fl;
	int status_fl;
	int sim_fl;
	int reg_fl;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show channels";
			e->usage = "Usage: polygator show channels [<type>]\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	total = 0;
	count = 0;
	number_fl = strlen("#");
	alias_fl = strlen("Alias");
	device_fl = strlen("Device");
	module_fl = strlen("Module");
	status_fl = strlen("Status");
	sim_fl = strlen("SIM");
	reg_fl = strlen("Registered");
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count));
		alias_fl = mmax(alias_fl, strlen(ch_gsm->alias));
		device_fl = mmax(device_fl, strlen(ch_gsm->device));
		module_fl = mmax(module_fl, strlen(pg_gsm_module_type_to_string(ch_gsm->gsm_module_type)));
		status_fl = mmax(status_fl, strlen(ch_gsm->flags.enable?"enabled":"disabled"));
		sim_fl = mmax(sim_fl, strlen(ch_gsm->flags.sim_inserted?"inserted":""));
		reg_fl = mmax(reg_fl, strlen(ch_gsm->flags.sim_inserted?reg_status_print_short(ch_gsm->reg_stat):""));
		count++;
		ast_mutex_unlock(&ch_gsm->lock);
	}
	if (count) {
		ast_cli(a->fd, "  GSM channel%s:\n", ESS(count));
		ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
				number_fl, "#",
		  		alias_fl, "Alias",
				device_fl, "Device",
				module_fl, "Module",
				status_fl, "Status",
				sim_fl, "SIM",
				reg_fl, "Registered");
		count = 0;
		AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
		{
			ast_mutex_lock(&ch_gsm->lock);
			ast_cli(a->fd, "| %-*lu | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
					number_fl, (unsigned long int)count++,
					alias_fl, ch_gsm->alias,
					device_fl, ch_gsm->device,
					module_fl, pg_gsm_module_type_to_string(ch_gsm->gsm_module_type),
					status_fl, ch_gsm->flags.enable?"enabled":"disabled",
					sim_fl, ch_gsm->flags.sim_inserted?"inserted":"",
					reg_fl, ch_gsm->flags.sim_inserted?reg_status_print_short(ch_gsm->reg_stat):"");
			ast_mutex_unlock(&ch_gsm->lock);
		}
		total += count;
		ast_cli(a->fd, "  Total %lu GSM channel%s\n", (unsigned long int)count, ESS(count));
	}

	if (!total)
		ast_cli(a->fd, "  No channels found\n");

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_channels()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_trunks()
//------------------------------------------------------------------------------
static char *pg_cli_show_trunks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_trunk_gsm *tr_gsm;
	struct pg_channel_gsm *ch_gsm;
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold;
	size_t count;
	size_t total;

	char buf[20];
	int number_fl;
	int trunk_fl;
	int channel_fl;
	int status_fl;
	int sim_fl;
	int reg_fl;
	int oper_fl;
	int code_fl;

	switch(cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show trunks";
			e->usage = "Usage: polygator show trunks\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&pg_lock);
	total = 0;
	count = 0;
	number_fl = strlen("#");
	trunk_fl = strlen("Trunk");
	channel_fl = strlen("Channel");
	status_fl = strlen("Status");
	sim_fl = strlen("SIM");
	reg_fl = strlen("Registered");
	oper_fl = strlen("Operator");
	code_fl = strlen("Code");
	AST_LIST_TRAVERSE(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry)
	{
		number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count));
		trunk_fl = mmax(trunk_fl, strlen(tr_gsm->name));
		AST_LIST_TRAVERSE(&tr_gsm->channel_gsm_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry)
		{
			ch_gsm = ch_gsm_fold->channel_gsm;
			ast_mutex_lock(&ch_gsm->lock);
			channel_fl = mmax(channel_fl, strlen(ch_gsm->alias));
			status_fl = mmax(status_fl, strlen(ch_gsm->flags.enable?"enabled":"disabled"));
			sim_fl = mmax(sim_fl, strlen(ch_gsm->flags.sim_inserted?"inserted":""));
			reg_fl = mmax(reg_fl, strlen(ch_gsm->flags.enable?reg_status_print_short(ch_gsm->reg_stat):""));
			oper_fl = mmax(oper_fl, strlen(ch_gsm->operator_name?ch_gsm->operator_name:""));
			code_fl = mmax(code_fl, strlen(ch_gsm->operator_code?ch_gsm->operator_code:""));
			ast_mutex_unlock(&ch_gsm->lock);
		}
		count++;
	}
	if (count) {
		ast_cli(a->fd, "  GSM trunk%s:\n", ESS(count));
		ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
				number_fl, "#",
		  		trunk_fl, "Trunk",
				channel_fl, "Channel",
				status_fl, "Status",
				sim_fl, "SIM",
				reg_fl, "Registered",
				oper_fl, "Operator",
				code_fl, "Code");
		count = 0;
		AST_LIST_TRAVERSE(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry)
		{
			snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count++);
			ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
					number_fl, buf,
			  		trunk_fl, tr_gsm->name,
					channel_fl, "",
					status_fl, "",
					sim_fl, "",
					reg_fl, "",
					oper_fl, "",
					code_fl, "");
			AST_LIST_TRAVERSE(&tr_gsm->channel_gsm_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry)
			{
				ch_gsm = ch_gsm_fold->channel_gsm;
				ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
					number_fl, "",
			  		trunk_fl, "",
					channel_fl, ch_gsm->alias,
					status_fl, ch_gsm->flags.enable?"enabled":"disabled",
					sim_fl, ch_gsm->flags.sim_inserted?"inserted":"",
					reg_fl, ch_gsm->flags.enable?reg_status_print_short(ch_gsm->reg_stat):"",
					oper_fl, ch_gsm->operator_name?ch_gsm->operator_name:"",
					code_fl, ch_gsm->operator_code?ch_gsm->operator_code:"");
				ast_mutex_unlock(&ch_gsm->lock);
			}
		}
		total += count;
		ast_cli(a->fd, "  Total %lu GSM trunk%s\n", (unsigned long int)count, ESS(count));
	}
	if (!total)
		ast_cli(a->fd, "  No trunks found\n");
	
	ast_mutex_unlock(&pg_lock);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_trunks()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_calls()
//------------------------------------------------------------------------------
static char *pg_cli_show_calls(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_channel_gsm *ch_gsm;
	struct pg_call_gsm *call_gsm;
	struct timeval tv;
	char calling[MAX_ADDRESS_LENGTH];
	char called[MAX_ADDRESS_LENGTH];

	size_t count;
	size_t total;

	char numbuf[20];
	char linbuf[20];
	char durbuf[20];
	char bilbuf[20];
	int number_fl;
	int channel_fl;
	int line_fl;
	int state_fl;
	int direction_fl;
	int calling_fl;
	int called_fl;
	int duration_fl;
	int billing_fl;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show calls";
			e->usage = "Usage: polygator show calls\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
	}

	// check args count
	if(a->argc < 3)
		return CLI_SHOWUSAGE;

	gettimeofday(&tv, NULL);

	total = 0;
	count = 0;
	number_fl = strlen("#");
	channel_fl = strlen("Channel");
	line_fl = strlen("Line");
	state_fl = strlen("State");
	direction_fl = strlen("Direction");
	calling_fl = strlen("Calling");
	called_fl = strlen("Called");
	duration_fl = strlen("Duration");
	billing_fl = strlen("Billing");

	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		channel_fl = mmax(channel_fl, strlen(ch_gsm->alias));
		AST_LIST_TRAVERSE(&ch_gsm->call_list, call_gsm, entry)
		{
			number_fl = mmax(number_fl, snprintf(numbuf, sizeof(numbuf), "%lu", (unsigned long int)count));
			line_fl = mmax(line_fl, snprintf(linbuf, sizeof(linbuf), "%d", call_gsm->line));
			state_fl = mmax(state_fl, strlen(pg_call_gsm_state_to_string(call_gsm->state)));
			direction_fl = mmax(direction_fl, strlen(pg_call_gsm_direction_to_string(call_gsm->direction)));
			calling_fl = mmax(calling_fl, snprintf(calling, sizeof(calling), "%s%s", (call_gsm->calling_name.type.full == 145)?("+"):(""), call_gsm->calling_name.value));
			called_fl = mmax(called_fl, snprintf(called, sizeof(called), "%s%s", (call_gsm->called_name.type.full == 145)?("+"):(""), call_gsm->called_name.value));
			duration_fl = mmax(duration_fl, snprintf(durbuf, sizeof(durbuf), "%ld", (long int)(tv.tv_sec - call_gsm->start_time.tv_sec)));
			billing_fl = mmax(billing_fl, snprintf(bilbuf, sizeof(bilbuf), "%ld", (long int)((call_gsm->answer_time.tv_sec)?(tv.tv_sec - call_gsm->answer_time.tv_sec):(0))));
			count++;
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}

	if (count) {
		ast_cli(a->fd, "  GSM call%s:\n", ESS(count));
		ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
				number_fl, "#",
				channel_fl, "Channel",
				line_fl, "Line",
				state_fl, "State",
				direction_fl, "Direction",
				calling_fl, "Calling",
				called_fl, "Called",
				duration_fl, "Duration",
				billing_fl, "Billing");
		count = 0;
		AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
		{
			ast_mutex_lock(&ch_gsm->lock);
			AST_LIST_TRAVERSE(&ch_gsm->call_list, call_gsm, entry)
			{
				snprintf(numbuf, sizeof(numbuf), "%lu", (unsigned long int)count);
				snprintf(linbuf, sizeof(linbuf), "%d", call_gsm->line);
				snprintf(calling, sizeof(calling), "%s%s", (call_gsm->calling_name.type.full == 145)?("+"):(""), call_gsm->calling_name.value);
				snprintf(called, sizeof(called), "%s%s", (call_gsm->called_name.type.full == 145)?("+"):(""), call_gsm->called_name.value);
				snprintf(durbuf, sizeof(durbuf), "%ld", (long int)(tv.tv_sec - call_gsm->start_time.tv_sec));
				snprintf(bilbuf, sizeof(bilbuf), "%ld", (long int)((call_gsm->answer_time.tv_sec)?(tv.tv_sec - call_gsm->answer_time.tv_sec):(0)));
				ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %*s | %*s |\n",
						number_fl, numbuf,
						channel_fl, ch_gsm->alias,
						line_fl, linbuf,
						state_fl, pg_call_gsm_state_to_string(call_gsm->state),
						direction_fl, pg_call_gsm_direction_to_string(call_gsm->direction),
						calling_fl, calling,
						called_fl, called,
						duration_fl, durbuf,
						billing_fl, bilbuf);
				count++;
			}
			ast_mutex_unlock(&ch_gsm->lock);
		}
		total += count;
		ast_cli(a->fd, "  Total %lu GSM call%s\n", (unsigned long int)count, ESS(count));
	}

	if (!total)
		ast_cli(a->fd, "  No calls\n");

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_calls()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_gsm_call_stat_out()
//------------------------------------------------------------------------------
static char *pg_cli_show_gsm_call_stat_out(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_channel_gsm *ch_gsm;

	struct timeval tv_begin, tv_end;
	struct ast_tm tm_begin, tm_end;

	size_t count;
	size_t total;
	
	char frombuf[40];
	char tobuf[40];

	char numbuf[20];
	char totbuf[20];
	char ansbuf[20];
	char durbuf[20];
	char acdbuf[20];
	char asrbuf[20];

	int number_fl;
	int channel_fl;
	int total_fl;
	int answered_fl;
	int duration_fl;
	int acd_fl;
	int asr_fl;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show gsm call stat out";
			e->usage = "Usage: polygator show gsm call stat out\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
	}

	// check args count
	if(a->argc < 6)
		return CLI_SHOWUSAGE;

	tv_set(tv_begin, 0, 0);
	gettimeofday(&tv_end, NULL);

	if (a->argc == 7) {
		if (!strcmp(a->argv[6], "hour")) {
			tv_begin.tv_sec = tv_end.tv_sec - 60 * 60;
		} else if (!strcmp(a->argv[6], "day")) {
			tv_begin.tv_sec = tv_end.tv_sec - 60 * 60 * 24;
		} else if (!strcmp(a->argv[6], "week")) {
			tv_begin.tv_sec = tv_end.tv_sec - 60 * 60 * 24 * 7;
		} else {
			ast_strptime(a->argv[6], "%Y-%m-%d %H:%M:%S", &tm_begin);
			tv_begin = ast_mktime(&tm_begin, NULL); 
		}
	} else if (a->argc == 8) {
		ast_strptime(a->argv[6], "%Y-%m-%d %H:%M:%S", &tm_begin);
		tv_begin = ast_mktime(&tm_begin, NULL); 
		ast_strptime(a->argv[7], "%Y-%m-%d %H:%M:%S", &tm_end);
		tv_end = ast_mktime(&tm_end, NULL); 
	}

	if (tv_begin.tv_sec) {
		ast_localtime(&tv_begin, &tm_begin, NULL);
		ast_strftime(frombuf, sizeof(frombuf), "%Y-%m-%d %H:%M:%S", &tm_begin);
	} else
		snprintf(frombuf, sizeof(frombuf), "begin");
	ast_localtime(&tv_end, &tm_end, NULL);
	ast_strftime(tobuf, sizeof(tobuf), "%Y-%m-%d %H:%M:%S", &tm_end);

	total = 0;
	count = 0;
	number_fl = strlen("#");
	channel_fl = strlen("Channel");
	total_fl = strlen("Total");
	answered_fl = strlen("Answered");
	duration_fl = strlen("Duration");
	acd_fl = strlen("ACD");
	asr_fl = strlen("ASR");

	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);

		if (ch_gsm->imsi) {
			ch_gsm->out_total_call_count = pg_cdr_table_get_out_total_call_count("imsi", ch_gsm->imsi, tv_begin.tv_sec, tv_end.tv_sec, NULL);
			ch_gsm->out_answered_call_count = pg_cdr_table_get_out_answered_call_count("imsi", ch_gsm->imsi, tv_begin.tv_sec, tv_end.tv_sec, NULL);
			ch_gsm->out_active_call_duration = pg_cdr_table_get_out_active_call_duration("imsi", ch_gsm->imsi, tv_begin.tv_sec, tv_end.tv_sec, NULL);
		} else {
			ch_gsm->out_total_call_count = pg_cdr_table_get_out_total_call_count("channel", ch_gsm->device, tv_begin.tv_sec, tv_end.tv_sec, NULL);
			ch_gsm->out_answered_call_count = pg_cdr_table_get_out_answered_call_count("channel", ch_gsm->device, tv_begin.tv_sec, tv_end.tv_sec, NULL);
			ch_gsm->out_active_call_duration = pg_cdr_table_get_out_active_call_duration("channel", ch_gsm->device, tv_begin.tv_sec, tv_end.tv_sec, NULL);
		}

		if (ch_gsm->out_answered_call_count)
			ch_gsm->acd = ch_gsm->out_active_call_duration/ch_gsm->out_answered_call_count;
		else
			ch_gsm->acd = 0;

		if (ch_gsm->out_total_call_count)
			ch_gsm->asr = (100 * ch_gsm->out_answered_call_count)/ch_gsm->out_total_call_count;
		else
			ch_gsm->asr = 0;

		number_fl = mmax(number_fl, snprintf(numbuf, sizeof(numbuf), "%lu", (unsigned long int)count));
		channel_fl = mmax(channel_fl, strlen(ch_gsm->alias));
		total_fl = mmax(total_fl, snprintf(totbuf, sizeof(totbuf), "%ld", (long int)ch_gsm->out_total_call_count));
		answered_fl = mmax(answered_fl, snprintf(ansbuf, sizeof(ansbuf), "%ld", (long int)ch_gsm->out_answered_call_count));
		duration_fl = mmax(duration_fl, snprintf(durbuf, sizeof(durbuf), "%ld", (long int)ch_gsm->out_active_call_duration));
		acd_fl = mmax(acd_fl, snprintf(acdbuf, sizeof(acdbuf), "%ld", (long int)ch_gsm->acd));
		asr_fl = mmax(asr_fl, snprintf(asrbuf, sizeof(asrbuf), "%ld%%", (long int)ch_gsm->asr));

		count++;
		ast_mutex_unlock(&ch_gsm->lock);
	}

	if (count) {
		ast_cli(a->fd, "  Outgoing GSM call statistic from %s to %s\n", frombuf, tobuf);
		ast_cli(a->fd, "  GSM channel%s:\n", ESS(count));
		ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
				number_fl, "#",
				channel_fl, "Channel",
				total_fl, "Total",
				answered_fl, "Answered",
				duration_fl, "Duration",
				acd_fl, "ACD",
				asr_fl, "ASR");
		count = 0;
		AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
		{
			ast_mutex_lock(&ch_gsm->lock);
			snprintf(numbuf, sizeof(numbuf), "%lu", (unsigned long int)count);
			snprintf(totbuf, sizeof(totbuf), "%ld", (long int)ch_gsm->out_total_call_count);
			snprintf(ansbuf, sizeof(ansbuf), "%ld", (long int)ch_gsm->out_answered_call_count);
			snprintf(durbuf, sizeof(durbuf), "%ld", (long int)ch_gsm->out_active_call_duration);
			snprintf(acdbuf, sizeof(acdbuf), "%ld", (long int)ch_gsm->acd);
			snprintf(asrbuf, sizeof(asrbuf), "%ld%%", (long int)ch_gsm->asr);
			ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
					number_fl, numbuf,
					channel_fl, ch_gsm->alias,
					total_fl, totbuf,
					answered_fl, ansbuf,
					duration_fl, durbuf,
					acd_fl, acdbuf,
					asr_fl, asrbuf);
			count++;
			ast_mutex_unlock(&ch_gsm->lock);
		}
		total += count;
		ast_cli(a->fd, "  Total %lu GSM channel%s\n", (unsigned long int)count, ESS(count));
	}

	if (!total)
		ast_cli(a->fd, "  No channels found\n");

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_gsm_call_stat_out()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_netinfo()
//------------------------------------------------------------------------------
static char *pg_cli_show_netinfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	size_t count;
	size_t total;
	struct pg_channel_gsm *ch_gsm;

	char buf[20];
	char rssi[32];
	int number_fl;
	int alias_fl;
	int status_fl;
	int sim_fl;
	int reg_fl;
	int oper_fl;
	int code_fl;
	int imsi_fl;
	int rssi_fl;
	int ber_fl;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show netinfo";
			e->usage = "Usage: polygator show netinfo\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	total = 0;
	count = 0;
	number_fl = strlen("#");
	alias_fl = strlen("Alias");
	status_fl = strlen("Status");
	sim_fl = strlen("SIM");
	reg_fl = strlen("Registered");
	oper_fl = strlen("Operator");
	code_fl = strlen("Code");
	imsi_fl = strlen("IMSI");
	rssi_fl = strlen("RSSI");
	ber_fl = strlen("BER");
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count));
		alias_fl = mmax(alias_fl, strlen(ch_gsm->alias));
		status_fl = mmax(status_fl, strlen(ch_gsm->flags.enable?"enabled":"disabled"));
		sim_fl = mmax(sim_fl, strlen(ch_gsm->flags.sim_inserted?"inserted":""));
		reg_fl = mmax(reg_fl, strlen(ch_gsm->flags.sim_inserted?reg_status_print_short(ch_gsm->reg_stat):""));
		oper_fl = mmax(oper_fl, strlen(ch_gsm->operator_name?ch_gsm->operator_name:""));
		code_fl = mmax(code_fl, strlen(ch_gsm->operator_code?ch_gsm->operator_code:""));
		imsi_fl = mmax(imsi_fl, strlen(ch_gsm->flags.sim_inserted?(ch_gsm->imsi?ch_gsm->imsi:"unknown"):""));
		rssi_fl = mmax(rssi_fl, strlen(ch_gsm->flags.sim_inserted?rssi_print_short(rssi, ch_gsm->rssi):""));
		ber_fl = mmax(ber_fl, strlen(ch_gsm->flags.sim_inserted?ber_print_short(ch_gsm->ber):""));
		count++;
		ast_mutex_unlock(&ch_gsm->lock);
	}
	if (count) {
		ast_cli(a->fd, "  GSM channel%s:\n", ESS(count));
		ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
				number_fl, "#",
				alias_fl, "Alias",
				status_fl, "Status",
				sim_fl, "SIM",
				reg_fl, "Registered",
				oper_fl, "Operator",
				code_fl, "Code",
				imsi_fl, "IMSI",
				rssi_fl, "RSSI",
				ber_fl, "BER");
		count = 0;
		AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
		{
			ast_mutex_lock(&ch_gsm->lock);
			ast_cli(a->fd, "| %-*lu | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %*s | %*s |\n",
					number_fl, (unsigned long int)count++,
					alias_fl, ch_gsm->alias,
					status_fl, ch_gsm->flags.enable?"enabled":"disabled",
					sim_fl, ch_gsm->flags.sim_inserted?"inserted":"",
					reg_fl, ch_gsm->flags.sim_inserted?reg_status_print_short(ch_gsm->reg_stat):"",
					oper_fl, ch_gsm->operator_name?ch_gsm->operator_name:"",
					code_fl, ch_gsm->operator_code?ch_gsm->operator_code:"",
					imsi_fl, ch_gsm->flags.sim_inserted?(ch_gsm->imsi?ch_gsm->imsi:"unknown"):"",
					rssi_fl, ch_gsm->flags.sim_inserted?rssi_print_short(rssi, ch_gsm->rssi):"",
					ber_fl, ch_gsm->flags.sim_inserted?ber_print_short(ch_gsm->ber):"");
			ast_mutex_unlock(&ch_gsm->lock);
		}
		total += count;
		ast_cli(a->fd, "  Total %lu GSM channel%s\n", (unsigned long int)count, ESS(count));
	}

	if (!total)
		ast_cli(a->fd, "  No channels found\n");

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_netinfo()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_devinfo()
//------------------------------------------------------------------------------
static char *pg_cli_show_devinfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	size_t count;
	size_t total;
	struct pg_channel_gsm *ch_gsm;

	char buf[20];
	int number_fl;
	int alias_fl;
	int device_fl;
	int module_fl;
	int status_fl;
	int hw_fl;
	int fw_fl;
	int imei_fl;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show devinfo";
			e->usage = "Usage: polygator show devinfo\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	total = 0;
	count = 0;
	number_fl = strlen("#");
	alias_fl = strlen("Alias");
	device_fl = strlen("Device");
	module_fl = strlen("Module");
	status_fl = strlen("Status");
	hw_fl = strlen("Hardware");
	fw_fl = strlen("Firmware");
	imei_fl = strlen("IMEI");
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count));
		alias_fl = mmax(alias_fl, strlen(ch_gsm->alias));
		device_fl = mmax(device_fl, strlen(ch_gsm->device));
		module_fl = mmax(module_fl, strlen(pg_gsm_module_type_to_string(ch_gsm->gsm_module_type)));
		status_fl = mmax(status_fl, strlen(ch_gsm->flags.enable?"enabled":"disabled"));
		hw_fl = mmax(hw_fl, strlen(ch_gsm->model?ch_gsm->model:""));
		fw_fl = mmax(fw_fl, strlen(ch_gsm->firmware?ch_gsm->firmware:""));
		imei_fl = mmax(imei_fl, strlen(ch_gsm->imei?ch_gsm->imei:""));
		count++;
		ast_mutex_unlock(&ch_gsm->lock);
	}
	if (count) {
		ast_cli(a->fd, "  GSM channel%s:\n", ESS(count));
		ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
				number_fl, "#",
				alias_fl, "Alias",
				device_fl, "Device",
				module_fl, "Module",
				status_fl, "Status",
				hw_fl, "Hardware",
				fw_fl, "Firmware",
				imei_fl, "IMEI");
		count = 0;
		AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
		{
			ast_mutex_lock(&ch_gsm->lock);
			ast_cli(a->fd, "| %-*lu | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
					number_fl, (unsigned long int)count++,
					alias_fl, ch_gsm->alias,
					device_fl, ch_gsm->device,
					module_fl, pg_gsm_module_type_to_string(ch_gsm->gsm_module_type),
					status_fl, ch_gsm->flags.enable?"enabled":"disabled",
					hw_fl, ch_gsm->model?ch_gsm->model:"",
					fw_fl, ch_gsm->firmware?ch_gsm->firmware:"",
					imei_fl, ch_gsm->imei?ch_gsm->imei:"");
			ast_mutex_unlock(&ch_gsm->lock);
		}
		total += count;
		ast_cli(a->fd, "  Total %lu GSM channel%s\n", (unsigned long int)count, ESS(count));
	}

	if (!total)
		ast_cli(a->fd, "  No channels found\n");

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_devinfo()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_board()
//------------------------------------------------------------------------------
static char *pg_cli_show_board(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_board *brd;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show board";
			e->usage = "Usage: polygator show board <board>\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			// try to generate complete board name
			if (a->pos == 3)
				return pg_cli_generate_complete_board_name(a->word, a->n);

			return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	// get board by name
	brd = pg_get_board_by_name(a->argv[3]);

	if (brd) {
		ast_mutex_lock(&brd->lock);

		ast_cli(a->fd, "  Board \"%s\"\n", brd->name);
		ast_cli(a->fd, "  -- type = %s\n", brd->type);

		ast_mutex_unlock(&brd->lock);
	} else
		ast_cli(a->fd, "  Board \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_board()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_vinetic()
//------------------------------------------------------------------------------
static char *pg_cli_show_vinetic(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_vinetic *vin;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show vinetic";
			e->usage = "Usage: polygator show vinetic <vinetic>\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			// try to generate complete VINETIC name
			if (a->pos == 3)
				return pg_cli_generate_complete_vinetic_name(a->word, a->n);

			return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	// get vinetic by name
	vin = pg_get_vinetic_by_name(a->argv[3]);

	if (vin) {
		ast_mutex_lock(&vin->lock);
		ast_cli(a->fd, "  VINETIC \"%s\"\n", vin->name);
		ast_cli(a->fd, "  -- device = %s\n", vin->path);
		if (pg_is_vinetic_run(vin)) {
			ast_cli(a->fd, "  -- EDSP firmware version %u.%u.%u\n",
				(vin->context.edsp_sw_version_register.mv << 13) +
				(vin->context.edsp_sw_version_register.prt << 12) +
				(vin->context.edsp_sw_version_register.features << 0),
				vin->context.edsp_sw_version_register.main_version,
				vin->context.edsp_sw_version_register.release);

		} else
			ast_cli(a->fd, "  is not running\n");

		ast_mutex_unlock(&vin->lock);
	} else
		ast_cli(a->fd, "  VINETIC \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_vinetic()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_show_channel_gsm()
//------------------------------------------------------------------------------
static char *pg_cli_show_channel_gsm(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_channel_gsm *ch_gsm;
	char buf[256];

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator show channel gsm";
			e->usage = "Usage: polygator show channel gsm <channel>\n";
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			// try to generate complete channel name
			if (a->pos == 4)
				return pg_cli_generate_complete_channel_gsm_name(a->word, a->n, 0);
			return NULL;
	}

	if (a->argc < 5)
		return CLI_SHOWUSAGE;

	if ((ch_gsm = pg_get_channel_gsm_by_name(a->argv[4]))) {

		ast_mutex_lock(&ch_gsm->lock);

		ast_cli(a->fd, "  Channel \"%s\"\n", ch_gsm->alias);
		ast_cli(a->fd, "  -- device = %s\n", ch_gsm->device);
		ast_cli(a->fd, "  -- TTY device = %s\n", ch_gsm->tty_path);
		ast_cli(a->fd, "  -- SIM device = %s\n", ch_gsm->sim_path);
		ast_cli(a->fd, "  -- module = %s\n", pg_gsm_module_type_to_string(ch_gsm->gsm_module_type));
		ast_cli(a->fd, "  -- power = %s\n", ch_gsm->flags.power?"on":"off");
		ast_cli(a->fd, "  -- status = %s\n", ch_gsm->flags.enable?"enabled":"disabled");
		ast_cli(a->fd, "  -- state = %s\n",  pg_cahnnel_gsm_state_to_string(ch_gsm->state));
#if 0
		sprintf(buf, "%p", (void *)chnl->channel_thread);
		ast_cli(a->fd, "  -- thread = %s\n", (chnl->channel_thread == AST_PTHREADT_NULL)?("null"):(buf));
#endif
		ast_cli(a->fd, "  -- IMEI = %s\n", strlen(ch_gsm->imei)?ch_gsm->imei:"unknown");
		ast_cli(a->fd, "  -- SIM = %s\n", ch_gsm->flags.sim_inserted?"inserted":"removed");
		ast_cli(a->fd, "  -- ICCID = %s\n", ch_gsm->iccid?ch_gsm->iccid:"unknown");
		ast_cli(a->fd, "  -- IMSI = %s\n", ch_gsm->imsi?ch_gsm->imsi:"unknown");
		ast_cli(a->fd, "  -- operator = %s (%s)\n", ch_gsm->operator_name?ch_gsm->operator_name:"unknown", ch_gsm->operator_code?ch_gsm->operator_code:"unknown");
		ast_cli(a->fd, "  -- registration status = %s\n", ch_gsm->flags.sim_inserted?reg_status_print(ch_gsm->reg_stat):"unknown");
		ast_cli(a->fd, "  -- number = %s\n", ch_gsm->flags.sim_inserted?address_show(buf, &ch_gsm->subscriber_number, 1):"unknown");
		ast_cli(a->fd, "  -- RSSI = %s\n", ch_gsm->flags.sim_inserted?rssi_print(buf, ch_gsm->rssi):"unknown");
		ast_cli(a->fd, "  -- BER = %s\n", ch_gsm->flags.sim_inserted?ber_print(ch_gsm->ber):"unknown");
		ast_cli(a->fd, "  -- call wait state = %s\n", ch_gsm->flags.sim_inserted?pg_callwait_state_to_string(ch_gsm->callwait):"unknown");
		ast_cli(a->fd, "  -- CLIR state = %s\n", ch_gsm->flags.sim_inserted?pg_clir_state_to_string(ch_gsm->clir):"unknown");
		ast_cli(a->fd, "  -- CLIR status = %s\n", ch_gsm->flags.sim_inserted?pg_clir_status_to_string(ch_gsm->clir_status):"unknown");
// 		ast_cli(a->fd, "  -- balance request string = %s\n", ch_gsm->config.balance_request?ch_gsm->config.balance_request:"unknown");
// 		ast_cli(a->fd, "  -- balance = [%s]\n", ch_gsm->balance?ch_gsm->balance:"unknown");
		ast_cli(a->fd, "  -- outgoing = %s\n", pg_call_gsm_outgoing_to_string(ch_gsm->config.outgoing_type));
		ast_cli(a->fd, "  -- incoming = %s\n", pg_call_gsm_incoming_type_to_string(ch_gsm->config.incoming_type));
		if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_SPEC)
		  ast_cli(a->fd, "  -- incomingto = %s\n", ch_gsm->config.gsm_call_extension);
		ast_cli(a->fd, "  -- context = %s\n", ch_gsm->config.gsm_call_context);

#if 0
		ast_cli(a->fd, "  -- last incoming call time = %s (%ld sec)\n",
				eggsm_second_to_dhms(buf, chnl->last_time_incoming), chnl->last_time_incoming);
		ast_cli(a->fd, "  -- last outgoing call time = %s (%ld sec)\n",
				eggsm_second_to_dhms(buf, chnl->last_time_outgoing), chnl->last_time_outgoing);

		ast_cli(a->fd, "  -- total incoming call time = %s (%ld sec)\n",
				eggsm_second_to_dhms(buf, chnl->call_time_incoming), chnl->call_time_incoming);
		ast_cli(a->fd, "  -- total outgoing call time = %s (%ld sec)\n",
				eggsm_second_to_dhms(buf, chnl->call_time_outgoing), chnl->call_time_outgoing);
		ast_cli(a->fd, "  -- total call time = %s (%ld sec)\n",
				eggsm_second_to_dhms(buf, (chnl->call_time_incoming + chnl->call_time_outgoing)),
										(chnl->call_time_incoming + chnl->call_time_outgoing));

		ast_cli(a->fd, "  -- TX pass (last) = %u frames\n", chnl->send_frame_curr);
		ast_cli(a->fd, "  -- TX drop (last) = %u frames\n", chnl->send_drop_curr);
		ast_cli(a->fd, "  -- TX sid (last) = %u frames\n", chnl->send_sid_curr);
		ast_cli(a->fd, "  -- RX (last) = %u frames\n", chnl->recv_frame_curr);

		ast_cli(a->fd, "  -- TX pass (total) = %u frames\n", chnl->send_frame_total);
		ast_cli(a->fd, "  -- TX drop (total) = %u frames\n", chnl->send_drop_total);
		ast_cli(a->fd, "  -- TX sid (total) = %u frames\n", chnl->send_sid_total);
		ast_cli(a->fd, "  -- RX (total) = %u frames\n", chnl->recv_frame_total);
#endif
		ast_mutex_unlock(&ch_gsm->lock);
	} else
		ast_cli(a->fd, "  Channel <%s> not found\n", a->argv[4]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_channel_gsm()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_config_actions()
//------------------------------------------------------------------------------
static char *pg_cli_config_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char src_fn[64];
	char dst_fn[64];
	char src_path[PATH_MAX];
	char dst_path[PATH_MAX];
	int sz;

	char *gline;
	char *gargv[AST_MAX_ARGS];
	int gargc;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator config [save|copy|delete]";
			snprintf(pg_cli_config_actions_usage, sizeof(pg_cli_config_actions_usage),
					"Usage: polygator config <operation>=\"save\"|\"copy\"|\"delete\"\n");
			e->usage = pg_cli_config_actions_usage;
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			gline = ast_strdupa(a->line);
			if (!(pg_cli_generating_prepare(gline, &gargc, gargv))) {
				if((a->pos == 3) || ((!strcmp(gargv[2], "copy")) && (a->pos == 4)))
					return pg_cli_generate_complete_config_filename(a->word, a->n);
			}
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc <= 2) {
		snprintf(pg_cli_config_actions_usage, sizeof(pg_cli_config_actions_usage),
				"Usage: polygator config <operation>=\"save\"|\"copy\"|\"delete\"\n");
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[2], "save")) {
		// save config file
		// get filename for configuration storage
		if (a->argv[3]) {
			// check for file name leaded "polygator"
			if (!strncasecmp(a->argv[3], "polygator", strlen("polygator")))
				sprintf(dst_fn, "%s", a->argv[3]);
			else {
				ast_cli(a->fd, "  -- bad file name \"%s\" - must begin by \"polygator\"\n", a->argv[3]);
				return CLI_SUCCESS;
			}
		} else
			// use default filename "polygator.conf"
			sprintf(dst_fn, "%s", pg_config_file);
		// build full path filename
		// new file -- file to create
		sprintf(dst_path, "%s/%s", ast_config_AST_CONFIG_DIR, dst_fn);
		// old file -- file to backup
		sprintf(src_path, "%s/%s.bak", ast_config_AST_CONFIG_DIR, dst_fn);
		// if new file is existing file store as backup file
		if (!rename(dst_path, src_path))
			ast_cli(a->fd, "  -- file \"%s\" exist - stored as \"%s.bak\"\n", dst_fn, dst_fn);
		// build new configuration file
		if ((sz = pg_config_file_build(dst_fn)) > 0)
			ast_cli(a->fd, "  -- configuration saved to \"%s\" - %d bytes\n", dst_fn, sz);
		else
			ast_cli(a->fd, "  -- can't build \"%s\" file\n", dst_fn);
	} else if (!strcasecmp(a->argv[2], "copy")) {
		// copy config file
		// get destination filename
		if (a->argv[3]) {
			// check for file name leaded "polygator"
			if (!strncasecmp(a->argv[3], "polygator", strlen("polygator")))
				sprintf(dst_fn, "%s", a->argv[3]);
			else {
				ast_cli(a->fd, "  -- bad destination file name \"%s\" - must begin by \"polygator\"\n", a->argv[3]);
				return CLI_SUCCESS;
			}
		} else {
			snprintf(pg_cli_config_actions_usage, sizeof(pg_cli_config_actions_usage),
					"Usage: polygator config copy <dst> [<src>]\ndefault src=%s\n", pg_config_file);
			return CLI_SHOWUSAGE;
		}
		// get source filename - optional
		if (a->argv[4]) {
			// check for file name leaded "polygator"
			if (!strncasecmp(a->argv[4], "polygator", strlen("polygator")))
				sprintf(src_fn, "%s", a->argv[4]);
			else {
				ast_cli(a->fd, "  -- bad source file name \"%s\" - must begin by \"polygator\"\n", a->argv[4]);
				return CLI_SUCCESS;
			}
		} else 
			// use default filename "polygator.conf"
			sprintf(src_fn, "%s", pg_config_file);
		// copy configuration file
		if (!pg_config_file_copy(dst_fn, src_fn))
			ast_cli(a->fd, "  -- configuration copying from \"%s\" to \"%s\" succeeded\n", src_fn, dst_fn);
		else
			ast_cli(a->fd, "  -- can't copy configuration from \"%s\" to \"%s\"\n", src_fn, dst_fn);
	} else if(!strcasecmp(a->argv[2], "delete")) {
		// delete config file
		// get filename for deleting
		if (a->argv[3])
			snprintf(dst_fn, sizeof(dst_fn), "%s", a->argv[3]);
		else
			snprintf(dst_fn, sizeof(dst_fn), "%s", pg_config_file);
		// build full path filename
		sprintf(dst_path, "%s/%s", ast_config_AST_CONFIG_DIR, dst_fn);
		// remove file from filesystem
		if (unlink(dst_path) < 0)
			ast_cli(a->fd, "  -- can't delete file \"%s\": %s\n", dst_fn, strerror(errno));
		else
			ast_cli(a->fd, "  -- file \"%s\" deleted\n", dst_fn);
	} else {
		ast_cli(a->fd, "  -- unknown operation - %s\n", a->argv[2]);
		snprintf(pg_cli_config_actions_usage, sizeof(pg_cli_config_actions_usage),
				"Usage: polygator config <operation>=\"save\"|\"copy\"|\"delete\"\n");
		return CLI_SHOWUSAGE;
	}
	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_config_actions()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_actions()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	size_t i;
	cli_fn_type subhandler;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator channel gsm";
			snprintf(pg_cli_channel_gsm_actions_usage, sizeof(pg_cli_channel_gsm_actions_usage),
					 "Usage: polygator channel gsm <channel> <action> [...]\n");
			e->usage = pg_cli_channel_gsm_actions_usage;
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			// try to generate complete GSM channel name
			if (a->pos == 3)
				return pg_cli_generate_complete_channel_gsm_name(a->word, a->n, 1);
			// try to generate complete GSM channel action
			else if (a->pos == 4)
				return pg_cli_generate_complete_channel_gsm_action(a->word, a->n);
			// generation channel action parameters ...
			else if (a->pos >= 5) {
				// from this point delegate generation function to
				// action depended CLI entries
				subhandler = NULL;
				// search action CLI entry
				for (i=0; i<PG_CLI_ACTION_HANDLERS_COUNT(pg_cli_channel_gsm_action_handlers); i++)
				{
					// get actions by name
					if (strstr(a->line, pg_cli_channel_gsm_action_handlers[i].name)) {
						subhandler = pg_cli_channel_gsm_action_handlers[i].handler;
						break;
					}
				}
				if (subhandler)
					return subhandler(e, cmd, a);
			}
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc <= 4) {
		snprintf(pg_cli_channel_gsm_actions_usage, sizeof(pg_cli_channel_gsm_actions_usage),
				 "Usage: polygator channel gsm <channel> <action> [...]\n");
		return CLI_SHOWUSAGE;
	}

	subhandler = NULL;
	// search action CLI entry
	for (i=0; i<PG_CLI_ACTION_HANDLERS_COUNT(pg_cli_channel_gsm_action_handlers); i++)
	{
		// get actions by name
		if (!strcmp(a->argv[4], pg_cli_channel_gsm_action_handlers[i].name)) {
			subhandler = pg_cli_channel_gsm_action_handlers[i].handler;
			break;
		}
	}
	if (subhandler)
		return subhandler(e, cmd, a);

	// if command not handled
	return CLI_FAILURE;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_actions()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_action_power()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_action_power(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_channel_gsm *ch_gsm;

	char *gline;
	char *gargv[AST_MAX_ARGS];
	int gargc;

	size_t total;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			ast_cli(a->fd, "is ch_act_pwr subhandler -- CLI_INIT unsupported in this context\n");
			return CLI_FAILURE;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			gline = ast_strdupa(a->line);
			if (!(pg_cli_generating_prepare(gline, &gargc, gargv))) {
				if (a->pos == 5)
					return pg_cli_generate_complete_channel_gsm_action_power_on_off(a->word, a->n, gargv[3]);
			}
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 6) {
		snprintf(pg_cli_channel_gsm_actions_usage, sizeof(pg_cli_channel_gsm_actions_usage),
				"Usage: polygator channel gsm <channel> power on|off\n");
		return CLI_SHOWUSAGE;
	}

	total = 0;
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		if (!strcmp(a->argv[3], "all") || !strcmp(a->argv[3], ch_gsm->alias)) {
			total++;
			ast_cli(a->fd, "  GSM channel=\"%s\": ", ch_gsm->alias);
			if (ast_true(a->argv[5])) {
				// on 
				if (!ch_gsm->flags.power) {
					if (!pg_channel_gsm_power_set(ch_gsm, 1)) {
						ch_gsm->flags.power = 1;
						ast_mutex_unlock(&ch_gsm->lock);
						usleep(799999);
						ast_mutex_lock(&ch_gsm->lock);
						ast_cli(a->fd, "power on\n");
					} else
						ast_cli(a->fd, "can't set GSM power suply to on: %s\n", strerror(errno));
				} else
					ast_cli(a->fd, "already set to on\n");
			} else {
				// off
				if (ch_gsm->flags.power) {
					if (!ch_gsm->flags.enable) {
						if (!pg_channel_gsm_power_set(ch_gsm, 0)) {
							ch_gsm->flags.power = 0;
							ast_cli(a->fd, "power off\n");
						} else
							ast_cli(a->fd, "can't set GSM power suply to off: %s\n", strerror(errno));
					} else {
						ast_cli(a->fd, "already enabled\n");
						ast_cli(a->fd, "-- you must disable channel before\n");
					}
				} else
					ast_cli(a->fd, "already set to off\n");
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}

	if (!total)
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_power()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_action_enable_disable()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_action_enable_disable(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_channel_gsm *ch_gsm;

	unsigned int pwr_seq_num;
	size_t total;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			ast_cli(a->fd, "is ch_act_pwr subhandler -- CLI_INIT unsupported in this context\n");
			return CLI_FAILURE;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	total = 0;
	pwr_seq_num = 0;
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		if (!strcmp(a->argv[3], "all") || !strcmp(a->argv[3], ch_gsm->alias)) {
			total++;
			ast_cli(a->fd, "  GSM channel=\"%s\": ", ch_gsm->alias);
			if (!strcmp(a->argv[4], "enable")) {
				// enable
				if (!ch_gsm->flags.enable) {
					// start GSM channel workthread
					ch_gsm->flags.enable = 1;
					ch_gsm->power_sequence_number = pwr_seq_num++;
					if (ast_pthread_create_detached(&ch_gsm->thread, NULL, pg_channel_gsm_workthread, ch_gsm) < 0) {
						ast_cli(a->fd, "can't start workthread\n");
						ch_gsm->flags.enable = 0;
						ch_gsm->thread = AST_PTHREADT_NULL;
					} else
						ast_cli(a->fd, "enabled\n");
				} else
					ast_cli(a->fd, "already enabled\n");
			} else if(!strcmp(a->argv[4], "restart")) {
				// restart
				if (ch_gsm->flags.enable) {
					if (ch_gsm->flags.restart_now)
						ast_cli(a->fd, "restart signal already sent\n");
					else if(ch_gsm->flags.shutdown_now)
						ast_cli(a->fd, "shutdown signal already sent\n");
					else {
						ch_gsm->flags.restart = 1;
						ch_gsm->flags.restart_now = 1;
						ast_cli(a->fd, "send restart signal\n");
					}
				} else
					ast_cli(a->fd, "channel now disabled\n");
			} else {
				// disable
				if (ch_gsm->flags.enable) {
					// check shutdown flag
					if (!ch_gsm->flags.shutdown_now) {
						ch_gsm->flags.shutdown = 1;
						ch_gsm->flags.shutdown_now = 1;
						ast_cli(a->fd, "send shutdown signal\n");
					} else
							ast_cli(a->fd, "shutdown signal already sent\n");
				} else
					ast_cli(a->fd, "already disabled\n");
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}

	if (!total)
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_enable_disable()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_action_suspend_resume()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_action_suspend_resume(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_channel_gsm *ch_gsm;
	struct pg_call_gsm *call;

	size_t total;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			ast_cli(a->fd, "is ch_act_pwr subhandler -- CLI_INIT unsupported in this context\n");
			return CLI_FAILURE;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	total = 0;
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		if (!strcmp(a->argv[3], "all") || !strcmp(a->argv[3], ch_gsm->alias)) {
			total++;
			ast_cli(a->fd, "  GSM channel=\"%s\": ", ch_gsm->alias);
			if (!strcmp(a->argv[4], "suspend")) {
				// suspend
				ast_cli(a->fd, "suspend...\n");
				if (ch_gsm->state == PG_CHANNEL_GSM_STATE_RUN) {
					// stop all timers
					memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
					// hangup active calls
					AST_LIST_TRAVERSE(&ch_gsm->call_list, call, entry)
						pg_call_gsm_sm(call, PG_CALL_GSM_MSG_RELEASE_IND, AST_CAUSE_NORMAL_CLEARING);
					while (ch_gsm->call_list.first)
					{
						ast_mutex_unlock(&ch_gsm->lock);
						usleep(1000);
						ast_mutex_lock(&ch_gsm->lock);
					}
					pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
					ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_SUSPEND;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
					x_timer_set(ch_gsm->timers.waitsuspend, waitsuspend_timeout);
				} else if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SUSPEND) {
					ast_cli(a->fd, " module switch to suspend state\n");
				} else if ((ch_gsm->state == PG_CHANNEL_GSM_STATE_CHECK_PIN) || (ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_CALL_READY))
				  	ch_gsm->flags.suspend_now = 1;
				else
					ast_cli(a->fd, " switching to suspend state from \"%s\" not allowed\n", pg_cahnnel_gsm_state_to_string(ch_gsm->state));
			} else {
				// resume
				ast_cli(a->fd, "resume...\n");
				if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SUSPEND) {
					pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1");
					pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
					ch_gsm->state = PG_CHANNEL_GSM_STATE_CHECK_PIN;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
					ch_gsm->flags.resume_now = 1;
					// start simpoll timer
					x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
				} else
					ast_cli(a->fd, " switching to run state from \"%s\" not allowed\n", pg_cahnnel_gsm_state_to_string(ch_gsm->state));
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}

	if (!total)
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_suspend_resume()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_action_param()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_action_param(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int pos;
	int tmpi;
	float tmpf;
	struct pg_channel_rtp *rtp;
	struct pg_vinetic *vin;
	char *gline;
	char *gargv[AST_MAX_ARGS];
	int gargc;
	int operation;
	int param;
	size_t total;
	struct pg_channel_gsm *ch_gsm;
	struct pg_trunk_gsm *tr_gsm;
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold;
	char *cp;
	unsigned char tmpchr;
	char tmpbuf[256];
	char imei_buf[16];
	int imei_len;
	char imei_check_digit;
	struct x_timer timer;
	int res;
	struct timeval tv;
#ifdef HAVE_ASTERISK_SELECT_H
	ast_fdset fds;
#else
	fd_set fds;
#endif
	struct termios old_termios;
	struct termios raw_termios;
	int old_state;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			ast_cli(a->fd, "is ch_act_prm subhandler -- CLI_INIT unsupported in this context\n");
			return CLI_FAILURE;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
		  	gline = ast_strdupa(a->line);
			if (!(pg_cli_generating_prepare(gline, &gargc, gargv))) {
				if (a->pos == 5) {
					return pg_cli_generate_complete_channel_gsm_action_params(a->word, a->n, gargv[4]);
				} else if ((a->pos == 6) && (!strcmp(gargv[4], "set"))) {
					if (!strcmp(gargv[5], "alias"))
						return pg_cli_generate_complete_channel_gsm_name(a->word, a->n, 0);
					else if (!strcmp(gargv[5], "progress"))
						return pg_cli_generate_complete_channel_gsm_progress(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "incoming"))
						return pg_cli_generate_complete_channel_gsm_incoming(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "outgoing"))
						return pg_cli_generate_complete_channel_gsm_outgoing(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "context"))
						return pg_cli_generate_complete_context(a->word, a->n);
					else if (!strcmp(gargv[5], "callwait"))
						return pg_cli_generate_complete_channel_gsm_callwait(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "clir"))
						return pg_cli_generate_complete_channel_gsm_clir(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "sms.ntf.ctx"))
						return pg_cli_generate_complete_context(a->word, a->n);
					else if (!strcmp(gargv[5], "trunk"))
						return pg_cli_generate_complete_trunk_gsm_channel_set(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "ali.nelec"))
						return pg_cli_generate_complete_channel_gsm_ali_nelec(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "ali.nelec.tm"))
						return pg_cli_generate_complete_channel_gsm_ali_nelec_tm(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "ali.nelec.oldc"))
						return pg_cli_generate_complete_channel_gsm_ali_nelec_oldc(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "ali.nelec.as"))
						return pg_cli_generate_complete_channel_gsm_ali_nelec_as(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "ali.nelec.nlp"))
						return pg_cli_generate_complete_channel_gsm_ali_nelec_nlp(a->word, a->n, gargv[3]);
					else if (!strcmp(gargv[5], "ali.nelec.nlpm"))
						return pg_cli_generate_complete_channel_gsm_ali_nelec_nlpm(a->word, a->n, gargv[3]);
				} else if ((a->pos == 6) && (!strcmp(gargv[4], "delete"))) {
					if (!strcmp(gargv[5], "trunk"))
						return pg_cli_generate_complete_trunk_gsm_channel_delete(a->word, a->n, gargv[3]);
				}
			}
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 5) {
		snprintf(pg_cli_channel_gsm_actions_usage, sizeof(pg_cli_channel_gsm_actions_usage),
			"Usage: polygator channel gsm <channel> <oper>\n");
		return CLI_SHOWUSAGE;
	}

	// get params operation
	operation = pg_get_channel_gsm_param_operation(a->argv[4]);
	if (operation == PG_CHANNEL_GSM_PARAM_OP_UNKNOWN) {
		ast_cli(a->fd, "  Unknown operation \"%s\"\n", a->argv[4]);
		return CLI_SUCCESS;
	}

	if (((operation == PG_CHANNEL_GSM_PARAM_OP_GET) || (operation == PG_CHANNEL_GSM_PARAM_OP_QUERY)) && (a->argc < 6)) {
		snprintf(pg_cli_channel_gsm_actions_usage, sizeof(pg_cli_channel_gsm_actions_usage),
			"Usage: polygator channel gsm <channel> %s <param>\n", a->argv[4]);
		return CLI_SHOWUSAGE;
	}

	if (((operation == PG_CHANNEL_GSM_PARAM_OP_SET) || (operation == PG_CHANNEL_GSM_PARAM_OP_DELETE)) && (a->argc < 7)) {
		snprintf(pg_cli_channel_gsm_actions_usage, sizeof(pg_cli_channel_gsm_actions_usage),
			"Usage: polygator channel gsm <channel> %s <param> <value>\n", a->argv[4]);
		return CLI_SHOWUSAGE;
	}

	// get param id
	param = pg_get_channel_gsm_param(a->argv[5]);
	if (param == PG_CHANNEL_GSM_PARAM_UNKNOWN) {
		ast_cli(a->fd, "  unknown parameter \"%s\"\n", a->argv[5]);
		return CLI_SUCCESS;
	}

	total = 0;
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		if (!strcmp(a->argv[3], "all") || !strcmp(a->argv[3], ch_gsm->alias)) {
			total++;
			ast_cli(a->fd, "  GSM channel=\"%s\": ", ch_gsm->alias);
			switch (operation)
			{
				//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				case PG_CHANNEL_GSM_PARAM_OP_GET:
					ast_cli(a->fd, "get(%s)", a->argv[5]);
					switch (param)
					{
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_ALIAS:
							ast_cli(a->fd, " -> %s\n", ch_gsm->alias?ch_gsm->alias:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PIN:
							ast_cli(a->fd, " -> %s\n", ch_gsm->pin?ch_gsm->pin:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PUK:
							ast_cli(a->fd, " -> %s\n", ch_gsm->puk?ch_gsm->puk:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NUMBER:
							if (ch_gsm->subscriber_number.length)
								ast_cli(a->fd, " -> %s\n", address_show(tmpbuf, &ch_gsm->subscriber_number, 1));
							else
								ast_cli(a->fd, " -> unknown\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_IMEI:
							if (strlen(ch_gsm->imei))
								ast_cli(a->fd, " -> %s\n", ch_gsm->imei);
							else
								ast_cli(a->fd, " -> unknown\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PROGRESS:
							ast_cli(a->fd, " -> %s\n", pg_call_gsm_progress_to_string(ch_gsm->config.progress));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_INCOMING:
							ast_cli(a->fd, " -> %s\n", pg_call_gsm_incoming_type_to_string(ch_gsm->config.incoming_type));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_INCOMINGTO:
							ast_cli(a->fd, " -> %s\n", strlen(ch_gsm->config.gsm_call_extension)?ch_gsm->config.gsm_call_extension:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_OUTGOING:
							ast_cli(a->fd, " -> %s\n", pg_call_gsm_outgoing_to_string(ch_gsm->config.outgoing_type));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CONTEXT:
							ast_cli(a->fd, " -> %s\n", strlen(ch_gsm->config.gsm_call_context)?ch_gsm->config.gsm_call_context:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_REGATTEMPT:
							if (ch_gsm->config.reg_try_count < 0)
								ast_cli(a->fd, " -> forever\n");
							else
								ast_cli(a->fd, " -> %d\n", ch_gsm->config.reg_try_count);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CALLWAIT:
							ast_cli(a->fd, " -> %s\n", pg_callwait_state_to_string(ch_gsm->config.callwait));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CLIR:
							ast_cli(a->fd, " -> %s\n", pg_clir_state_to_string(ch_gsm->config.clir));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_DCRTTL:
							ast_cli(a->fd, " -> %ld\n", (long int)ch_gsm->config.dcrttl);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_LANGUAGE:
							ast_cli(a->fd, " -> %s\n", strlen(ch_gsm->config.language)?ch_gsm->config.language:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_MOHINTERPRET:
							ast_cli(a->fd, " -> %s\n", strlen(ch_gsm->config.mohinterpret)?ch_gsm->config.mohinterpret:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_BAUDRATE:
							ast_cli(a->fd, " -> %u\n", ch_gsm->config.baudrate);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAININ:
							ast_cli(a->fd, " -> %d\n", ch_gsm->config.gainin);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINOUT:
							ast_cli(a->fd, " -> %d\n", ch_gsm->config.gainout);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAIN1:
							ast_cli(a->fd, " -> %2.2f dB (0x%02x)\n", vin_gainem_to_gaindb(ch_gsm->config.gain1), ch_gsm->config.gain1);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++sms.ntf.exten
						case PG_CHANNEL_GSM_PARAM_GAIN2:
							ast_cli(a->fd, " -> %2.2f dB (0x%02x)\n", vin_gainem_to_gaindb(ch_gsm->config.gain2), ch_gsm->config.gain2);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINX:
							ast_cli(a->fd, " -> %2.2f dB (0x%02x)\n", vin_gainem_to_gaindb(ch_gsm->config.gainx), ch_gsm->config.gainx);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINR:
							ast_cli(a->fd, " -> %2.2f dB (0x%02x)\n", vin_gainem_to_gaindb(ch_gsm->config.gainr), ch_gsm->config.gainr);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_MODULETYPE:
							ast_cli(a->fd, " -> %s\n", pg_gsm_module_type_to_string(ch_gsm->gsm_module_type));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSSENDINTERVAL:
							ast_cli(a->fd, " -> %ld\n", ch_gsm->config.sms_send_interval.tv_sec);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSSENDATTEMPT:
							ast_cli(a->fd, " -> %d\n", ch_gsm->config.sms_send_attempt);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSMAXPARTCOUNT:
							ast_cli(a->fd, " -> %d\n", ch_gsm->config.sms_max_part);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_ENABLE:
							ast_cli(a->fd, " -> %s\n", ch_gsm->config.sms_notify_enable?"yes":"no");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_CONTEXT:
							ast_cli(a->fd, " -> %s\n", strlen(ch_gsm->config.sms_notify_context)?ch_gsm->config.sms_notify_context:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_EXTENSION:
							ast_cli(a->fd, " -> %s\n", strlen(ch_gsm->config.sms_notify_extension)?ch_gsm->config.sms_notify_extension:"unknown");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_TRUNK:
							total = 0;
							AST_LIST_TRAVERSE(&ch_gsm->trunk_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_channel_list_entry)
								ast_cli(a->fd, "\n\t%lu: %s", (unsigned long int)total++, ch_gsm_fold->name);
							if (total) ast_cli(a->fd, "\n");
							else ast_cli(a->fd, " -> <empty list>\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_TRUNKONLY:
							ast_cli(a->fd, " -> %s\n", ch_gsm->config.trunkonly?"yes":"no");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CONFALLOW:
							ast_cli(a->fd, " -> %s\n", ch_gsm->config.conference_allowed?"yes":"no");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC:
							ast_cli(a->fd, " -> %s\n", (ch_gsm->config.ali_nelec == VIN_DIS)?"inactive":"active");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_TM:
							ast_cli(a->fd, " -> %s\n", (ch_gsm->config.ali_nelec_tm == VIN_DTM_ON)?"on":"off");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_OLDC:
							ast_cli(a->fd, " -> %s\n", (ch_gsm->config.ali_nelec_oldc == VIN_OLDC_ZERO)?"zero":"old");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_AS:
							ast_cli(a->fd, " -> %s\n", (ch_gsm->config.ali_nelec_as == VIN_AS_RUN)?"run":"stop");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_NLP:
							ast_cli(a->fd, " -> %s\n", (ch_gsm->config.ali_nelec_nlp == VIN_OFF)?"off":"on");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_NLPM:
							ast_cli(a->fd, " -> %s\n", pg_vinetic_ali_nelec_nlpm_to_string(ch_gsm->config.ali_nelec_nlpm));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						default:
							ast_cli(a->fd, " - unknown parameter\n");
							break;
					}
					break;
				//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				case PG_CHANNEL_GSM_PARAM_OP_SET:
					ast_cli(a->fd, "set(%s) <- %s", a->argv[5], a->argv[6]);
					switch (param)
					{
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_ALIAS:
							if (pg_get_channel_gsm_by_name(a->argv[6]))
								ast_cli(a->fd, " - already used on other channel\n");
							else {
								ast_free(ch_gsm->alias);
								ch_gsm->alias = ast_strdup(a->argv[6]);
								ast_cli(a->fd, " - ok\n");
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PIN:
							if ((strlen(a->argv[6]) >= 4) && (strlen(a->argv[6]) <= 8) && (is_str_digit(a->argv[6]))) {
								if (ch_gsm->iccid && strlen(ch_gsm->iccid))
									pg_set_pin_by_iccid(ch_gsm->iccid, a->argv[6], &ch_gsm->lock);
								if (ch_gsm->pin) ast_free(ch_gsm->pin);
								ch_gsm->pin = ast_strdup(a->argv[6]);
								if (ch_gsm->flags.pin_required)
									pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\"", ch_gsm->pin);
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - PIN must be set of 4-8 digit\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PUK:
							if ((strlen(a->argv[6]) >= 4) && (strlen(a->argv[6]) <= 8) && (is_str_digit(a->argv[6]))) {
								if (ch_gsm->iccid && strlen(ch_gsm->iccid))
									pg_set_puk_by_iccid(ch_gsm->iccid, a->argv[6], &ch_gsm->lock);
								if (ch_gsm->puk) ast_free(ch_gsm->puk);
								ch_gsm->puk = ast_strdup(a->argv[6]);
								if (!ch_gsm->pin) ch_gsm->pin = pg_get_pin_by_iccid(ch_gsm->iccid, &ch_gsm->lock);
								if (!ch_gsm->pin) ch_gsm->pin = ast_strdup("0000");
								if (ch_gsm->flags.puk_required)
									pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "\"%s\",\"%s\"", ch_gsm->puk, ch_gsm->pin);
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - PUK must be set of 4-8 digit\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_IMEI:
							// check IMEI for valid length
							imei_len = strlen(a->argv[6]);
							if ((imei_len != 14) && (imei_len != 15)) {
								ast_cli(a->fd, " - IMEI=%s has wrong length=%d\n", a->argv[6], imei_len);
								break;
							}
							// copy IMEI
							memset(imei_buf, 0, 16);
							memcpy(imei_buf, a->argv[6], (imei_len < 15)?(imei_len):(15));
							// calc IMEI check digit
							if ((tmpi = imei_calc_check_digit(imei_buf)) < 0) {
								if(tmpi == -2)
									ast_cli(a->fd, " - IMEI is too short\n");
								if(tmpi == -3)
									ast_cli(a->fd, " - IMEI has illegal character\n");
								else
									ast_cli(a->fd, " - can't calc IMEI check digit\n");
								break;
							}
							imei_check_digit = (char)tmpi;
							if (imei_len == 15) {
								if (imei_check_digit != imei_buf[14]) {
									ast_cli(a->fd, " - IMEI=%s has wrong check digit \"%c\" must be \"%c\"\n", a->argv[6], imei_buf[14], imei_check_digit);
									break;
								}
							}

							ast_cli(a->fd, " - write IMEI=%.*s(%c)...\n", 14, a->argv[6], imei_check_digit);
							imei_buf[14] = imei_check_digit;

							// check channel state
							if ((ch_gsm->state != PG_CHANNEL_GSM_STATE_RUN) &&
									(ch_gsm->state != PG_CHANNEL_GSM_STATE_WAIT_SUSPEND) &&
										(ch_gsm->state != PG_CHANNEL_GSM_STATE_SUSPEND)) {
								ast_cli(a->fd, "  GSM channel=\"%s\": unaplicable state \"%s\" - must be \"run\", \"wait for suspend\" or \"suspend\"\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								break;
							}
							// store initial state
							old_state = ch_gsm->state;
							// backup termios
							if (tcgetattr(ch_gsm->tty_fd, &old_termios)) {
								ast_cli(a->fd, "  GSM channel=\"%s\": tcgetattr() error: %s\n", ch_gsm->alias, strerror(errno));
								break;
							}
							if (tcgetattr(ch_gsm->tty_fd, &raw_termios)) {
								ast_cli(a->fd, "  GSM channel=\"%s\": tcgetattr() error: %s\n", ch_gsm->alias, strerror(errno));
								break;
							}
							//  check for IMEI is the same
							if (!strcmp(ch_gsm->imei, imei_buf)) {
								ast_cli(a->fd, "  GSM channel=\"%s\": this IMEI value already set\n", ch_gsm->alias);
								break;
							}
							ast_cli(a->fd, "  GSM channel=\"%s\": check for suspend...", ch_gsm->alias);
							// action for RUN state
							if (ch_gsm->state == PG_CHANNEL_GSM_STATE_RUN) {
								// check channel for active calls
								if (pg_channel_gsm_get_calls_count(ch_gsm)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": has active call - try later\n", ch_gsm->alias);
									break;
								}
								// stop all timers
								memset(&ch_gsm->timers, 0, sizeof(struct pg_channel_gsm_timers));
								pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "0");
								ch_gsm->state = PG_CHANNEL_GSM_STATE_WAIT_SUSPEND;
								ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								x_timer_set(ch_gsm->timers.waitsuspend, waitsuspend_timeout);
							}
							// action for WAIT_SUSPEND state
							if (ch_gsm->state == PG_CHANNEL_GSM_STATE_WAIT_SUSPEND) {
								tmpi = 30;
								while (tmpi-- > 0)
								{
									if (ch_gsm->state == PG_CHANNEL_GSM_STATE_SUSPEND)
										break;
									// waiting
									ast_mutex_unlock(&ch_gsm->lock);
									sleep(1);
									ast_mutex_lock(&ch_gsm->lock);
								}
							}
							// check for SUSPEND state
							if (ch_gsm->state != PG_CHANNEL_GSM_STATE_SUSPEND) {
								ast_cli(a->fd, "failed\n");
								break;
							}
							ast_cli(a->fd, "succeeded\n");

							// hardware depend procedure
							if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM300) {
								// SIM300
								// set raw termios
								cfmakeraw(&raw_termios);
								if (tcsetattr(ch_gsm->tty_fd, TCSANOW, &raw_termios)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": tcsetattr() error: %s\n", ch_gsm->alias, strerror(errno));
									goto pg_channel_gsm_imei_set_end;
								}
								// select AUXILARY channel of SERIAL port
								ch_gsm->flags.main_tty = 0;
								if (pg_channel_gsm_serial_set(ch_gsm, 1) < 0) {
									ast_cli(a->fd, "  GSM channel=\"%s\": can't switch serial port to auxilary channel: %s\n", ch_gsm->alias, strerror(errno));
									goto pg_channel_gsm_imei_set_end;
								}
								if (tcflush(ch_gsm->tty_fd, TCIOFLUSH) < 0) {
									ast_cli(a->fd, "  GSM channel=\"%s\": can't flush tty device: %s\n", ch_gsm->alias, strerror(errno));
									goto pg_channel_gsm_imei_set_end;
								}
								// imei data1
								sim300_build_imei_data1(tmpbuf, &tmpi);
								// write imei data1
								pos = 0;
								x_timer_set_second(timer, 10);
								while (is_x_timer_active(timer))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 0;
									FD_ZERO(&fds);
									FD_SET(ch_gsm->tty_fd, &fds);
									ast_mutex_unlock(&ch_gsm->lock);
									res = ast_select(ch_gsm->tty_fd + 1, NULL, &fds, NULL, &tv);
									ast_mutex_lock(&ch_gsm->lock);
									// check select result code
									if (res > 0) {
										if (FD_ISSET(ch_gsm->tty_fd, &fds)) {
											if ((res = write(ch_gsm->tty_fd, &tmpbuf[pos], tmpi)) < 0) {
												ast_cli(a->fd, "  GSM channel=\"%s\": write(imei data1): %s\n", ch_gsm->alias, strerror(errno));
												goto pg_channel_gsm_imei_set_end;
											} else {
												tmpi -= res;
												pos += res;
												if (tmpi <= 0) break;
											}
										}
									} else if (res < 0) {
										ast_cli(a->fd, "  GSM channel=\"%s\": select(write imei data1): %s\n", ch_gsm->alias, strerror(errno));
										goto pg_channel_gsm_imei_set_end;
									}
								}
								if (is_x_timer_fired(timer)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": write imei data1 - time is out\n", ch_gsm->alias);
									goto pg_channel_gsm_imei_set_end;
								}
								// wait for 0x06 from GSM module
								x_timer_set_second(timer, 10);
								while (is_x_timer_active(timer))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 0;
									FD_ZERO(&fds);
									FD_SET(ch_gsm->tty_fd, &fds);
									ast_mutex_unlock(&ch_gsm->lock);
									res = ast_select(ch_gsm->tty_fd + 1, &fds, NULL, NULL, &tv);
									ast_mutex_lock(&ch_gsm->lock);
									// check select result code
									if (res > 0) {
										if (FD_ISSET(ch_gsm->tty_fd, &fds)) {
											if ((res = read(ch_gsm->tty_fd, &tmpchr, 1)) < 0) {
												if (errno != EAGAIN) {
													ast_cli(a->fd, "  GSM channel=\"%s\": read(wait for 0x06 imei data1): %s\n", ch_gsm->alias, strerror(errno));
													goto pg_channel_gsm_imei_set_end;
												}
											} else {
												if (tmpchr == 0x06) break;
											}
										}
									} else if (res < 0) {
										ast_cli(a->fd, "  GSM channel=\"%s\": select(wait for 0x06 imei data1): %s\n", ch_gsm->alias, strerror(errno));
										goto pg_channel_gsm_imei_set_end;
									}
								}
								if (is_x_timer_fired(timer)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": wait for 0x06 imei data1 - time is out\n", ch_gsm->alias);
									goto pg_channel_gsm_imei_set_end;
								}
								// imei data2
								sim300_build_imei_data2(tmpbuf, &tmpi);
								// write imei data2
								pos = 0;
								x_timer_set_second(timer, 10);
								while (is_x_timer_active(timer))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 0;
									FD_ZERO(&fds);
									FD_SET(ch_gsm->tty_fd, &fds);
									ast_mutex_unlock(&ch_gsm->lock);
									res = ast_select(ch_gsm->tty_fd + 1, NULL, &fds, NULL, &tv);
									ast_mutex_lock(&ch_gsm->lock);
									// check select result code
									if (res > 0) {
										if (FD_ISSET(ch_gsm->tty_fd, &fds)) {
											if ((res = write(ch_gsm->tty_fd, &tmpbuf[pos], tmpi)) < 0) {
												ast_cli(a->fd, "  GSM channel=\"%s\": write(imei data2): %s\n", ch_gsm->alias, strerror(errno));
												goto pg_channel_gsm_imei_set_end;
											} else {
												tmpi -= res;
												pos += res;
												if (tmpi <= 0) break;
											}
										}
									} else if (res < 0) {
										ast_cli(a->fd, "  GSM channel=\"%s\": select(write imei data2): %s\n", ch_gsm->alias, strerror(errno));
										goto pg_channel_gsm_imei_set_end;
									}
								}
								if (is_x_timer_fired(timer)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": write imei data2 - time is out\n", ch_gsm->alias);
									goto pg_channel_gsm_imei_set_end;
								}
								// wait for 0x06 from GSM module
								x_timer_set_second(timer, 10);
								while (is_x_timer_active(timer))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 0;
									FD_ZERO(&fds);
									FD_SET(ch_gsm->tty_fd, &fds);
									ast_mutex_unlock(&ch_gsm->lock);
									res = ast_select(ch_gsm->tty_fd + 1, &fds, NULL, NULL, &tv);
									ast_mutex_lock(&ch_gsm->lock);
									// check select result code
									if (res > 0) {
										if (FD_ISSET(ch_gsm->tty_fd, &fds)) {
											if ((res = read(ch_gsm->tty_fd, &tmpchr, 1)) < 0) {
												if (errno != EAGAIN) {
													ast_cli(a->fd, "  GSM channel=\"%s\": read(wait for 0x06 imei data2): %s\n", ch_gsm->alias, strerror(errno));
													goto pg_channel_gsm_imei_set_end;
												}
											} else {
												if (tmpchr == 0x06) break;
											}
										}
									} else if (res < 0) {
										ast_cli(a->fd, "  GSM channel=\"%s\": select(wait for 0x06 imei data2): %s\n", ch_gsm->alias, strerror(errno));
										goto pg_channel_gsm_imei_set_end;
									}
								}
								if (is_x_timer_fired(timer)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": wait for 0x06 imei data2 - time is out\n", ch_gsm->alias);
									goto pg_channel_gsm_imei_set_end;
								}
								// write 0x06
								tmpchr = 0x06;
								x_timer_set_second(timer, 10);
								while (is_x_timer_active(timer))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 0;
									FD_ZERO(&fds);
									FD_SET(ch_gsm->tty_fd, &fds);
									ast_mutex_unlock(&ch_gsm->lock);
									res = ast_select(ch_gsm->tty_fd + 1, NULL, &fds, NULL, &tv);
									ast_mutex_lock(&ch_gsm->lock);
									// check select result code
									if (res > 0) {
										if (FD_ISSET(ch_gsm->tty_fd, &fds)) {
											if ((res = write(ch_gsm->tty_fd, &tmpchr, 1)) < 0) {
												ast_cli(a->fd, "  GSM channel=\"%s\": write(0x06 a): %s\n", ch_gsm->alias, strerror(errno));
												goto pg_channel_gsm_imei_set_end;
											} else
												break;
										}
									} else if (res < 0) {
										ast_cli(a->fd, "  GSM channel=\"%s\": select(write 0x06 a): %s\n", ch_gsm->alias, strerror(errno));
										goto pg_channel_gsm_imei_set_end;
									}
								}
								if (is_x_timer_fired(timer)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": write 0x06 a - time is out\n", ch_gsm->alias);
									goto pg_channel_gsm_imei_set_end;
								}
								// build imei data3
								sim300_build_imei_data3(imei_buf, imei_check_digit, tmpbuf, &tmpi);
								// write imei data3
								pos = 0;
								x_timer_set_second(timer, 10);
								while (is_x_timer_active(timer))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 0;
									FD_ZERO(&fds);
									FD_SET(ch_gsm->tty_fd, &fds);
									ast_mutex_unlock(&ch_gsm->lock);
									res = ast_select(ch_gsm->tty_fd + 1, NULL, &fds, NULL, &tv);
									ast_mutex_lock(&ch_gsm->lock);
									// check select result code
									if (res > 0) {
										if (FD_ISSET(ch_gsm->tty_fd, &fds)) {
											if ((res = write(ch_gsm->tty_fd, &tmpbuf[pos], tmpi)) < 0) {
												ast_cli(a->fd, "  GSM channel=\"%s\": write(imei data3): %s\n", ch_gsm->alias, strerror(errno));
												goto pg_channel_gsm_imei_set_end;
											} else {
												tmpi -= res;
												pos += res;
												if (tmpi <= 0) break;
											}
										}
									} else if (res < 0) {
										ast_cli(a->fd, "  GSM channel=\"%s\": select(write imei data3): %s\n", ch_gsm->alias, strerror(errno));
										goto pg_channel_gsm_imei_set_end;
									}
								}
								if (is_x_timer_fired(timer)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": write imei data3 - time is out\n", ch_gsm->alias);
									goto pg_channel_gsm_imei_set_end;
								}
								// wait for 0x06 from GSM module
								x_timer_set_second(timer, 10);
								while (is_x_timer_active(timer))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 0;
									FD_ZERO(&fds);
									FD_SET(ch_gsm->tty_fd, &fds);
									ast_mutex_unlock(&ch_gsm->lock);
									res = ast_select(ch_gsm->tty_fd + 1, &fds, NULL, NULL, &tv);
									ast_mutex_lock(&ch_gsm->lock);
									// check select result code
									if (res > 0) {
										if (FD_ISSET(ch_gsm->tty_fd, &fds)) {
											if ((res = read(ch_gsm->tty_fd, &tmpchr, 1)) < 0) {
												if (errno != EAGAIN) {
													ast_cli(a->fd, "  GSM channel=\"%s\": read(wait for 0x06 imei data3): %s\n", ch_gsm->alias, strerror(errno));
													goto pg_channel_gsm_imei_set_end;
												}
											} else {
												if (tmpchr == 0x06) break;
											}
										}
									} else if (res < 0) {
										ast_cli(a->fd, "  GSM channel=\"%s\": select(wait for 0x06 imei data3): %s\n", ch_gsm->alias, strerror(errno));
										goto pg_channel_gsm_imei_set_end;
									}
								}
								if (is_x_timer_fired(timer)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": wait for 0x06 imei data3 - time is out\n", ch_gsm->alias);
									goto pg_channel_gsm_imei_set_end;
								}
								// write 0x06
								tmpchr = 0x06;
								x_timer_set_second(timer, 10);
								while (is_x_timer_active(timer))
								{
									tv.tv_sec = 1;
									tv.tv_usec = 0;
									FD_ZERO(&fds);
									FD_SET(ch_gsm->tty_fd, &fds);
									ast_mutex_unlock(&ch_gsm->lock);
									res = ast_select(ch_gsm->tty_fd + 1, NULL, &fds, NULL, &tv);
									ast_mutex_lock(&ch_gsm->lock);
									// check select result code
									if (res > 0) {
										if (FD_ISSET(ch_gsm->tty_fd, &fds)) {
											if ((res = write(ch_gsm->tty_fd, &tmpchr, 1)) < 0) {
												ast_cli(a->fd, "  GSM channel=\"%s\": write(0x06 b): %s\n", ch_gsm->alias, strerror(errno));
												goto pg_channel_gsm_imei_set_end;
											} else
												break;
										}
									} else if (res < 0) {
										ast_cli(a->fd, "  GSM channel=\"%s\": select(write 0x06 b): %s\n", ch_gsm->alias, strerror(errno));
										goto pg_channel_gsm_imei_set_end;
									}
								}
								if (is_x_timer_fired(timer)) {
									ast_cli(a->fd, "  GSM channel=\"%s\": write 0x06 b - time is out\n", ch_gsm->alias);
									goto pg_channel_gsm_imei_set_end;
								}
							} else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_M10) {
								// M10
								pg_atcommand_queue_append(ch_gsm, AT_M10_EGMR, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1,7,\"%s\"", imei_buf);
							} else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM900) {
								// SIM900
								ast_cli(a->fd, "  GSM channel=\"%s\": IMEI change command for SIM900 under working\n", ch_gsm->alias);
								goto pg_channel_gsm_imei_set_end;
							} else if (ch_gsm->gsm_module_type == POLYGATOR_MODULE_TYPE_SIM5215) {
								// SIM5215
								ast_cli(a->fd, "  GSM channel=\"%s\": IMEI change command for SIM5215 under working\n", ch_gsm->alias);
								goto pg_channel_gsm_imei_set_end;
							} else {
								// UNKNOWN
								ast_cli(a->fd, "  GSM channel=\"%s\": unknown module type\n", ch_gsm->alias);
								goto pg_channel_gsm_imei_set_end;
							}
							ch_gsm->imei[0] = '\0';
							ast_cli(a->fd, "  GSM channel=\"%s\": IMEI write is completed\n", ch_gsm->alias);
pg_channel_gsm_imei_set_end:
							// restore old termios
							if (tcsetattr(ch_gsm->tty_fd, TCSANOW, &old_termios))
								ast_cli(a->fd, "  GSM channel=\"%s\": tcsetattr() error: %s\n", ch_gsm->alias, strerror(errno));
							// select MAIN channel of SERIAL port
							ch_gsm->flags.main_tty = 1;
							if (pg_channel_gsm_serial_set(ch_gsm, 0) < 0)
								ast_cli(a->fd, "  GSM channel=\"%s\": can't switch serial port to main channel: %s\n", ch_gsm->alias, strerror(errno));
							if (tcflush(ch_gsm->tty_fd, TCIOFLUSH) < 0)
								ast_cli(a->fd, "  GSM channel=\"%s\": can't flush tty device: %s\n", ch_gsm->alias, strerror(errno));
							// restore previous state
							if (old_state == PG_CHANNEL_GSM_STATE_RUN) {
								pg_atcommand_queue_append(ch_gsm, AT_CFUN, AT_OPER_WRITE, 0, pg_at_response_timeout, 0, "1");
								pg_atcommand_queue_append(ch_gsm, AT_CPIN, AT_OPER_READ, 0, pg_at_response_timeout, 0, NULL);
								ch_gsm->state = PG_CHANNEL_GSM_STATE_CHECK_PIN;
								ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
								ch_gsm->flags.resume_now = 1;
								// start simpoll timer
								x_timer_set(ch_gsm->timers.simpoll, simpoll_timeout);
							}
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NUMBER:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PROGRESS:
							tmpi = pg_get_gsm_call_progress(a->argv[6]);
							if (tmpi != PG_GSM_CALL_PROGRESS_TYPE_UNKNOWN) {
								ch_gsm->config.progress = tmpi;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - unknown call progress type\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_INCOMING:
							tmpi = pg_get_call_gsm_incoming_type(a->argv[6]);
							if (tmpi != PG_CALL_GSM_INCOMING_TYPE_UNKNOWN) {
								ch_gsm->config.incoming_type = tmpi;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - unknown call incoming type\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_INCOMINGTO:
							ast_copy_string(ch_gsm->config.gsm_call_extension, a->argv[6], sizeof(ch_gsm->config.gsm_call_extension));
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_OUTGOING:
							tmpi = pg_get_gsm_call_outgoing(a->argv[6]);
							if (tmpi != PG_CALL_GSM_OUTGOING_TYPE_UNKNOWN) {
								ch_gsm->config.outgoing_type = tmpi;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - unknown call outgoing type\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CONTEXT:
							ast_copy_string(ch_gsm->config.gsm_call_context, a->argv[6], sizeof(ch_gsm->config.gsm_call_context));
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_REGATTEMPT:
							if (is_str_digit((a->argv[6])) || ((*a->argv[6] == '-') && (is_str_digit(((a->argv[6])+1))))) {
								ch_gsm->reg_try_count = ch_gsm->config.reg_try_count = atoi(a->argv[6]);
								ch_gsm->flags.sim_change = 0;
								ast_cli(a->fd, " -> ok\n");
							} else if (!strcasecmp(a->argv[6], "forever")) {
								ch_gsm->reg_try_count = ch_gsm->config.reg_try_count = -1;
								ch_gsm->flags.sim_change = 0;
								ast_cli(a->fd, " -> ok\n");
							} else
								ast_cli(a->fd, " -> %s unknown\n", a->argv[6]);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CALLWAIT:
							tmpi = pg_get_callwait_state(a->argv[6]);
							if ((tmpi != PG_CALLWAIT_STATE_UNKNOWN) && (tmpi != PG_CALLWAIT_STATE_QUERY)) {
								if ((tmpi == PG_CALLWAIT_STATE_ENABLE) && (ch_gsm->config.incoming_type != PG_CALL_GSM_INCOMING_TYPE_SPEC)) {
									ast_cli(a->fd, " - fail - callwait valid for call incoming type spec only\n");
								} else {
									ch_gsm->config.callwait = tmpi;
									ast_cli(a->fd, " - ok\n");
								}
							} else
								ast_cli(a->fd, " - unknown callwait state\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CLIR:
							tmpi = pg_get_clir_state(a->argv[6]);
							if ((tmpi != PG_CLIR_STATE_UNKNOWN) && (tmpi != PG_CLIR_STATE_QUERY)) {
								ch_gsm->config.clir = tmpi;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - unknown clir state\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_DCRTTL:
							if (is_str_digit(a->argv[6])) {
								tmpi = atoi(a->argv[6]);
								if (tmpi < 60) {
									tmpi = 60;
									ast_cli(a->fd, " - value %d less than 60 - set dynttl to 60\n", tmpi);
								}
								ch_gsm->config.dcrttl = tmpi;
								ast_cli(a->fd, " - ok\n");
							} else 
								ast_cli(a->fd, " - bad param - must be digit\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_LANGUAGE:
							ast_copy_string(ch_gsm->config.language, a->argv[6], sizeof(ch_gsm->config.language));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_MOHINTERPRET:
							ast_copy_string(ch_gsm->config.mohinterpret, a->argv[6], sizeof(ch_gsm->config.mohinterpret));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_BAUDRATE:
							if (is_str_digit(a->argv[6])) {
								ch_gsm->config.baudrate = atoi(a->argv[6]);
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - bad param - must be digit\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAININ:
							if (is_str_digit(a->argv[6])) {
								tmpi = atoi(a->argv[6]);
								if (tmpi < 0) {
									tmpi = 0;
									ast_cli(a->fd, " - value %d less than 0 - set gainin to 0\n", tmpi);
								}
								if (tmpi > 100) {
									tmpi = 100;
									ast_cli(a->fd, " - value %d greater than 100 - set gainin to 100\n", tmpi);
								}
								ch_gsm->config.gainin = tmpi;
								ch_gsm->gainin = 0;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - bad param - must be digit in range 0-100\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINOUT:
							if (is_str_digit(a->argv[6])) {
								tmpi = atoi(a->argv[6]);
								if (tmpi < 0) {
									tmpi = 0;
									ast_cli(a->fd, " - value %d less than 0 - set gainout to 0\n", tmpi);
								}
								if (tmpi > 15) {
									tmpi = 15;
									ast_cli(a->fd, " - value %d greater than 15 - set gainin to 15\n", tmpi);
								}
								ch_gsm->config.gainout = tmpi;
								ch_gsm->gainout = 0;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - bad param - must be digit in range 0-15\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAIN1:
							if (sscanf(a->argv[6], "%f", &tmpf) == 1) {
								if (tmpf > VIN_GAINDB_MAX) tmpf = VIN_GAINDB_MAX;
								if (tmpf < VIN_GAINDB_MIN) tmpf = VIN_GAINDB_MIN;
								ch_gsm->config.gain1 = vin_gaindb_to_gainem(tmpf);
								if ((rtp = ch_gsm->channel_rtp)) {
									vin = rtp->vinetic;
									ast_mutex_lock(&vin->lock);
									// unblock vinetic
									if (vin_reset_status(&vin->context) < 0) {
										ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
										ast_mutex_unlock(&vin->lock);
										break;
									}
									// diasble coder channel speech compression
									if (vin_coder_channel_disable(vin->context, rtp->position_on_vinetic) < 0) {
										ast_cli(a->fd, "vinetic=\"%s\": vin_coder_channel_disable(): %s\n", vin->name, vin_error_str(&vin->context));
										ast_mutex_unlock(&vin->lock);
										break;
									}
									vin_coder_channel_set_gain1(vin->context, rtp->position_on_vinetic, ch_gsm->config.gain1);
									if (vin_coder_channel_enable(vin->context, rtp->position_on_vinetic) < 0) {
										ast_cli(a->fd, "vinetic=\"%s\": vin_coder_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
										ast_mutex_unlock(&vin->lock);
										break;
									}
									ast_mutex_unlock(&vin->lock);
								}
								ast_cli(a->fd, " - 0x%02x - ok\n", ch_gsm->config.gain1);
							} else
								ast_cli(a->fd, " - can't get gain value from \"%s\"\n", a->argv[6]);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAIN2:
							if (sscanf(a->argv[6], "%f", &tmpf) == 1) {
								if (tmpf > VIN_GAINDB_MAX) tmpf = VIN_GAINDB_MAX;
								if (tmpf < VIN_GAINDB_MIN) tmpf = VIN_GAINDB_MIN;
								ch_gsm->config.gain2 = vin_gaindb_to_gainem(tmpf);
								if ((rtp = ch_gsm->channel_rtp)) {
									vin = rtp->vinetic;
									ast_mutex_lock(&vin->lock);
									// unblock vinetic
									if (vin_reset_status(&vin->context) < 0) {
										ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
										ast_mutex_unlock(&vin->lock);
										break;
									}
									// diasble coder channel speech compression
									if (vin_coder_channel_disable(vin->context, rtp->position_on_vinetic) < 0) {
										ast_cli(a->fd, "vinetic=\"%s\": vin_coder_channel_disable(): %s\n", vin->name, vin_error_str(&vin->context));
										ast_mutex_unlock(&vin->lock);
										break;
									}
									vin_coder_channel_set_gain2(vin->context, rtp->position_on_vinetic, ch_gsm->config.gain2);
									if (vin_coder_channel_enable(vin->context, rtp->position_on_vinetic) < 0) {
										ast_cli(a->fd, "vinetic=\"%s\": vin_coder_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
										ast_mutex_unlock(&vin->lock);
										break;
									}
									ast_mutex_unlock(&vin->lock);
								}
								ast_cli(a->fd, " - 0x%02x - ok\n", ch_gsm->config.gain2);
							} else
								ast_cli(a->fd, " - can't get gain value from \"%s\"\n", a->argv[6]);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINX:
							if (sscanf(a->argv[6], "%f", &tmpf) == 1) {
								if (tmpf > VIN_GAINDB_MAX) tmpf = VIN_GAINDB_MAX;
								if (tmpf < VIN_GAINDB_MIN) tmpf = VIN_GAINDB_MIN;
								ch_gsm->config.gainx = vin_gaindb_to_gainem(tmpf);
								if ((rtp = ch_gsm->channel_rtp)) {
									vin = rtp->vinetic;
									ast_mutex_lock(&vin->lock);
									// unblock vinetic
									if (vin_reset_status(&vin->context) < 0) {
										ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
										ast_mutex_unlock(&vin->lock);
										break;
									}
									if ((ch_gsm->vinetic_alm_slot >= 0) &&
										(is_vin_ali_enabled(vin->context)) &&
											(is_vin_ali_channel_enabled(vin->context, ch_gsm->vinetic_alm_slot))) {
										// disable vinetic ALI channel
										if (vin_ali_channel_disable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_channel_disable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										// enable vinetic ALI channel
										vin_ali_channel_set_gainx(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.gainx);
										if (vin_ali_channel_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										// enable vinetic ALI Near End LEC
										if (vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
									}
									ast_mutex_unlock(&vin->lock);
								}
								ast_cli(a->fd, " - 0x%02x - ok\n", ch_gsm->config.gainx);
							} else
								ast_cli(a->fd, " - can't get gain value from \"%s\"\n", a->argv[6]);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINR:
							if (sscanf(a->argv[6], "%f", &tmpf) == 1) {
								if (tmpf > VIN_GAINDB_MAX) tmpf = VIN_GAINDB_MAX;
								if (tmpf < VIN_GAINDB_MIN) tmpf = VIN_GAINDB_MIN;
								ch_gsm->config.gainr = vin_gaindb_to_gainem(tmpf);
								if ((rtp = ch_gsm->channel_rtp)) {
									vin = rtp->vinetic;
									ast_mutex_lock(&vin->lock);
									// unblock vinetic
									if (vin_reset_status(&vin->context) < 0) {
										ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
										ast_mutex_unlock(&vin->lock);
										break;
									}
									if ((ch_gsm->vinetic_alm_slot >= 0) &&
										(is_vin_ali_enabled(vin->context)) &&
											(is_vin_ali_channel_enabled(vin->context, ch_gsm->vinetic_alm_slot))) {
										// disable vinetic ALI channel
										if (vin_ali_channel_disable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_channel_disable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										// enable vinetic ALI channel
										vin_ali_channel_set_gainr(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.gainr);
										if (vin_ali_channel_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_channel_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										// enable vinetic ALI Near End LEC
										if (vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
									}
									ast_mutex_unlock(&vin->lock);
								}
								ast_cli(a->fd, " - 0x%02x - ok\n", ch_gsm->config.gainr);
							} else
								ast_cli(a->fd, " - can't get gain value from \"%s\"\n", a->argv[6]);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_MODULETYPE:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSSENDINTERVAL:
							if (is_str_digit(a->argv[6])) {
								tmpi = atoi(a->argv[6]);
								if (tmpi < 10) {
									tmpi = 10;
									ast_cli(a->fd, " - value %d less than 10 - set smssendinterval to 10\n", tmpi);
								}
								ch_gsm->config.sms_send_interval.tv_sec = tmpi;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - bad param - must be digit\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSSENDATTEMPT:
							if (is_str_digit(a->argv[6])) {
								tmpi = atoi(a->argv[6]);
								if (tmpi < 1) {
									tmpi = 1;
									ast_cli(a->fd, " - value %d less than 1 - set smssendattempt to 1\n", tmpi);
								}
								ch_gsm->config.sms_send_attempt = tmpi;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - bad param - must be digit\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSMAXPARTCOUNT:
							if (is_str_digit(a->argv[6])) {
								tmpi = atoi(a->argv[5]);
								if (tmpi < 1) {
									tmpi = 0;
									ast_cli(a->fd, " - value %d less than 1 - set smsmaxpartcount to 1\n", tmpi);
								}
								ch_gsm->config.sms_max_part = tmpi;
								ast_cli(a->fd, " - ok\n");
							} else
								ast_cli(a->fd, " - bad param - must be digit\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_ENABLE:
							ch_gsm->config.sms_notify_enable = -ast_true(a->argv[6]);
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_CONTEXT:
							ast_copy_string(ch_gsm->config.sms_notify_context, a->argv[6], sizeof(ch_gsm->config.sms_notify_context));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_EXTENSION:
							ast_copy_string(ch_gsm->config.sms_notify_extension, a->argv[6], sizeof(ch_gsm->config.sms_notify_extension));
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_TRUNK:
							// check for trunk exist
							if (!(tr_gsm = pg_get_trunk_gsm_by_name(a->argv[6]))) {
								// create trunk storage
								if ((tr_gsm = ast_calloc(1, sizeof(struct pg_trunk_gsm)))) {
									tr_gsm->name = ast_strdup(a->argv[6]);
									AST_LIST_INSERT_TAIL(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry);
									// init trunk channel list
									AST_LIST_HEAD_SET_NOLOCK(&tr_gsm->channel_gsm_list, NULL);
									tr_gsm->channel_gsm_last = NULL;
								}
							}
							if (tr_gsm) {
								// check for channel already in trunk
								if (!pg_get_channel_gsm_from_trunk_by_name(tr_gsm, ch_gsm->alias)) {
									// create trunk channel gsm fold
									if ((ch_gsm_fold = ast_calloc(1, sizeof(struct pg_trunk_gsm_channel_gsm_fold)))) {
										ch_gsm_fold->name = ast_strdup(a->argv[6]);
										// set channel pointer
										ch_gsm_fold->channel_gsm = ch_gsm;
										// add entry into trunk list
										AST_LIST_INSERT_TAIL(&tr_gsm->channel_gsm_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry);
										// add entry into channel list
										AST_LIST_INSERT_TAIL(&ch_gsm->trunk_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_channel_list_entry);
										ast_cli(a->fd, " - ok\n");
									} else
										ast_cli(a->fd, " - can't get memory for GSM trunk=\"%s\" channel entry\n", a->argv[6]);
								} else
									ast_cli(a->fd, " - channel already in GSM trunk=\"%s\"\n", a->argv[6]);
							} else
								ast_cli(a->fd, " - can't get memory for GSM trunk=\"%s\"\n", a->argv[6]);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_TRUNKONLY:
							ch_gsm->config.trunkonly = -ast_true(a->argv[6]);
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CONFALLOW:
							ch_gsm->config.conference_allowed = -ast_true(a->argv[6]);
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC:
							ch_gsm->config.ali_nelec = str_true(a->argv[6])?VIN_EN:VIN_DIS;
							// apply to vinetic
							if ((rtp = ch_gsm->channel_rtp)) {
								vin = rtp->vinetic;
								ast_mutex_lock(&vin->lock);
								// unblock vinetic
								if (vin_reset_status(&vin->context) < 0) {
									ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
									ast_mutex_unlock(&vin->lock);
									break;
								}
								if (ch_gsm->vinetic_alm_slot >= 0) {
									if (is_vin_ali_enabled(vin->context)) {
										// disable vinetic ALI Near End LEC
										if (vin_ali_near_end_lec_disable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										if (ch_gsm->config.ali_nelec == VIN_EN) {
											// enable vinetic ALI Near End LEC
											vin_ali_near_end_lec_set_dtm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_tm);
											vin_ali_near_end_lec_set_oldc(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_oldc);
											vin_ali_near_end_lec_set_as(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_as);
											vin_ali_near_end_lec_set_nlp(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlp);
											vin_ali_near_end_lec_set_nlpm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlpm);
											if (vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
												ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
												ast_mutex_unlock(&vin->lock);
												break;
											}
										}
									}
								}
								ast_mutex_unlock(&vin->lock);
							}
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_TM:
							ch_gsm->config.ali_nelec_tm = str_true(a->argv[6])?VIN_DTM_ON:VIN_DTM_OFF;
							// apply to vinetic
							if ((rtp = ch_gsm->channel_rtp)) {
								vin = rtp->vinetic;
								ast_mutex_lock(&vin->lock);
								// unblock vinetic
								if (vin_reset_status(&vin->context) < 0) {
									ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
									ast_mutex_unlock(&vin->lock);
									break;
								}
								if (ch_gsm->vinetic_alm_slot >= 0) {
									if (is_vin_ali_enabled(vin->context)) {
										// disable vinetic ALI Near End LEC
										if (vin_ali_near_end_lec_disable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										if (ch_gsm->config.ali_nelec == VIN_EN) {
											// enable vinetic ALI Near End LEC
											vin_ali_near_end_lec_set_dtm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_tm);
											vin_ali_near_end_lec_set_oldc(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_oldc);
											vin_ali_near_end_lec_set_as(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_as);
											vin_ali_near_end_lec_set_nlp(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlp);
											vin_ali_near_end_lec_set_nlpm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlpm);
											if (vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
												ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
												ast_mutex_unlock(&vin->lock);
												break;
											}
										}
									}
								}
								ast_mutex_unlock(&vin->lock);
							}
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_OLDC:
							if (!strcmp(a->argv[6], "oldc"))
								ch_gsm->config.ali_nelec_oldc = VIN_OLDC_NO;
							else
								ch_gsm->config.ali_nelec_oldc = VIN_OLDC_ZERO;
							// apply to vinetic
							if ((rtp = ch_gsm->channel_rtp)) {
								vin = rtp->vinetic;
								ast_mutex_lock(&vin->lock);
								// unblock vinetic
								if (vin_reset_status(&vin->context) < 0) {
									ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
									ast_mutex_unlock(&vin->lock);
									break;
								}
								if (ch_gsm->vinetic_alm_slot >= 0) {
									if (is_vin_ali_enabled(vin->context)) {
										// disable vinetic ALI Near End LEC
										if (vin_ali_near_end_lec_disable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										if (ch_gsm->config.ali_nelec == VIN_EN) {
											// enable vinetic ALI Near End LEC
											vin_ali_near_end_lec_set_dtm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_tm);
											vin_ali_near_end_lec_set_oldc(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_oldc);
											vin_ali_near_end_lec_set_as(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_as);
											vin_ali_near_end_lec_set_nlp(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlp);
											vin_ali_near_end_lec_set_nlpm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlpm);
											if (vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
												ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
												ast_mutex_unlock(&vin->lock);
												break;
											}
										}
									}
								}
								ast_mutex_unlock(&vin->lock);
							}
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_AS:
							ch_gsm->config.ali_nelec_as = str_true(a->argv[6])?VIN_AS_RUN:VIN_AS_STOP;
							// apply to vinetic
							if ((rtp = ch_gsm->channel_rtp)) {
								vin = rtp->vinetic;
								ast_mutex_lock(&vin->lock);
								// unblock vinetic
								if (vin_reset_status(&vin->context) < 0) {
									ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
									ast_mutex_unlock(&vin->lock);
									break;
								}
								if (ch_gsm->vinetic_alm_slot >= 0) {
									if (is_vin_ali_enabled(vin->context)) {
										// disable vinetic ALI Near End LEC
										if (vin_ali_near_end_lec_disable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										if (ch_gsm->config.ali_nelec == VIN_EN) {
											// enable vinetic ALI Near End LEC
											vin_ali_near_end_lec_set_dtm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_tm);
											vin_ali_near_end_lec_set_oldc(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_oldc);
											vin_ali_near_end_lec_set_as(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_as);
											vin_ali_near_end_lec_set_nlp(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlp);
											vin_ali_near_end_lec_set_nlpm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlpm);
											if (vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
												ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
												ast_mutex_unlock(&vin->lock);
												break;
											}
										}
									}
								}
								ast_mutex_unlock(&vin->lock);
							}
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_NLP:
							ch_gsm->config.ali_nelec_nlp = str_true(a->argv[6])?VIN_ON:VIN_OFF;
							// apply to vinetic
							if ((rtp = ch_gsm->channel_rtp)) {
								vin = rtp->vinetic;
								ast_mutex_lock(&vin->lock);
								// unblock vinetic
								if (vin_reset_status(&vin->context) < 0) {
									ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
									ast_mutex_unlock(&vin->lock);
									break;
								}
								if (ch_gsm->vinetic_alm_slot >= 0) {
									if (is_vin_ali_enabled(vin->context)) {
										// disable vinetic ALI Near End LEC
										if (vin_ali_near_end_lec_disable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										if (ch_gsm->config.ali_nelec == VIN_EN) {
											// enable vinetic ALI Near End LEC
											vin_ali_near_end_lec_set_dtm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_tm);
											vin_ali_near_end_lec_set_oldc(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_oldc);
											vin_ali_near_end_lec_set_as(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_as);
											vin_ali_near_end_lec_set_nlp(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlp);
											vin_ali_near_end_lec_set_nlpm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlpm);
											if (vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
												ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
												ast_mutex_unlock(&vin->lock);
												break;
											}
										}
									}
								}
								ast_mutex_unlock(&vin->lock);
							}
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_NLPM:
							tmpi = pg_vinetic_get_ali_nelec_nlpm(a->argv[6]);
							if (tmpi < 0) {
								ast_cli(a->fd, " - unknown nlp mode\n");
								break;
							}
							ch_gsm->config.ali_nelec_nlpm = tmpi;
							// apply to vinetic
							if ((rtp = ch_gsm->channel_rtp)) {
								vin = rtp->vinetic;
								ast_mutex_lock(&vin->lock);
								// unblock vinetic
								if (vin_reset_status(&vin->context) < 0) {
									ast_cli(a->fd, "vinetic=\"%s\": vin_reset_status(): %s\n", vin->name, vin_error_str(&vin->context));
									ast_mutex_unlock(&vin->lock);
									break;
								}
								if (ch_gsm->vinetic_alm_slot >= 0) {
									if (is_vin_ali_enabled(vin->context)) {
										// disable vinetic ALI Near End LEC
										if (vin_ali_near_end_lec_disable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
											ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
											ast_mutex_unlock(&vin->lock);
											break;
										}
										if (ch_gsm->config.ali_nelec == VIN_EN) {
											// enable vinetic ALI Near End LEC
											vin_ali_near_end_lec_set_dtm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_tm);
											vin_ali_near_end_lec_set_oldc(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_oldc);
											vin_ali_near_end_lec_set_as(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_as);
											vin_ali_near_end_lec_set_nlp(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlp);
											vin_ali_near_end_lec_set_nlpm(vin->context, ch_gsm->vinetic_alm_slot, ch_gsm->config.ali_nelec_nlpm);
											if (vin_ali_near_end_lec_enable(vin->context, ch_gsm->vinetic_alm_slot) < 0) {
												ast_cli(a->fd, "vinetic=\"%s\": vin_ali_near_end_lec_enable(): %s\n", vin->name, vin_error_str(&vin->context));
												ast_mutex_unlock(&vin->lock);
												break;
											}
										}
									}
								}
								ast_mutex_unlock(&vin->lock);
							}
							ast_cli(a->fd, " - ok\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						default:
							ast_cli(a->fd, " - unknown parameter\n");
							break;
					}
					break;
				//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				case PG_CHANNEL_GSM_PARAM_OP_DELETE:
					ast_cli(a->fd, "delete(%s)", a->argv[5]);
					switch (param)
					{
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_TRUNK:
							// check for trunk exist
							if ((tr_gsm = pg_get_trunk_gsm_by_name(a->argv[6]))) {
								if ((ch_gsm_fold = pg_get_channel_gsm_fold_from_trunk_by_name(tr_gsm, ch_gsm->alias))) {
									// remove entry from trunk list
									AST_LIST_REMOVE(&tr_gsm->channel_gsm_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry);
									// remove entry from channel list
									AST_LIST_REMOVE(&ch_gsm->trunk_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_channel_list_entry);
									ast_free(ch_gsm_fold->name);
									ast_free(ch_gsm_fold);
									ast_cli(a->fd, " - channel removed from GSM trunk=\"%s\"\n", a->argv[6]);
								} else
									ast_cli(a->fd, " - channel is not member of GSM trunk=\"%s\"\n", a->argv[6]);
							} else
								ast_cli(a->fd, " - GSM trunk=\"%s\" not exist\n", a->argv[6]);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						default:
							ast_cli(a->fd, " - unknown parameter\n");
							break;
					}
					break;
				//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				case PG_CHANNEL_GSM_PARAM_OP_QUERY:
					ast_cli(a->fd, "query(%s)", a->argv[5]);
					switch (param)
					{
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_ALIAS:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PIN:
							cp = pg_get_pin_by_iccid(ch_gsm->iccid, &ch_gsm->lock);
							ast_cli(a->fd, " -> %s\n", cp?cp:"unknown");
							if(cp) ast_free(cp);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PUK:
							cp = pg_get_puk_by_iccid(ch_gsm->iccid, &ch_gsm->lock);
							ast_cli(a->fd, " -> %s\n", cp?cp:"unknown");
							if(cp) ast_free(cp);
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_IMEI:
							ast_cli(a->fd, " - command under working\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NUMBER:
							ast_cli(a->fd, " - command under working\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_PROGRESS:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_INCOMING:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_INCOMINGTO:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_OUTGOING:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CONTEXT:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_REGATTEMPT:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CALLWAIT:
							ast_cli(a->fd, " - command under working\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_CLIR:
							ast_cli(a->fd, " - command under working\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_DCRTTL:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_LANGUAGE:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_MOHINTERPRET:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_BAUDRATE:
							ast_cli(a->fd, " - command under working\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAININ:
							ast_cli(a->fd, " - command under working\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINOUT:
							ast_cli(a->fd, " - command under working\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAIN1:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAIN2:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINX:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_GAINR:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_MODULETYPE:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSSENDINTERVAL:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSSENDATTEMPT:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMSMAXPARTCOUNT:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_ENABLE:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_CONTEXT:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_SMS_NOTIFY_EXTENSION:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_TM:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_OLDC:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_AS:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_NLP:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						case PG_CHANNEL_GSM_PARAM_NELEC_NLPM:
							ast_cli(a->fd, " - unsupported operation\n");
							break;
						//++++++++++++++++++++++++++++++++++++++++++++++++++++++
						default:
							ast_cli(a->fd, " - unknown parameter\n");
							break;
					}
					break;
				//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				default:
					ast_cli(a->fd, " - unknown operation\n");
					break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}

	if (!total)
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_param()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_action_debug()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_action_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_channel_gsm *ch_gsm;

	char *gline;
	char *gargv[AST_MAX_ARGS];
	int gargc;

	int debug_prmid;
	size_t total;
	int res;
	char path[PATH_MAX];

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			ast_cli(a->fd, "is ch_act_dbg subhandler -- CLI_INIT unsupported in this context\n");
			return CLI_FAILURE;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			gline = ast_strdupa(a->line);
			if (!(pg_cli_generating_prepare(gline, &gargc, gargv))) {
				if (a->pos == 5)
					return pg_cli_generate_complete_channel_gsm_action_debug_params(a->word, a->n);
				if (a->pos == 6)
					return pg_cli_generate_complete_channel_gsm_action_debug_param_on_off(a->word, a->n, gargv[3], gargv[5]);
			}
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 6) {
		snprintf(pg_cli_channel_gsm_actions_usage, sizeof(pg_cli_channel_gsm_actions_usage),
			"Usage: polygator channel gsm <channel> debug <what> on|off\n");
		return CLI_SHOWUSAGE;
	}

	// get debug param id
	debug_prmid = pg_get_channel_gsm_debug_param(a->argv[5]);
	if (debug_prmid == PG_CHANNEL_GSM_DEBUG_UNKNOWN) {
		ast_cli(a->fd, "  Unknown debug parameter \"%s\"\n", a->argv[5]);
		return CLI_SUCCESS;
	}

	total = 0;
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		if (!strcmp(a->argv[3], "all") || !strcmp(a->argv[3], ch_gsm->alias)) {
			total++;
			ast_cli(a->fd, "  GSM channel=\"%s\": ", ch_gsm->alias);
			switch(debug_prmid)
			{
				//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				case PG_CHANNEL_GSM_DEBUG_AT:
					ast_cli(a->fd, "%s debug", a->argv[5]);
					if (a->argv[6] && !ast_true(a->argv[6])) {
						// off
						if (ch_gsm->debug.at) {
							ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+");
							if (ch_gsm->debug.at_debug_fp) {
								fprintf(ch_gsm->debug.at_debug_fp, "\ndebug stoped\n");
								fflush(ch_gsm->debug.at_debug_fp);
								fclose(ch_gsm->debug.at_debug_fp);
								ch_gsm->debug.at_debug_fp = NULL;
								ast_free(ch_gsm->debug.at_debug_path);
								ch_gsm->debug.at_debug_path = NULL;
							}
							ast_cli(a->fd, " disabled\n");
						} else
							ast_cli(a->fd, " already disabled\n");
						ch_gsm->debug.at = 0;
					} else {
						// on
						if (!ch_gsm->debug.at) {
							snprintf(path, sizeof(path), "%s/polygator", ast_config_AST_LOG_DIR);
							if (!(res = ast_mkdir(path, S_ISUID|S_ISGID|
														S_IWUSR|S_IRUSR|S_IXUSR|
														S_IWGRP|S_IRGRP|S_IXGRP|
														S_IWOTH|S_IROTH|S_IXOTH))) {
								snprintf(path, sizeof(path), "%s/polygator/at-%s.dbg", ast_config_AST_LOG_DIR, ch_gsm->alias);
								if (ch_gsm->debug.at_debug_path) ast_free(ch_gsm->debug.at_debug_path);
								ch_gsm->debug.at_debug_path = ast_strdup(path);
								if ((ch_gsm->debug.at_debug_fp = fopen(ch_gsm->debug.at_debug_path, "a+"))) {
									fprintf(ch_gsm->debug.at_debug_fp, "\ndebug started\n");
									fflush(ch_gsm->debug.at_debug_fp);
									fclose(ch_gsm->debug.at_debug_fp);
									ch_gsm->debug.at_debug_fp = NULL;
								} else {
									ast_cli(a->fd, "can't open file=\"%s\": %s\n", ch_gsm->debug.at_debug_path, strerror(errno));
									break;
								}
							} else { // ast_mkdir error
								ast_cli(a->fd, "can't make parent dir for file \"%s\": %s\n", ch_gsm->debug.at_debug_path, strerror(errno));
								break;
							}
							ast_cli(a->fd, " enabled\n");
						} else
							ast_cli(a->fd, " already enabled\n");
						ch_gsm->debug.at = 1;
					}
					break;
				//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				case PG_CHANNEL_GSM_DEBUG_RECEIVER:
					ast_cli(a->fd, "%s debug", a->argv[5]);
					if (a->argv[6] && !ast_true(a->argv[6])) {
						// off
						if (ch_gsm->debug.receiver) {
							ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+");
							if (ch_gsm->debug.receiver_debug_fp) {
								fprintf(ch_gsm->debug.receiver_debug_fp, "\ndebug stoped\n");
								fflush(ch_gsm->debug.receiver_debug_fp);
								fclose(ch_gsm->debug.receiver_debug_fp);
								ch_gsm->debug.receiver_debug_fp = NULL;
								ast_free(ch_gsm->debug.receiver_debug_path);
								ch_gsm->debug.receiver_debug_path = NULL;
							}
							ast_cli(a->fd, " disabled\n");
						} else
							ast_cli(a->fd, " already disabled\n");
						ch_gsm->debug.receiver = 0;
					} else {
						// on
						if (!ch_gsm->debug.receiver) {
							snprintf(path, sizeof(path), "%s/polygator", ast_config_AST_LOG_DIR);
							if (!(res = ast_mkdir(path, S_ISUID|S_ISGID|
														S_IWUSR|S_IRUSR|S_IXUSR|
														S_IWGRP|S_IRGRP|S_IXGRP|
														S_IWOTH|S_IROTH|S_IXOTH))) {
								snprintf(path, sizeof(path), "%s/polygator/receiver-%s.dbg", ast_config_AST_LOG_DIR, ch_gsm->alias);
								if (ch_gsm->debug.receiver_debug_path) ast_free(ch_gsm->debug.receiver_debug_path);
								ch_gsm->debug.receiver_debug_path = ast_strdup(path);
								if ((ch_gsm->debug.receiver_debug_fp = fopen(ch_gsm->debug.receiver_debug_path, "a+"))) {
									fprintf(ch_gsm->debug.receiver_debug_fp, "\ndebug started\n");
									fflush(ch_gsm->debug.receiver_debug_fp);
									fclose(ch_gsm->debug.receiver_debug_fp);
									ch_gsm->debug.receiver_debug_fp = NULL;
								} else {
									ast_cli(a->fd, "can't open file=\"%s\": %s\n", ch_gsm->debug.receiver_debug_path, strerror(errno));
									break;
								}
							} else { // ast_mkdir error
								ast_cli(a->fd, "can't make parent dir for file \"%s\": %s\n", ch_gsm->debug.receiver_debug_path, strerror(errno));
								break;
							}
							ast_cli(a->fd, " enabled\n");
						} else
							ast_cli(a->fd, " already enabled\n");
						ch_gsm->debug.receiver = 1;
					}
					break;
				//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				default:
					ast_cli(a->fd, "unknown debug parameter \"%s\"\n", a->argv[5]);
					break;
			}
		}
		ast_mutex_unlock(&ch_gsm->lock);
	}

	if (!total)
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_debug()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_action_ussd()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_action_ussd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_channel_gsm *ch_gsm;
	
	char buf[1024];
	char *buf_ptr;
	int buf_len;
	
	char *in_ptr;
	int in_len;

	char resp_databuf[1024];
	int resp_datalen;

	int is_ussd_send;
	int pipe_flags;

	struct timeval tv;
#ifdef HAVE_ASTERISK_SELECT_H
	ast_fdset rfds;
#else
	fd_set rfds;
#endif
	int res;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			ast_cli(a->fd, "is ch_act_ussd subhandler -- CLI_INIT unsupported in this context\n");
			return CLI_FAILURE;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 6) {
		sprintf(pg_cli_channel_gsm_actions_usage, "Usage: polygator channel gsm <channel> ussd <ussd>\n");
		return CLI_SHOWUSAGE;
	}
	// check name param for wildcard "all"
	if (!strcmp(a->argv[3], "all")) {
		ast_cli(a->fd, "wildcard \"all\" not supported -- use channel name\n");
		return CLI_SUCCESS;
	}

	is_ussd_send = 0;

	// get GSM channel
	if ((ch_gsm = pg_get_channel_gsm_by_name(a->argv[3]))) {
		ast_mutex_lock(&ch_gsm->lock);
		// check condition for send USSD
		if ((ch_gsm->state == PG_CHANNEL_GSM_STATE_RUN) &&
			((ch_gsm->reg_stat == REG_STAT_REG_HOME_NET) || (ch_gsm->reg_stat == REG_STAT_REG_ROAMING)) &&
			(!pg_is_channel_gsm_has_calls(ch_gsm)) &&
			((ch_gsm->ussd_pipe[0] == -1) && (ch_gsm->ussd_pipe[1] == -1))) {
			// check for end USSD session
			if (strcasecmp(a->argv[5], "end")) {
				// convert string to hex presentation
				in_len = strlen(a->argv[5]);
				in_ptr = (char *)a->argv[5];
				memset(buf, 0, sizeof(buf));
				buf_ptr = buf;
				buf_len = sizeof(buf);
				if (!str_bin_to_hex(&in_ptr, &in_len, &buf_ptr, &buf_len)) {
					ch_gsm->ussd_sub_cmd = PG_AT_SUBCMD_CUSD_USER;
					pg_atcommand_queue_append(ch_gsm, AT_CUSD, AT_OPER_WRITE, PG_AT_SUBCMD_CUSD_USER, 60, 0, "%d,\"%s\"", 1, buf);
					ast_cli(a->fd, "  GSM channel=\"%s\": send USSD \"%s\"...\n", ch_gsm->alias, a->argv[5]);
					is_ussd_send = 1;
					ch_gsm->state = PG_CHANNEL_GSM_STATE_SERVICE;
				} else
					ast_cli(a->fd, "  GSM channel=\"%s\": send USSD \"%s\" - fail convert to hex\n", ch_gsm->alias, a->argv[5]);
			} else {
				// end USSD session
				ch_gsm->ussd_sub_cmd = PG_AT_SUBCMD_CUSD_USER;
				pg_atcommand_queue_append(ch_gsm, AT_CUSD, AT_OPER_WRITE, PG_AT_SUBCMD_CUSD_USER, pg_at_response_timeout, 0, "2");
				ast_cli(a->fd, "  GSM channel=\"%s\": end USSD session\n", ch_gsm->alias);
			}
		} else {
			ast_cli(a->fd, "  GSM channel=\"%s\": unable to send USSD \"%s\":", ch_gsm->alias, a->argv[5]);
			// print expected condition
			if (ch_gsm->state != PG_CHANNEL_GSM_STATE_RUN)
				ast_cli(a->fd, " \"channel is in %s state\"", pg_cahnnel_gsm_state_to_string(ch_gsm->state));
			if ((ch_gsm->reg_stat != REG_STAT_REG_HOME_NET) && (ch_gsm->reg_stat != REG_STAT_REG_ROAMING))
				ast_cli(a->fd, " \"channel not registered\"");
			if ((res = pg_channel_gsm_get_calls_count(ch_gsm)) > 0)
				ast_cli(a->fd, " \"channel has %d call%s\"", res, ESS(res));
			if ((ch_gsm->ussd_pipe[0] != -1) || (ch_gsm->ussd_pipe[1] != -1))
				ast_cli(a->fd, " \"channels ussd pipe temporarily used by another CLI\"");
			ast_cli(a->fd, "\n");
		}
		if (is_ussd_send) {
			if (pipe(ch_gsm->ussd_pipe) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->ussd_pipe): %s\n", ch_gsm->alias, strerror(errno));
				goto is_ussd_send_end;
			}
			if ((pipe_flags = fcntl(ch_gsm->ussd_pipe[0], F_GETFL)) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->ussd_pipe[0], F_GETFL): %s\n", ch_gsm->alias, strerror(errno));
				goto is_ussd_send_end;
			}
			if (fcntl(ch_gsm->ussd_pipe[0], F_SETFL, pipe_flags|O_NONBLOCK) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->ussd_pipe[0], F_SETFL): %s\n", ch_gsm->alias, strerror(errno));
				goto is_ussd_send_end;
			}
			if ((pipe_flags = fcntl(ch_gsm->ussd_pipe[1], F_GETFL)) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->ussd_pipe[1], F_GETFL): %s\n", ch_gsm->alias, strerror(errno));
				goto is_ussd_send_end;
			}
			if (fcntl(ch_gsm->ussd_pipe[1], F_SETFL, pipe_flags|O_NONBLOCK) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->ussd_pipe[1], F_SETFL): %s\n", ch_gsm->alias, strerror(errno));
				goto is_ussd_send_end;
			}
			while (1)
			{
				// prepare select
				tv.tv_sec = 60;
				tv.tv_usec = 0;
				FD_ZERO(&rfds);
				FD_SET(ch_gsm->ussd_pipe[0], &rfds);
				ast_mutex_unlock(&ch_gsm->lock);
				res = ast_select(ch_gsm->ussd_pipe[0] + 1, &rfds, NULL, NULL, &tv);
				ast_mutex_lock(&ch_gsm->lock);
				// check select result code
				if (res > 0) {
					if (FD_ISSET(ch_gsm->ussd_pipe[0], &rfds)) {
						resp_datalen = read(ch_gsm->ussd_pipe[0], resp_databuf, sizeof(resp_databuf));
						if (resp_datalen > 0)
							ast_cli(a->fd, "%.*s\n", resp_datalen, resp_databuf);
						else if (resp_datalen < 0)
							ast_cli(a->fd, "  GSM channel=\"%s\": read(): %s\n", ch_gsm->alias, strerror(errno));
						else
							break;
					}
				} else if (res < 0) {
					ast_cli(a->fd, "  GSM channel=\"%s\": select(): %s\n", ch_gsm->alias, strerror(errno));
					break;
				} else {
					ast_cli(a->fd, "  GSM channel=\"%s\": wait for USSD response - time is out\n", ch_gsm->alias);
					break;
				}
			}
is_ussd_send_end:
			close(ch_gsm->ussd_pipe[0]);
			ch_gsm->ussd_pipe[0] = -1;
			close(ch_gsm->ussd_pipe[1]);
			ch_gsm->ussd_pipe[1] = -1;
			ch_gsm->state = PG_CHANNEL_GSM_STATE_RUN;
			ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
		}
		ast_mutex_unlock(&ch_gsm->lock);
	} else
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_ussd()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_action_at()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_action_at(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a){

	struct pg_channel_gsm *ch_gsm;

	char *cp;

	char resp_databuf[1024];
	int resp_datalen;

	int is_at_send = 0;
	int pipe_flags;

	struct timeval tv;
#ifdef HAVE_ASTERISK_SELECT_H
	ast_fdset rfds;
#else
	fd_set rfds;
#endif
	int res;

	switch(cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			ast_cli(a->fd, "is ch_act_at subhandler -- CLI_INIT unsupported in this context\n");
			return CLI_FAILURE;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 6) {
		sprintf(pg_cli_channel_gsm_actions_usage, "Usage: polygator channel gsm <channel> at <command>\n");
		return CLI_SHOWUSAGE;
	}

	// check name param for wildcard "all"
	if (!strcmp(a->argv[3], "all")) {
		ast_cli(a->fd, "wildcard \"all\" not supported -- use channel name\n");
		return CLI_SUCCESS;
	}

	is_at_send = 0;

	// get GSM channel 
	if ((ch_gsm = pg_get_channel_gsm_by_name(a->argv[3]))) {
		ast_mutex_lock(&ch_gsm->lock);
		// check condition for send AT
		if ((ch_gsm->flags.enable) && ((ch_gsm->at_pipe[0] == -1) && (ch_gsm->at_pipe[1] == -1))) {
			// fix asterisk CLI restrictions "!" -> "?"
			cp = (char *)a->argv[5];
			while (*cp)
			{
				if(*cp == '!') *cp = '?';
				cp++;
			}
			ast_cli(a->fd, "  GSM channel=\"%s\": send AT command \"%s\"...\n", ch_gsm->alias, a->argv[5]);
			pg_atcommand_queue_append(ch_gsm, AT_UNKNOWN, AT_OPER_EXEC, 0, 60, 1, "%s", a->argv[5]);
			is_at_send = 1;
		} else {
			ast_cli(a->fd, "  GSM channel=\"%s\": unable to send AT command \"%s\":", ch_gsm->alias, a->argv[5]);
			// print expected condition
			if (!ch_gsm->flags.enable)
				ast_cli(a->fd, " \"channel disabled\"");
			if ((ch_gsm->at_pipe[0] != -1) || (ch_gsm->at_pipe[1] != -1))
				ast_cli(a->fd, " \"channels at pipe temporarily used by another CLI\"");
			ast_cli(a->fd, "\n");
		}
		if (is_at_send) {
			if (pipe(ch_gsm->at_pipe) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->at_pipe): %s\n", ch_gsm->alias, strerror(errno));
				goto is_at_send_end;
			}
			if ((pipe_flags = fcntl(ch_gsm->at_pipe[0], F_GETFL)) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->at_pipe[0], F_GETFL): %s\n", ch_gsm->alias, strerror(errno));
				goto is_at_send_end;
			}
			if (fcntl(ch_gsm->at_pipe[0], F_SETFL, pipe_flags|O_NONBLOCK) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->at_pipe[0], F_SETFL): %s\n", ch_gsm->alias, strerror(errno));
				goto is_at_send_end;
			}
			if ((pipe_flags = fcntl(ch_gsm->at_pipe[1], F_GETFL)) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->at_pipe[1], F_GETFL): %s\n", ch_gsm->alias, strerror(errno));
				goto is_at_send_end;
			}
			if (fcntl(ch_gsm->at_pipe[1], F_SETFL, pipe_flags|O_NONBLOCK) < 0) {
				ast_cli(a->fd, "  GSM channel=\"%s\": fcntl(ch_gsm->at_pipe[1], F_SETFL): %s\n", ch_gsm->alias, strerror(errno));
				goto is_at_send_end;
			}
			while (1)
			{
				// prepare select
				tv.tv_sec = 60;
				tv.tv_usec = 0;
				FD_ZERO(&rfds);
				FD_SET(ch_gsm->at_pipe[0], &rfds);
				ast_mutex_unlock(&ch_gsm->lock);
				res = ast_select(ch_gsm->at_pipe[0] + 1, &rfds, NULL, NULL, &tv);
				ast_mutex_lock(&ch_gsm->lock);
				// check select result code
				if (res > 0) {
					if (FD_ISSET(ch_gsm->at_pipe[0], &rfds)) {
						resp_datalen = read(ch_gsm->at_pipe[0], resp_databuf, sizeof(resp_databuf));
						if (resp_datalen > 0)
							ast_cli(a->fd, "%.*s", resp_datalen, resp_databuf);
						else if (resp_datalen < 0)
							ast_cli(a->fd, "  GSM channel=\"%s\": read(): %s\n", ch_gsm->alias, strerror(errno));
						else
							break;
					}
				} else if (res < 0) {
					ast_cli(a->fd, "  GSM channel=\"%s\": select(): %s\n", ch_gsm->alias, strerror(errno));
					break;
				} else {
					ast_cli(a->fd, "  GSM channel=\"%s\": wait for AT response - time is out\n", ch_gsm->alias);
					break;
				}
			}
is_at_send_end:
			close(ch_gsm->at_pipe[0]);
			ch_gsm->at_pipe[0] = -1;
			close(ch_gsm->at_pipe[1]);
			ch_gsm->at_pipe[1] = -1;
		}
		
		ast_mutex_unlock(&ch_gsm->lock);
	} else
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_at()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_channel_gsm_action_dcr()
//------------------------------------------------------------------------------
static char *pg_cli_channel_gsm_action_dcr(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a){

	struct pg_channel_gsm *ch_gsm;

	struct timeval tv;
	char from[MAX_ADDRESS_LENGTH];
	char to[MAX_ADDRESS_LENGTH];
	char *str;
	sqlite3_stmt *sql;
	int res;
	int row;

	char buf[20];
	char tmbuf[20];
	int number_fl;
	int from_fl;
	int to_fl;
	int ttl_fl;

	switch(cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			ast_cli(a->fd, "is ch_act_at subhandler -- CLI_INIT unsupported in this context\n");
			return CLI_FAILURE;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 5) {
		sprintf(pg_cli_channel_gsm_actions_usage, "Usage: polygator channel gsm <channel> dcr\n");
		return CLI_SHOWUSAGE;
	}

	// check name param for wildcard "all"
	if (!strcmp(a->argv[3], "all")) {
		ast_cli(a->fd, "wildcard \"all\" not supported -- use channel name\n");
		return CLI_SUCCESS;
	}

	number_fl = strlen("#");
	from_fl = strlen("From");
	to_fl = strlen("To");
	ttl_fl = strlen("TTL");

	if ((ch_gsm = pg_get_channel_gsm_by_name(a->argv[3]))) {
		ast_mutex_lock(&ch_gsm->lock);
		if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_DYN) {
			if (ch_gsm->iccid) {
				// get dynamic clip routing table
				gettimeofday(&tv, NULL);
				ast_mutex_lock(&pg_gen_db_lock);
				str = sqlite3_mprintf("SELECT fromtype,fromname,totype,toname,timestamp FROM '%q-dcr' WHERE timestamp>%ld ORDER BY timestamp DESC;", ch_gsm->imsi, (long int)(tv.tv_sec - ch_gsm->config.dcrttl));
				while (1)
				{
					res = sqlite3_prepare_fun(pg_gen_db, str, strlen(str), &sql, NULL);
					if (res == SQLITE_OK) {
						row = 0;
						while (1)
						{
							res = sqlite3_step(sql);
							if (res == SQLITE_ROW) {
								number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%d", row));
								from_fl = mmax(from_fl, snprintf(from, sizeof(from), "%s%s", (sqlite3_column_int(sql, 0) == 145)?("+"):(""), sqlite3_column_text(sql, 1)));
								to_fl = mmax(to_fl, snprintf(to, sizeof(to), "%s%s", (sqlite3_column_int(sql, 2) == 145)?("+"):(""), sqlite3_column_text(sql, 3)));
								ttl_fl = mmax(ttl_fl, snprintf(tmbuf, sizeof(tmbuf), "%ld", (long int)(ch_gsm->config.dcrttl - (tv.tv_sec - (long int)sqlite3_column_int64(sql, 4)))));
								row++;
							} else if (res == SQLITE_DONE)
								break;
							else if (res == SQLITE_BUSY) {
								ast_mutex_unlock(&ch_gsm->lock);
								usleep(1000);
								ast_mutex_lock(&ch_gsm->lock);
								continue;
							} else {
								ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
								break;
							}
						}
						if ((row) && (sqlite3_reset(sql) == SQLITE_OK)) {
							ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s |\n",
									number_fl, "#",
		  							from_fl, "From",
									to_fl, "To",
									ttl_fl, "TTL");
							row = 0;
							while (1)
							{
								res = sqlite3_step(sql);
								if (res == SQLITE_ROW) {
									snprintf(buf, sizeof(buf), "%d", row);
									snprintf(from, sizeof(from), "%s%s", (sqlite3_column_int(sql, 0) == 145)?("+"):(""), sqlite3_column_text(sql, 1));
									snprintf(to, sizeof(to), "%s%s", (sqlite3_column_int(sql, 2) == 145)?("+"):(""), sqlite3_column_text(sql, 3));
									snprintf(tmbuf, sizeof(tmbuf), "%ld", (long int)(ch_gsm->config.dcrttl - (tv.tv_sec - (long int)sqlite3_column_int64(sql, 4))));
									ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s |\n",
										number_fl, buf,
			  							from_fl, from,
										to_fl, to,
										ttl_fl, tmbuf);
									row++;
								} else if (res == SQLITE_DONE)
									break;
								else if (res == SQLITE_BUSY) {
									ast_mutex_unlock(&ch_gsm->lock);
									usleep(1000);
									ast_mutex_lock(&ch_gsm->lock);
									continue;
								} else {
									ast_log(LOG_ERROR, "sqlite3_step(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
									break;
								}
							}
						} else
							ast_cli(a->fd, "<%s>: dynamic clip routing table is empty\n", ch_gsm->alias);
						sqlite3_finalize(sql);
						break;
					} else if (res == SQLITE_BUSY) {
						ast_mutex_unlock(&ch_gsm->lock);
						usleep(1000);
						ast_mutex_lock(&ch_gsm->lock);
						continue;
					} else {
						ast_log(LOG_ERROR, "sqlite3_prepare_fun(): %d: %s\n", res, sqlite3_errmsg(pg_gen_db));
						break;
					}
				}
				sqlite3_free(str);
				ast_mutex_unlock(&pg_gen_db_lock);
			} else
				ast_cli(a->fd, "<%s>: dynamic clip routing table is empty\n", ch_gsm->alias);
		} else
			ast_cli(a->fd, "  GSM channel=\"%s\": this channel not used dynamic clip routing\n", ch_gsm->alias);
		ast_mutex_unlock(&ch_gsm->lock);
	} else
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_dcr()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cli_trunk_gsm_actions()
//------------------------------------------------------------------------------
static char *pg_cli_trunk_gsm_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_trunk_gsm *tr_gsm;
	struct pg_channel_gsm *ch_gsm;
	struct pg_trunk_gsm_channel_gsm_fold *tr_fold, *ch_fold;

	char *gline;
	char *gargv[AST_MAX_ARGS];
	int gargc;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator trunk gsm [create|delete|rename]";
			sprintf(pg_cli_trunk_gsm_actions_usage, "Usage: polygator trunk gsm <action> <trunk> [...]\n");
			e->usage = pg_cli_trunk_gsm_actions_usage;
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			gline = ast_strdupa(a->line);
			if (!(pg_cli_generating_prepare(gline, &gargc, gargv))) {
				// try to generate complete trunk name
				if ((a->pos == 4) && (strcmp(gargv[3], "create")))
					return pg_cli_generate_complete_trunk_gsm(a->word, a->n);
			}
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
	}

	if (a->argc < 4) {
		sprintf(pg_cli_trunk_gsm_actions_usage, "Usage: polygator trunk gsm <action> <trunk> [...]\n");
		return CLI_SHOWUSAGE;
	}
	// select operation
	if (!strcmp(a->argv[3], "create")) {
		// create
		ast_mutex_lock(&pg_lock);
		// search this trunk name in list
		if (!(tr_gsm = pg_get_trunk_gsm_by_name(a->argv[4]))) {
			// create trunk storage
			if ((tr_gsm = ast_calloc(1, sizeof(struct pg_trunk_gsm)))) {
				tr_gsm->name = ast_strdup(a->argv[4]);
				AST_LIST_INSERT_TAIL(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry);
				// init trunk channel list
				AST_LIST_HEAD_SET_NOLOCK(&tr_gsm->channel_gsm_list, NULL);
				tr_gsm->channel_gsm_last = NULL;
				ast_cli(a->fd, "  GSM trunk=\"%s\" registered\n", a->argv[4]);
			}
		} else
			ast_cli(a->fd, "  GSM trunk=\"%s\" already exist\n", a->argv[4]);
		ast_mutex_unlock(&pg_lock);
	} else if(!strcmp(a->argv[3], "delete")) {
		// delete
		ast_mutex_lock(&pg_lock);
		// search this trunk name in list
		if ((tr_gsm = pg_get_trunk_gsm_by_name(a->argv[4]))) {
			// clean trunk binding in trunk channel list member
			while ((tr_fold = AST_LIST_REMOVE_HEAD(&tr_gsm->channel_gsm_list, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry)))
			{
				if ((ch_gsm = tr_fold->channel_gsm)) {
					ast_mutex_lock(&ch_gsm->lock);
					AST_LIST_TRAVERSE(&ch_gsm->trunk_list, ch_fold, pg_trunk_gsm_channel_gsm_fold_channel_list_entry)
					if (!strcmp(tr_gsm->name, ch_fold->name)) break;
					if (ch_fold) {
						// remove trunk entry from channel
						AST_LIST_REMOVE(&ch_gsm->trunk_list, ch_fold, pg_trunk_gsm_channel_gsm_fold_channel_list_entry);
						ast_free(ch_fold->name);
						ast_free(ch_fold);
					}
					ast_mutex_unlock(&ch_gsm->lock);
				}
			}
			// remove trunk from list
			AST_LIST_REMOVE(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry);
			ast_free(tr_gsm->name);
			ast_free(tr_gsm);
			ast_cli(a->fd, "  GSM trunk=\"%s\" deleted\n", a->argv[4]);
		} else
			ast_cli(a->fd, "  GSM trunk=\"%s\" not found\n", a->argv[4]);
		ast_mutex_unlock(&pg_lock);
	} else if (!strcmp(a->argv[3], "rename")) {
		// rename
		ast_mutex_lock(&pg_lock);
		// check for new trunk name already exist
		if (!(tr_gsm = pg_get_trunk_gsm_by_name(a->argv[5]))) {
			// search this old trunk name in list
			if ((tr_gsm = pg_get_trunk_gsm_by_name(a->argv[4]))) {
				// rename trunk binding in trunk channel list member
				AST_LIST_TRAVERSE(&tr_gsm->channel_gsm_list, tr_fold, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry)
				{
					ast_free(tr_fold->name);
					tr_fold->name = ast_strdup(a->argv[5]);
				}
				// rename trunk
				ast_free(tr_gsm->name);
				tr_gsm->name = ast_strdup(a->argv[5]);
				ast_cli(a->fd, "  GSM trunk=\"%s\" successfully renamed to \"%s\"\n", a->argv[4], a->argv[5]);
			} else
				ast_cli(a->fd, "  GSM trunk=\"%s\" not found\n", a->argv[4]);
		} else
			ast_cli(a->fd, "  GSM trunk=\"%s\" already exist\n", a->argv[5]);
		ast_mutex_unlock(&pg_lock);
	} else
		// unknown
		ast_cli(a->fd, "  unknown GSM trunk operation \"%s\"\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_trunk_gsm_actions()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_config_variable()
//------------------------------------------------------------------------------
static const char *pg_get_config_variable(struct ast_config *ast_cfg, const char *category, const char *variable)
{
	const char *res = NULL;
	char *cat = NULL;
	
	if (ast_cfg) {
		while ((cat = ast_category_browse(ast_cfg, cat)))
		{
			if (!strcasecmp(cat, category))
				res = ast_variable_retrieve(ast_cfg, cat, variable);
		}
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_get_config_variable()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_cleanup()
//------------------------------------------------------------------------------
static void pg_cleanup(void)
{
	struct pg_board *brd;
	struct pg_channel_gsm *ch_gsm;
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold;
	struct pg_trunk_gsm *tr_gsm;
	struct pg_vinetic *vin;
	struct pg_channel_rtp *rtp;

	int is_wait;

	ast_mutex_lock(&pg_lock);

	// unregistering Polygator CLI interface
	if (pg_cli_registered) {
		ast_cli_unregister_multiple(pg_cli, sizeof(pg_cli)/sizeof(pg_cli[0]));
		ast_verbose("Polygator: CLI unregistered\n");
	}

	// unregistering PGGSM channel technology
	if (pg_gsm_tech_registered) {
		ast_channel_unregister(&pg_gsm_tech);
#if ASTERISK_VERSION_NUM >= 100000
		ast_format_cap_destroy(pg_gsm_tech.capabilities);
#endif
	}

	// destroy trunk gsm list
	while ((tr_gsm = AST_LIST_REMOVE_HEAD(&pg_general_trunk_gsm_list, pg_general_trunk_gsm_list_entry)))
	{
		ast_free(tr_gsm->name);
		ast_free(tr_gsm);
	}

	// wait for all vinetics shutdown
	do {
		is_wait = 0;
		// traverse all boards
		AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
		{
			ast_mutex_lock(&brd->lock);
			// traverse all vinetics
			AST_LIST_TRAVERSE(&brd->vinetic_list, vin, pg_board_vinetic_list_entry)
			{
				ast_mutex_lock(&vin->lock);
				// clean run flag
				vin->run = 0;
				vin_reset_status(&vin->context);
				// wait for thread is done
				if (vin->thread != AST_PTHREADT_NULL) {
					ast_mutex_unlock(&vin->lock);
					ast_mutex_unlock(&brd->lock);
					ast_mutex_unlock(&pg_lock);
					usleep(1000);
					is_wait = 1;
					ast_mutex_lock(&pg_lock);
					ast_mutex_lock(&brd->lock);
					ast_mutex_lock(&vin->lock);
				}
				ast_mutex_unlock(&vin->lock);
			}
			ast_mutex_unlock(&brd->lock);
		}
	} while(is_wait);

	// wait for all channel shutdown
	do {
		is_wait = 0;
		//
		AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
		{
			ast_mutex_lock(&ch_gsm->lock);
			// emit shutdown signal
			if (!ch_gsm->flags.shutdown_now) {
				ch_gsm->flags.shutdown = 1;
				ch_gsm->flags.shutdown_now = 1;
			}
			// wait for reset enable flag
			if (ch_gsm->flags.enable) {
				ast_mutex_unlock(&ch_gsm->lock);
				ast_mutex_unlock(&pg_lock);
				usleep(1000);
				is_wait = 1;
				ast_mutex_lock(&pg_lock);
				ast_mutex_lock(&ch_gsm->lock);
			}
			ast_mutex_unlock(&ch_gsm->lock);
		}
	} while(is_wait);

	// destroy general GSM channel list
	while ((ch_gsm = AST_LIST_REMOVE_HEAD(&pg_general_channel_gsm_list, pg_general_channel_gsm_list_entry)));

	// destroy general board list
	while ((brd = AST_LIST_REMOVE_HEAD(&pg_general_board_list, pg_general_board_list_entry)))
	{
		// destroy board GSM channel list
		while ((ch_gsm = AST_LIST_REMOVE_HEAD(&brd->channel_gsm_list, pg_board_channel_gsm_list_entry)))
		{
			// destroy channel gsm trunk entry
			while ((ch_gsm_fold = AST_LIST_REMOVE_HEAD(&ch_gsm->trunk_list, pg_trunk_gsm_channel_gsm_fold_channel_list_entry)))
			{
				ast_free(ch_gsm_fold->name);
				ast_free(ch_gsm_fold);
			}
			if (ch_gsm->device) ast_free(ch_gsm->device);
			if (ch_gsm->tty_path) ast_free(ch_gsm->tty_path);
			if (ch_gsm->sim_path) ast_free(ch_gsm->sim_path);
			if (ch_gsm->alias) ast_free(ch_gsm->alias);
			if (ch_gsm->ussd) ast_free(ch_gsm->ussd);
			if (ch_gsm->balance) ast_free(ch_gsm->balance);
			if (ch_gsm->operator_name) ast_free(ch_gsm->operator_name);
			if (ch_gsm->operator_code) ast_free(ch_gsm->operator_code);
			if (ch_gsm->imsi) ast_free(ch_gsm->imsi);
			if (ch_gsm->iccid) ast_free(ch_gsm->iccid);
			if (ch_gsm->iccid_ban) ast_free(ch_gsm->iccid_ban);
			if (ch_gsm->model) ast_free(ch_gsm->model);
			if (ch_gsm->firmware) ast_free(ch_gsm->firmware);
			if (ch_gsm->pin) ast_free(ch_gsm->pin);
			if (ch_gsm->puk) ast_free(ch_gsm->puk);
			if (ch_gsm->config.balance_request) ast_free(ch_gsm->config.balance_request);
			if (ch_gsm->debug.at_debug_path) ast_free(ch_gsm->debug.at_debug_path);
			if (ch_gsm->debug.receiver_debug_path) ast_free(ch_gsm->debug.receiver_debug_path);
			ast_free(ch_gsm);
		}
		// destroy board vinetic list
		while ((vin = AST_LIST_REMOVE_HEAD(&brd->vinetic_list, pg_board_vinetic_list_entry)))
		{
			// destroy vinetic RTP channel list
			while ((rtp = AST_LIST_REMOVE_HEAD(&vin->channel_rtp_list, pg_vinetic_channel_rtp_list_entry)))
			{
				ast_free(rtp->name);
				ast_free(rtp->path);
				ast_free(rtp);
			}
#if ASTERISK_VERSION_NUM >= 100000
			ast_format_cap_destroy(vin->capabilities);
#endif
			ast_free(vin->name);
			ast_free(vin->path);
			ast_free(vin->firmware);
			ast_free(vin->almab);
			ast_free(vin->almcd);
			ast_free(vin->cram);
			ast_free(vin);
		}
		// free dynamic allocated memory
		ast_free(brd->name);
		ast_free(brd->path);
		ast_free(brd->type);
		ast_free(brd);
	}
	// close General Database
	if (pg_gen_db)
		sqlite3_close(pg_gen_db);
	// close CDR Database
	if (pg_cdr_db)
		sqlite3_close(pg_cdr_db);
	// close SMS Database
	if (pg_sms_db)
		sqlite3_close(pg_sms_db);
	// close SIM card Database
	if (pg_sim_db)
		sqlite3_close(pg_sim_db);
	// unregister atexit function
	if (pg_atexit_registered) ast_unregister_atexit(pg_atexit);

	ast_mutex_unlock(&pg_lock);
}
//------------------------------------------------------------------------------
// end of pg_cleanup()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_atexit()
//------------------------------------------------------------------------------
static void pg_atexit(void)
{
	pg_atexit_registered = 0;

	// perform module cleanup
	pg_cleanup();
}
//------------------------------------------------------------------------------
// end of pg_atexit()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_load()
//------------------------------------------------------------------------------
static int pg_load(void)
{
	FILE *fp = NULL;
	char buf[256];
	char type[64];
	char name[64];
	char sim[64];
	int res;
	char *cp;

	unsigned int pos;
	unsigned int index;
	unsigned int vin_num;
	char vc_type[4];
	unsigned int vc_slot;
	unsigned int vio;

	char path[PATH_MAX];
	
	struct pg_board *brd;
	struct pg_channel_gsm *ch_gsm;
	struct pg_trunk_gsm_channel_gsm_fold *ch_gsm_fold;
	struct pg_trunk_gsm *tr_gsm;
	struct pg_vinetic *vin;
	struct pg_channel_rtp *rtp;

	struct ast_config *ast_cfg;
	struct ast_flags ast_cfg_flags;
	struct ast_variable *ast_var;
	const char *cvar;

	unsigned int brd_num;
	unsigned int pwr_seq_num;

	ast_mutex_lock(&pg_lock);

	ast_verbose("Polygator: module \"%s\" loading...\n", VERSION);
	gettimeofday(&pg_start_time, NULL);
	// retrieve configuration from file
	ast_cfg_flags.flags = 0;
	ast_cfg = ast_config_load(pg_config_file, ast_cfg_flags);
	// register atexit function
	if (ast_register_atexit(pg_atexit) < 0) {
		ast_log(LOG_ERROR, "unable to register atexit function\n");
		goto pg_load_error;
	}
	pg_atexit_registered = 1;
	// open SIM card database
	sprintf(path, "%s/polygator-sim.db", ast_config_AST_LOG_DIR);
	res = sqlite3_open(path, &pg_sim_db);
	if (res != SQLITE_OK) {
		ast_log(LOG_ERROR, "could not open Polygator SIM card database \"%s\"\n", path);
		goto pg_load_error;
	}
	pg_sim_db_table_create(NULL);
	// open General database
	sprintf(path, "%s/polygator-gen.db", ast_config_AST_LOG_DIR);
	res = sqlite3_open(path, &pg_gen_db);
	if (res != SQLITE_OK) {
		ast_log(LOG_ERROR, "could not open Polygator General database \"%s\"\n", path);
		goto pg_load_error;
	}
	// open CDR database
	sprintf(path, "%s/polygator-cdr.db", ast_config_AST_LOG_DIR);
	res = sqlite3_open(path, &pg_cdr_db);
	if (res != SQLITE_OK) {
		ast_log(LOG_ERROR, "could not open Polygator CDR database \"%s\"\n", path);
		goto pg_load_error;
	}
	pg_cdr_table_create(NULL);
	// open SMS database
	sprintf(path, "%s/polygator-sms.db", ast_config_AST_LOG_DIR);
	res = sqlite3_open(path, &pg_sms_db);
	if (res != SQLITE_OK) {
		ast_log(LOG_ERROR, "could not open Polygator SMS database \"%s\"\n", path);
		goto pg_load_error;
	}
	// scan polygator subsystem
	snprintf(path, PATH_MAX, "/dev/%s", "polygator/subsystem");
	if ((fp = fopen(path, "r"))) {
		while (fgets(buf, sizeof(buf), fp))
		{
			if (sscanf(buf, "%[0-9a-z-] %[0-9a-z/!-]", type, name) == 2) {
				str_xchg(name, '!', '/');
				cp =  strrchr(name, '/');
				ast_verbose("Polygator: found board type=\"%s\" name=\"%s\"\n", type, (cp)?(cp+1):(name));
				if (!(brd = ast_calloc(1, sizeof(struct pg_board)))) {
					ast_log(LOG_ERROR, "can't get memory for struct pg_board\n");
					goto pg_load_error;
				}
				// add board into general board list
				AST_LIST_INSERT_TAIL(&pg_general_board_list, brd, pg_general_board_list_entry);
				// init board
				ast_mutex_init(&brd->lock);
				brd->type = ast_strdup(type);
				brd->name = ast_strdup((cp)?(cp+1):(name));
				snprintf(path, PATH_MAX, "/dev/%s", name);
				brd->path = ast_strdup(path);
			}
		}
		fclose(fp);
		fp = NULL;
	} else {
		if (errno != ENOENT) {
			ast_log(LOG_ERROR, "unable to scan Polygator subsystem: %d %s\n", errno, strerror(errno));
			goto pg_load_error;
		} else
			ast_verbose("Polygator: subsystem not found\n");
	}
	// scan polygator board
	brd_num = 0;
	pwr_seq_num = 0;
	AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
	{
		if (!(fp = fopen(brd->path, "r"))) {
			ast_log(LOG_ERROR, "unable to scan Polygator board \"%s\": %s\n", brd->name, strerror(errno));
			goto pg_load_error;
		}
		while (fgets(buf, sizeof(buf), fp))
		{
			if (sscanf(buf, "GSM%u %[0-9A-Za-z-] %[0-9A-Za-z/!-] %[0-9A-Za-z/!-] VIN%u%[ACMLP]%u VIO=%u", &pos, type, name, sim, &vin_num, vc_type, &vc_slot, &vio) == 8) {
				snprintf(buf, sizeof(buf), "%s-gsm%u", brd->name, pos);
				ast_verbose("Polygator: found GSM channel=\"%s\"\n", buf);
				if (!(ch_gsm = ast_calloc(1, sizeof(struct pg_channel_gsm)))) {
					ast_log(LOG_ERROR, "can't get memory for struct pg_channel_gsm\n");
					goto pg_load_error;
				}
				pg_gsm_tech_neededed = 1;
				// add channel into board channel list
				AST_LIST_INSERT_TAIL(&brd->channel_gsm_list, ch_gsm, pg_board_channel_gsm_list_entry);
				// add channel into general channel list
				AST_LIST_INSERT_TAIL(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry);
				// init GSM channel
				ast_mutex_init(&ch_gsm->lock);
				ch_gsm->thread = AST_PTHREADT_NULL;
				ch_gsm->position_on_board = pos;
				ch_gsm->board = brd;
				ch_gsm->device = ast_strdup(buf);
				snprintf(buf, sizeof(buf), "chan-%u%u", brd_num, pos);
				ch_gsm->alias = ast_strdup(buf);
				str_xchg(name, '!', '/');
				snprintf(path, sizeof(path), "/dev/%s", name);
				ch_gsm->tty_path = ast_strdup(path);
				str_xchg(sim, '!', '/');
				snprintf(path, sizeof(path), "/dev/%s", sim);
				ch_gsm->sim_path = ast_strdup(path);
				ch_gsm->gsm_module_type = pg_gsm_module_type_get(type);
				ch_gsm->at_pipe[0] = ch_gsm->at_pipe[1] = -1;
				ch_gsm->ussd_pipe[0] = ch_gsm->ussd_pipe[1] = -1;
				ch_gsm->vinetic_number = vin_num;
				ch_gsm->vinetic_alm_slot = -1;
				ch_gsm->vinetic_pcm_slot = -1;
				if (!strcasecmp(vc_type, "ALM"))
					ch_gsm->vinetic_alm_slot = vc_slot;
				else if (!strcasecmp(vc_type, "PCM"))
					ch_gsm->vinetic_pcm_slot = vc_slot;
				// reset registaration status
				ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
				// reset callwait state
				ch_gsm->callwait = PG_CALLWAIT_STATE_UNKNOWN;
				// reset clir state
				ch_gsm->clir = PG_CLIR_STATE_UNKNOWN;
				// reset rssi value
				ch_gsm->rssi = 99;
				// reset ber value
				ch_gsm->ber = 99;
				// reset power sequence number
				ch_gsm->power_sequence_number = -1;
				// get config variables
				// alias
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "alias"))) {
					ast_free(ch_gsm->alias);
					ch_gsm->alias = ast_strdup(cvar);
				}
				// enable
				ch_gsm->config.enable = 0;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "enable")))
					ch_gsm->config.enable = -ast_true(cvar);
				// pin
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "pin")))
					ch_gsm->pin = ast_strdup(cvar);
				// baudrate
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "baudrate")) && (is_str_digit(cvar)))
					ch_gsm->config.baudrate = atoi(cvar);
				if (!ch_gsm->config.baudrate) ch_gsm->config.baudrate = 115200;
				// regattempt
				ch_gsm->config.reg_try_count = PG_REG_TRY_COUNT_DEFAULT;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "regattempt"))) {
					if (is_str_digit(cvar) || ((*cvar == '-') && (is_str_digit(cvar+1))))
						ch_gsm->config.reg_try_count = atoi(cvar);
					else if (!strcasecmp(cvar, "forever"))
						ch_gsm->config.reg_try_count = -1;
				}
				// callwait
				ch_gsm->config.callwait = PG_CALLWAIT_STATE_DISABLE;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "callwait")))
					ch_gsm->config.callwait = -ast_true(cvar);
				if ((ch_gsm->config.callwait == PG_CALLWAIT_STATE_UNKNOWN) || (ch_gsm->config.callwait == PG_CALLWAIT_STATE_QUERY))
					ch_gsm->config.callwait = PG_CALLWAIT_STATE_DISABLE;
				// clir
				ch_gsm->config.clir = PG_CLIR_STATE_SUBSCRIPTION;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "clir")))
					ch_gsm->config.clir = pg_get_clir_state(cvar);
				if ((ch_gsm->config.clir == PG_CLIR_STATE_UNKNOWN) || (ch_gsm->config.clir == PG_CLIR_STATE_QUERY))
					ch_gsm->config.clir = PG_CLIR_STATE_SUBSCRIPTION;
				// outgoing
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "outgoing")))
					ch_gsm->config.outgoing_type = pg_get_gsm_call_outgoing(cvar);
				if (ch_gsm->config.outgoing_type == PG_CALL_GSM_OUTGOING_TYPE_UNKNOWN)
					ch_gsm->config.outgoing_type = PG_CALL_GSM_OUTGOING_TYPE_ALLOW;
				// incoming
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "incoming")))
					ch_gsm->config.incoming_type = pg_get_call_gsm_incoming_type(cvar);
				if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_UNKNOWN)
					ch_gsm->config.incoming_type = PG_CALL_GSM_INCOMING_TYPE_SPEC;
				// incomingto
				if (ch_gsm->config.incoming_type == PG_CALL_GSM_INCOMING_TYPE_SPEC) {
					ast_copy_string(ch_gsm->config.gsm_call_extension, "s", sizeof(ch_gsm->config.gsm_call_extension));
					if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "incomingto")))
						ast_copy_string(ch_gsm->config.gsm_call_extension, cvar, sizeof(ch_gsm->config.gsm_call_extension));
				} else
					ch_gsm->config.callwait = PG_CALLWAIT_STATE_DISABLE;
				// dcrttl
				ch_gsm->config.dcrttl = 604800;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "dcrttl")) && is_str_digit(cvar))
					ch_gsm->config.dcrttl = atoi(cvar);
				if (ch_gsm->config.dcrttl < 60)
					ch_gsm->config.dcrttl = 60;
				// context
				ast_copy_string(ch_gsm->config.gsm_call_context, "default", sizeof(ch_gsm->config.gsm_call_context));
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "context")))
					ast_copy_string(ch_gsm->config.gsm_call_context, cvar, sizeof(ch_gsm->config.gsm_call_context));
				// progress
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "progress")))
					ch_gsm->config.progress = pg_get_gsm_call_progress(cvar);
				if (ch_gsm->config.progress == PG_GSM_CALL_PROGRESS_TYPE_UNKNOWN)
					ch_gsm->config.progress = PG_GSM_CALL_PROGRESS_TYPE_ANSWER;
				// language
				ast_copy_string(ch_gsm->config.language, "en", sizeof(ch_gsm->config.language));
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "language")))
					ast_copy_string(ch_gsm->config.language, cvar, sizeof(ch_gsm->config.language));
				// get mohinterpret string
				ast_copy_string(ch_gsm->config.mohinterpret, "default", sizeof(ch_gsm->config.mohinterpret));
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "mohinterpret")))
					ast_copy_string(ch_gsm->config.mohinterpret, cvar, sizeof(ch_gsm->config.mohinterpret));
				// gainin
				ch_gsm->config.gainin = 80;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "gainin")) && (is_str_digit(cvar))) {
					ch_gsm->config.gainin = atoi(cvar);
					if (ch_gsm->config.gainin < 0) ch_gsm->config.gainin = 0;
					if (ch_gsm->config.gainin > 100) ch_gsm->config.gainin = 100;
				}
				ch_gsm->gainin = ch_gsm->config.gainin;
				// gainout
				ch_gsm->config.gainout = 7;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "gainout")) && (is_str_digit(cvar))) {
					ch_gsm->config.gainout = atoi(cvar);
					if (ch_gsm->config.gainout < 0) ch_gsm->config.gainout = 0;
					if (ch_gsm->config.gainout > 15) ch_gsm->config.gainout = 15;
				}
				ch_gsm->gainout = ch_gsm->config.gainout;
				// gain1 - level voice channel CODER: sig -> host
				if ((!(cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "gain1"))) ||
						(!is_str_xdigit(cvar)) ||
							(sscanf(cvar, "%02X", &ch_gsm->config.gain1) != 1))
					ch_gsm->config.gain1 = 0x60;
				// gain2 - level voice channel CODER: host -> sig
				if ((!(cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "gain2"))) ||
						(!is_str_xdigit(cvar)) ||
							(sscanf(cvar, "%02X", &ch_gsm->config.gain2) != 1))
					ch_gsm->config.gain2 = 0x60;
				// gainx - level voice channel ALI: analog -> sig
				if ((!(cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "gainx"))) ||
						(!is_str_xdigit(cvar)) ||
							(sscanf(cvar, "%02X", &ch_gsm->config.gainx) != 1))
					ch_gsm->config.gainx = 0x60;
				// gainr - level voice channel ALI: sig -> analog
				if ((!(cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "gainr"))) ||
						(!is_str_xdigit(cvar)) ||
							(sscanf(cvar, "%02X", &ch_gsm->config.gainr) != 1))
					ch_gsm->config.gainr = 0x60;
				// SMS
				// sms.send.interval
				ch_gsm->config.sms_send_interval.tv_sec = 20;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "sms.send.interval")) && (is_str_digit(cvar)))
					ch_gsm->config.sms_send_interval.tv_sec = atoi(cvar);
				if (ch_gsm->config.sms_send_interval.tv_sec < 10)
					ch_gsm->config.sms_send_interval.tv_sec = 10;
				// sms.max.attempt
				ch_gsm->config.sms_send_attempt = 2;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "sms.send.attempt")) && (is_str_digit(cvar)))
					ch_gsm->config.sms_send_attempt = atoi(cvar);
				if (ch_gsm->config.sms_send_attempt < 1)
					ch_gsm->config.sms_send_attempt = 1;
				// sms.max.part
				ch_gsm->config.sms_max_part = 2;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "sms.max.part")) && (is_str_digit(cvar)))
					ch_gsm->config.sms_max_part = atoi(cvar);
				if (ch_gsm->config.sms_max_part < 1)
					ch_gsm->config.sms_max_part = 1;
				// sms.notify.enable
				ch_gsm->config.sms_notify_enable = 0;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "sms.notify.enable")))
					ch_gsm->config.sms_notify_enable = -ast_true(cvar);
				// sms.notify.context
				ast_copy_string(ch_gsm->config.sms_notify_context, "default", sizeof(ch_gsm->config.sms_notify_context));
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "sms.notify.context")))
					ast_copy_string(ch_gsm->config.sms_notify_context, cvar, sizeof(ch_gsm->config.sms_notify_context));
				// sms.notify.extension
				ast_copy_string(ch_gsm->config.sms_notify_extension, "s", sizeof(ch_gsm->config.sms_notify_extension));
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "sms.notify.extension")))
					ast_copy_string(ch_gsm->config.sms_notify_extension, cvar, sizeof(ch_gsm->config.sms_notify_extension));

				// trunk
				if (ast_cfg) {
					ast_var = ast_variable_browse(ast_cfg, ch_gsm->device);
					while (ast_var)
					{
						if (!strcmp(ast_var->name, "trunk")) {
							// search trunk gsm
							tr_gsm = pg_get_trunk_gsm_by_name(ast_var->value);
							if (!tr_gsm) {  
								// create trunk storage
								if (!(tr_gsm = ast_calloc(1, sizeof(struct pg_trunk_gsm)))) {
									ast_var = ast_var->next;
									continue;
								}
								tr_gsm->name = ast_strdup(ast_var->value);
								AST_LIST_INSERT_TAIL(&pg_general_trunk_gsm_list, tr_gsm, pg_general_trunk_gsm_list_entry);
								// init trunk channel list
								AST_LIST_HEAD_SET_NOLOCK(&tr_gsm->channel_gsm_list, NULL);
								tr_gsm->channel_gsm_last = NULL;
								ast_verbose("Polygator: registered GSM trunk=\"%s\"\n", tr_gsm->name);
							}
							if (tr_gsm) {
								// check for channel already in trunk
								if (!pg_get_channel_gsm_fold_from_trunk_by_name(tr_gsm, ch_gsm->alias)) {
									// create trunk channel gsm fold
									if ((ch_gsm_fold = ast_calloc(1, sizeof(struct pg_trunk_gsm_channel_gsm_fold)))) {
										ch_gsm_fold->name = ast_strdup(ast_var->value);
										// set channel pointer
										ch_gsm_fold->channel_gsm = ch_gsm;
										// add entry into trunk list
										AST_LIST_INSERT_TAIL(&tr_gsm->channel_gsm_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_trunk_list_entry);
										// add entry into channel list
										AST_LIST_INSERT_TAIL(&ch_gsm->trunk_list, ch_gsm_fold, pg_trunk_gsm_channel_gsm_fold_channel_list_entry);
									}
								}
							}
						}
						ast_var = ast_var->next;
					}
				}
				// trunkonly
				ch_gsm->config.trunkonly = 0;
				if (ch_gsm->trunk_list.first) {
					if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "trunkonly")))
						ch_gsm->config.trunkonly = -ast_true(cvar);
				}
				// confallow
				ch_gsm->config.conference_allowed = 0;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "confallow")))
					ch_gsm->config.conference_allowed = -ast_true(cvar);

				// ali.nelec
				ch_gsm->config.ali_nelec = VIN_EN;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "ali.nelec")))
					ch_gsm->config.ali_nelec = str_true(cvar)?VIN_EN:VIN_DIS;

				// ali.nelec.tm
				ch_gsm->config.ali_nelec_tm = VIN_DTM_ON;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "ali.nelec.tm")))
					ch_gsm->config.ali_nelec_tm = str_true(cvar)?VIN_DTM_ON:VIN_DTM_OFF;

				// ali.nelec.oldc
				ch_gsm->config.ali_nelec_oldc = VIN_OLDC_ZERO;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "ali.nelec.oldc"))) {
					if (!strcmp(cvar, "oldc"))
						ch_gsm->config.ali_nelec_oldc = VIN_OLDC_NO;
					else
						ch_gsm->config.ali_nelec_oldc = VIN_OLDC_ZERO;
				}

				// ali.nelec.as
				ch_gsm->config.ali_nelec_as = VIN_AS_RUN;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "ali.nelec.as")))
					ch_gsm->config.ali_nelec_as = str_true(cvar)?VIN_AS_RUN:VIN_AS_STOP;

				// ali.nelec.nlp
				ch_gsm->config.ali_nelec_nlp = VIN_ON;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "ali.nelec.nlp")))
					ch_gsm->config.ali_nelec_nlp = str_true(cvar)?VIN_ON:VIN_OFF;

				// ali.nelec.nlpm
				ch_gsm->config.ali_nelec_nlpm = VIN_NLPM_SIGN_NOISE;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "ali.nelec.nlpm"))) {
					ch_gsm->config.ali_nelec_nlpm = pg_vinetic_get_ali_nelec_nlpm(cvar);
					if (ch_gsm->config.ali_nelec_nlpm < 0)
						ch_gsm->config.ali_nelec_nlpm = VIN_NLPM_SIGN_NOISE;
				}

				if (ch_gsm->config.enable) {
					// start GSM channel workthread
					ch_gsm->flags.enable = 1;
					ch_gsm->power_sequence_number = pwr_seq_num++;
					if (ast_pthread_create_detached(&ch_gsm->thread, NULL, pg_channel_gsm_workthread, ch_gsm) < 0) {
						ast_log(LOG_ERROR, "can't start workthread for GSM channel=\"%s\"\n", ch_gsm->alias);
						ch_gsm->flags.enable = 0;
						ch_gsm->thread = AST_PTHREADT_NULL;
						goto pg_load_error;
					}
				}
			} else if (sscanf(buf, "VIN%uRTP%u %[0-9A-Za-z/!-]", &index, &pos, name) == 3) {
				str_xchg(name, '!', '/');
				cp =  strrchr(name, '/');
				ast_verbose("Polygator: found RTP channel=\"%s\"\n", (cp)?(cp+1):(name));
				if ((vin = pg_get_vinetic_from_board(brd, index))) {
					if (!(rtp = ast_calloc(1, sizeof(struct pg_channel_rtp)))) {
						ast_log(LOG_ERROR, "can't get memory for struct pg_channel_rtp\n");
						goto pg_load_error;
					}
					// add RTP channel into vinetic RTP channel list
					AST_LIST_INSERT_TAIL(&vin->channel_rtp_list, rtp, pg_vinetic_channel_rtp_list_entry);
					// init RTP channel
					ast_mutex_init(&rtp->lock);
					rtp->vinetic = vin;
					rtp->position_on_vinetic = pos;
					rtp->name = ast_strdup((cp)?(cp+1):(name));
					snprintf(path, sizeof(path), "/dev/%s", name);
					rtp->path = ast_strdup(path);
				} else 
					ast_log(LOG_WARNING, "PG board=\"%s\": VIN%u not found\n", brd->name, index);
			} else if (sscanf(buf, "VIN%u %[0-9A-Za-z/!-]", &pos, name) == 2) {
				str_xchg(name, '!', '/');
				cp =  strrchr(name, '/');
				ast_verbose("Polygator: found vinetic=\"%s\"\n", (cp)?(cp+1):(name));
				if (!(vin = ast_calloc(1, sizeof(struct pg_vinetic)))) {
					ast_log(LOG_ERROR, "can't get memory for struct pg_vinetic\n");
					goto pg_load_error;
				}
				// add vinetic into board vinetic list
				AST_LIST_INSERT_TAIL(&brd->vinetic_list, vin, pg_board_vinetic_list_entry);
				// init VINETIC
				ast_mutex_init(&vin->lock);
				vin->position_on_board = pos;
				vin->board = brd;
				vin->name = ast_strdup((cp)?(cp+1):(name));
				snprintf(path, sizeof(path), "/dev/%s", name);
				vin->path = ast_strdup(path);
				// firmware
				if ((cvar = pg_get_config_variable(ast_cfg, vin->name, "firmware")))
					vin->firmware = ast_strdup(cvar);
				if (!vin->firmware) vin->firmware = ast_strdup("RTP_0_15_56_V14");
				// almab
				if ((cvar = pg_get_config_variable(ast_cfg, vin->name, "almab")))
					vin->almab = ast_strdup(cvar);
				if (!vin->almab) vin->almab = ast_strdup("ALM_2484_AB_01.dwl");
				// almcd
				if ((cvar = pg_get_config_variable(ast_cfg, vin->name, "almcd")))
					vin->almcd = ast_strdup(cvar);
				if (!vin->almcd) vin->almcd = ast_strdup("ALM_2484_CD_01.dwl");
				// cram
				if ((cvar = pg_get_config_variable(ast_cfg, vin->name, "cram")))
					vin->cram = ast_strdup(cvar);
				if (!vin->cram) vin->cram = ast_strdup("cram.byt");
				vin->run = 1;
				vin->thread = AST_PTHREADT_NULL;
				vin->state = PG_VINETIC_STATE_INIT;
				AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
				{
					ast_mutex_lock(&ch_gsm->lock);
					if ((!strcmp(vin->board->name, ch_gsm->board->name)) &&
							(vin->position_on_board == ch_gsm->vinetic_number) &&
								(ch_gsm->vinetic_alm_slot >= 0)) {
						vin->patch_alm_gsm[ch_gsm->vinetic_alm_slot] = 1;
					}
					ast_mutex_unlock(&ch_gsm->lock);
				}
				// start vinetic workthread
				if (ast_pthread_create_detached(&vin->thread, NULL, pg_vinetic_workthread, vin) < 0) {
					ast_log(LOG_ERROR, "can't start workthread for vinetic=\"%s\"\n", vin->name);
					vin->thread = AST_PTHREADT_NULL;
					goto pg_load_error;
				}
			}
		}
		fclose(fp);
		fp = NULL;
		brd_num++;
	}

	if (pg_gsm_tech_neededed) {
		// registering channel class PGGSM in asterisk PBX
		if (ast_channel_register(&pg_gsm_tech)) {
			ast_log(LOG_ERROR, "unable to register channel class 'PGGSM'\n");
			goto pg_load_error;
		}
		pg_gsm_tech_registered = 1;

#if ASTERISK_VERSION_NUM >= 100000
		pg_gsm_tech.capabilities = ast_format_cap_alloc();
#endif
	}

	// registering Polygator CLI interface
	ast_cli_register_multiple(pg_cli, sizeof(pg_cli)/sizeof(pg_cli[0]));
	ast_verbose("Polygator: CLI registered\n");
	pg_cli_registered = 1;

	// destroy configuration environments
	if (ast_cfg) ast_config_destroy(ast_cfg);

	ast_verbose("Polygator: module loaded successfull\n");
	ast_mutex_unlock(&pg_lock);
	return AST_MODULE_LOAD_SUCCESS;

pg_load_error:

	// perform module cleanup
	pg_cleanup();

	// close current open file
	if (fp) fclose(fp);

	// destroy configuration environment
	if (ast_cfg) ast_config_destroy(ast_cfg);

	ast_mutex_unlock(&pg_lock);
	return AST_MODULE_LOAD_FAILURE;
}
//------------------------------------------------------------------------------
// end of pg_load()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_unload()
//------------------------------------------------------------------------------
static int pg_unload(void)
{
	// perform module cleanup
	pg_cleanup();

	ast_verbose("Polygator: module unloaded successfull\n");
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_unload()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
AST_MODULE_INFO(
	ASTERISK_GPL_KEY,
	AST_MODFLAG_DEFAULT,
	"Polygator",
	.load = pg_load,
	.unload = pg_unload,
);
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of chan_polygator.c                                                    */
/******************************************************************************/
