/******************************************************************************/
/* chan_pg.c                                                                  */
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

#include "asterisk/channel.h"
#include "asterisk/cli.h"
#include "asterisk/frame.h"
// #include "asterisk/lock.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"
// #include "asterisk/utils.h"
#include "asterisk/version.h"

#include "polygator/polygator-base.h"

#include "libvinetic.h"

#include "address.h"
#include "at.h"
#include "strutil.h"
#include "x_timer.h"

struct pg_at_cmd {
	struct at_command *at;
	int id;
	u_int32_t oper;
	int sub_cmd;
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

	// vinetic firmware
	char *firmware;
	char *almab;
	char *almcd;
	char *cram;

	int run;
	int state;

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

	unsigned int pos_on_vinetic;
	struct pg_vinetic *vinetic;

	int state;

	struct vinetic_context context;

	// entry for board vinetic list
	AST_LIST_ENTRY(pg_channel_rtp) pg_vinetic_channel_rtp_list_entry;
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
};

struct pg_channel_gsm {
	// GSM channel private lock
	ast_mutex_t lock;
	pthread_t thread;

	char *device;
	char *tty_path;
	int tty_fd;
	unsigned int gsm_module_type;	// type of GSM module ("SIM300", "M10", "SIM900", "SIM5215")

	unsigned int position_on_board;
	struct pg_board *board;
	
	char *alias;

	// configuration
	struct pg_channel_gsm_config {
		unsigned int enable:1;
		int baudrate;
		char language[MAX_LANGUAGE];
		char mohinterpret[MAX_MUSICCLASS];
	} config;

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
		unsigned int sim_present:1;
		unsigned int sim_startup:1;
		unsigned int pin_required:1;
		unsigned int puk_required:1;
		unsigned int pin_accepted:1;
		unsigned int func_test_done:1;
		unsigned int func_test_run:1;
	} flags;

	// timers
	struct pg_channel_gsm_timers {
		struct x_timer waitrdy;
		struct x_timer waitsuspend;
		struct x_timer testfun;
		struct x_timer testfunsend;
		struct x_timer callready;
		struct x_timer runhalfsecond;
		struct x_timer runonesecond;
		struct x_timer runfivesecond;
		struct x_timer runhalfminute;
		struct x_timer runoneminute;
		struct x_timer waitviodown;
		struct x_timer testviodown;
		struct x_timer dial;
		struct x_timer smssend;
		struct x_timer simpoll;
		struct x_timer pinwait;
	} timers;

	// Runtime data
	unsigned int power_sequence_number;
	int reg_stat; /* Registration status */
	int reg_stat_old; /* Registration status */
	int state;
	int vio;
	int baudrate;
	AST_LIST_HEAD_NOLOCK(cmd_queue, pg_at_cmd) cmd_queue; /* AT-command queue */
	int cmd_queue_length; /* AT-command queue */
	int cmd_done; /* AT-command queue */

	// entry for board channel list
	AST_LIST_ENTRY(pg_channel_gsm) pg_board_channel_gsm_list_entry;
	// entry for general channel list
	AST_LIST_ENTRY(pg_channel_gsm) pg_general_channel_gsm_list_entry;
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
static const struct ast_channel_tech pg_gsm_tech = {
#endif
	.type = "PGGSM",
	.description = "Polygator GSM",
#if ASTERISK_VERSION_NUM < 100000
	.capabilities = AST_FORMAT_ULAW
					|AST_FORMAT_ALAW
					/*|AST_FORMAT_G723_1*/
					|AST_FORMAT_G726
					|AST_FORMAT_G729A
					/*|AST_FORMAT_ILBC*/,
#endif
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
static char *pg_cli_show_board(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_boards(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_config_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char pg_cli_channel_gsm_actions_usage[256];
static char pg_cli_config_actions_usage[256];
static struct ast_cli_entry pg_cli[] = {
	AST_CLI_DEFINE(pg_cli_show_modinfo, "Show PG module information"),
	AST_CLI_DEFINE(pg_cli_show_board, "Show PG board information"),
	AST_CLI_DEFINE(pg_cli_show_boards, "Show PG boards information summary"),
	AST_CLI_DEFINE(pg_cli_show_channels, "Show PG channels information summary"),
	AST_CLI_DEFINE(pg_cli_config_actions, "Save Polygator configuration"),
	AST_CLI_DEFINE(pg_cli_channel_gsm_actions, "Perform actions on GSM channel"),
};

// eggsm CLI GSM channel actions
static char *pg_cli_channel_gsm_action_power(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_channel_gsm_action_enable_disable(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static struct pg_cli_action pg_cli_channel_gsm_action_handlers[] = {
	PG_CLI_ACTION("power", pg_cli_channel_gsm_action_power),
	PG_CLI_ACTION("enable", pg_cli_channel_gsm_action_enable_disable),
	PG_CLI_ACTION("disable", pg_cli_channel_gsm_action_enable_disable),
	PG_CLI_ACTION("restart", pg_cli_channel_gsm_action_enable_disable),
};

// GSM module types
struct pg_generic_param pg_gsm_module_types[] = {
	PG_GENERIC_PARAM("SIM300", POLYGATOR_MODULE_TYPE_SIM300),
	PG_GENERIC_PARAM("SIM900", POLYGATOR_MODULE_TYPE_SIM900),
	PG_GENERIC_PARAM("M10", POLYGATOR_MODULE_TYPE_M10),
	PG_GENERIC_PARAM("SIM5215", POLYGATOR_MODULE_TYPE_SIM5215),
};

static struct timeval waitrdy_timeout = {20, 0};
static struct timeval waitsuspend_timeout = {30, 0};
static struct timeval testfun_timeout = {10, 0};
static struct timeval testfunsend_timeout = {1, 0};
static struct timeval callready_timeout = {300, 0};
static struct timeval runhalfsecond_timeout = {0, 500000};
static struct timeval runonesecond_timeout = {1, 0};
static struct timeval runfivesecond_timeout = {5, 0};
static struct timeval halfminute_timeout = {30, 0};
static struct timeval runoneminute_timeout = {60, 0};
static struct timeval waitviodown_timeout = {20, 0};
static struct timeval testviodown_timeout = {1, 0};
static struct timeval onesec_timeout = {1, 0};
static struct timeval zero_timeout = {0, 1000};
static struct timeval simpoll_timeout = {2, 0};
static struct timeval pinwait_timeout = {8, 0};

static char pg_config_file[] = "polygator.conf";

static int pg_at_response_timeout = 2;

AST_MUTEX_DEFINE_STATIC(pg_lock);

static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_board_list, pg_board);
static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_channel_gsm_list, pg_channel_gsm);

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
// pg_gsm_module_type_get()
//------------------------------------------------------------------------------
static unsigned int pg_gsm_module_type_get(const char *module_type)
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
		default: return "unknown";
	}
}
//------------------------------------------------------------------------------
// end of pg_cahnnel_gsm_state_to_string()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_vio_get()
//------------------------------------------------------------------------------
static int pg_channel_gsm_vio_get(struct pg_channel_gsm *ch_gsm)
{
	char path [PATH_MAX];
	FILE *fp;
	char buf[256];
	char name[64];
	char type[64];
	unsigned int pos;
	unsigned int vio;
	int res = -1;

	if (ch_gsm) {
		snprintf(path, sizeof(path), "/dev/polygator/%s", ch_gsm->board->name);
		if ((fp = fopen(path, "r"))) {
			while (fgets(buf, sizeof(buf), fp))
			{
				if (sscanf(buf, "GSM%u %[0-9A-Za-z-] %[0-9A-Za-z-] VIO=%u", &pos, name, type, &vio) == 4) {
					if (pos == ch_gsm->position_on_board) {
						res = vio;
						break;
					}
				}
			}
			fclose(fp);
		}
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
	char path [PATH_MAX];
	FILE *fp;
	int res = -1;

	if (ch_gsm) {
		snprintf(path, sizeof(path), "/dev/polygator/%s", ch_gsm->board->name);
		if ((fp = fopen(path, "w"))) {
			if (fprintf(fp, "GSM%u PWR=%d", ch_gsm->position_on_board, state) >= 0)
				res = 0;
			fclose(fp);
		}
	} else
		errno = ENODEV;

	return res;
}
//------------------------------------------------------------------------------
// pg_channel_gsm_power_set()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_channel_gsm_key_press()
//------------------------------------------------------------------------------
static int pg_channel_gsm_key_press(struct pg_channel_gsm *ch_gsm, int state)
{
	char path [PATH_MAX];
	FILE *fp;
	int res = -1;

	if (ch_gsm) {
		snprintf(path, sizeof(path), "/dev/polygator/%s", ch_gsm->board->name);
		if ((fp = fopen(path, "w"))) {
			if (fprintf(fp, "GSM%u KEY=%d", ch_gsm->position_on_board, state) >= 0)
				res = 0;
			fclose(fp);
		}
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
// pg_atcommand_queue_free()
//------------------------------------------------------------------------------
static inline void pg_atcommand_queue_free(struct pg_channel_gsm* ch_gsm, struct pg_at_cmd *cmd)
{
	if (cmd) {
		ast_free(cmd);
		ch_gsm->cmd_queue_length--;
	}
}
//------------------------------------------------------------------------------
// end of pg_atcommand_queue_free()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_atcommand_queue_flush()
//------------------------------------------------------------------------------
static inline void pg_atcommand_queue_flush(struct pg_channel_gsm* ch_gsm)
{
	struct pg_at_cmd *cmd;
	
	while ((cmd = AST_LIST_REMOVE_HEAD(&ch_gsm->cmd_queue, entry)))
		pg_atcommand_queue_free(ch_gsm, cmd);
}
//------------------------------------------------------------------------------
// end of pg_atcommand_queue_flush()
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
	int res;
	int r_len;
	char r_char;
	char *r_cptr;
	char r_buf[2048];
	int r_buf_len;
	size_t r_buf_valid;
	struct timeval timeout;
	struct ast_tm *tm_ptr, tm_buf;
	struct timeval curr_tv;
	struct timezone curr_tz;

	struct termios termios;
	speed_t baudrate;

	struct pg_channel_gsm *ch_gsm = (struct pg_channel_gsm *)data;

	ast_debug(4, "GSM channel=\"%s\": thread start\n", ch_gsm->alias);
	ast_verbose("Polygator: GSM channel=\"%s\" enabled\n", ch_gsm->alias);

	if (ch_gsm->power_sequence_number)
		usleep(750000 * ch_gsm->power_sequence_number);

	ast_mutex_lock(&ch_gsm->lock);

	// enable power suply
	if (!ch_gsm->flags.power) {
		if (pg_channel_gsm_power_set(ch_gsm, 1)) {
			ast_log(LOG_ERROR, "GSM channel=\"%s\": can't set GSM power suply to on: %s\n", ch_gsm->alias, strerror(errno));
			goto pg_channel_gsm_workthread_end;
		}
		ch_gsm->flags.power = 1;
		ast_mutex_unlock(&ch_gsm->lock);
		sleep(2);
		ast_mutex_lock(&ch_gsm->lock);
	}

	// open TTY device
	if ((ch_gsm->tty_fd = open(ch_gsm->tty_path, O_RDWR | O_NONBLOCK)) < 0) {
		ast_log(LOG_ERROR, "GSM channel=\"%s\": can't open \"%s\": %s\n", ch_gsm->alias, ch_gsm->tty_path, strerror(errno));
		ast_mutex_unlock(&ch_gsm->lock);
		goto pg_channel_gsm_workthread_end;
	}

	// set initial state
	ch_gsm->state = PG_CHANNEL_GSM_STATE_DISABLE;

	ast_mutex_unlock(&ch_gsm->lock);

	// init receiver
	r_buf_len = 0;
	r_cptr = r_buf;
	r_buf[0] = '\0';
	r_buf_valid = 0;

	while (ch_gsm->flags.enable)
	{
		gettimeofday(&curr_tv, &curr_tz);

		FD_ZERO(&rfds);
		FD_SET(ch_gsm->tty_fd, &rfds);

		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;

		res = ast_select(ch_gsm->tty_fd + 1, &rfds, NULL, NULL, &timeout);
		if (res > 0) {
			if (FD_ISSET(ch_gsm->tty_fd, &rfds)) {
				r_len = read(ch_gsm->tty_fd, &r_char, 1);
				if (r_len > 0) {
#if 0
					// print received symbol
					if (chnl->debug_ctl.rcvr) {
						chnl->rcvr_dbg_fp = fopen(chnl->rcvr_dbg_fname, "a+");
						if (chnl->rcvr_dbg_fp) {
							if (chr == '\n')
								fprintf(chnl->rcvr_dbg_fp, "LF\n");
							else if (chr == '\r')
								fprintf(chnl->rcvr_dbg_fp, "\nCR");
							else if (isprint(chr))
								fprintf(chnl->rcvr_dbg_fp, "%c", chr);
							else
								fprintf(chnl->rcvr_dbg_fp, "[%02x]", (unsigned char)chr);
							fclose(chnl->rcvr_dbg_fp);
							chnl->rcvr_dbg_fp = NULL;
						}
					}
#endif
					if ((r_char == '\n') || (r_char == '\r')) {
						// check for data in received buffer
						if (r_buf_len > 0)
							r_buf_valid = 1;
						else
							r_buf_valid = 0;
						// terminate end of received string
						*r_cptr = '\0';
						// set pointer to start buffer
						r_cptr = r_buf;
					} else if (r_char == '>') {
						// pdu prompt
#if 0
						if (chnl->debug_ctl.at) {
							chnl->at_dbg_fp = fopen(chnl->at_dbg_fname, "a+");
							if (chnl->at_dbg_fp) {
								if ((tm_ptr = ast_localtime(&curr_tv, &tm_buf, NULL)))
									fprintf(chnl->at_dbg_fp, "[%04d-%02d-%02d-%02d:%02d:%02d.%06ld] AT SEND PDU - [%s]\n",
															tm_ptr->tm_year + 1900,
															tm_ptr->tm_mon+1,
															tm_ptr->tm_mday,
															tm_ptr->tm_hour,
															tm_ptr->tm_min,
															tm_ptr->tm_sec,
															curr_tv.tv_usec,
															chnl->now_send_pdu_buf);
								else
									fprintf(chnl->at_dbg_fp, "[%ld.%06ld] AT SEND PDU - [%s]\n",
															curr_tv.tv_sec,
															curr_tv.tv_usec,
															chnl->now_send_pdu_buf);
								fclose(chnl->at_dbg_fp);
								chnl->at_dbg_fp = NULL;
							}
						}

						// Send SMS PDU
						write(chnl->dev_fd, chnl->now_send_pdu_buf, chnl->now_send_pdu_len);
						// Send control symbol Ctrl-Z
						write(chnl->dev_fd, (char *)&ctrlz, 1);
						chnl->recv_len = 0;
						chnl->recv_ptr = chnl->recv_buf;
						chnl->recv_buf[0] = '\0';
						chnl->recv_buf_valid = 0;
#endif
					} else {
						/* store current symbol into received buffer */
						*r_cptr++ = r_char;
						r_buf_len++;
						if (r_buf_len >= sizeof(r_buf)) {
							ast_log(LOG_WARNING, "GSM channel=\"%s\": AT-command buffer full\n", ch_gsm->alias);
							r_buf_len = 0;
							r_cptr = r_buf;
							r_buf[0] = '\0';
							r_buf_valid = 0;
						}
					}
				} else if (r_len < 0)
					ast_log(LOG_ERROR, "GSM channel=\"%s\": read error: %s\n", ch_gsm->alias, strerror(errno));
			}
		} else if (res < 0) {
			ast_log(LOG_ERROR, "GSM channel=\"%s\": select error: %s\n", ch_gsm->alias, strerror(errno));
			continue;
		}
		
		if (r_buf_valid) {
			ast_verbose("GSM channel=\"%s\": received=[%s]\n", ch_gsm->alias, r_buf);
			r_buf_len = 0;
			r_cptr = r_buf;
			r_buf[0] = '\0';
			r_buf_valid = 0;
		}

		ast_mutex_lock(&ch_gsm->lock);

		// handle timers
		// waitviodown
		if (is_x_timer_enable(ch_gsm->timers.waitviodown) && is_x_timer_fired(ch_gsm->timers.waitviodown)) {
			// waitviodown timer fired
			ast_log(LOG_ERROR, "GSM channel=\"%s\": power status VIO not turn to down\n", ch_gsm->alias);
			// stop waitviodown timer
			x_timer_stop(ch_gsm->timers.waitviodown);
			// stop testviodown timer
			x_timer_stop(ch_gsm->timers.testviodown);
			// shutdown channel
			ast_mutex_unlock(&ch_gsm->lock);
			goto pg_channel_gsm_workthread_end;
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
				// stop testviodown timer
				x_timer_stop(ch_gsm->timers.testviodown);
				// stop waitviodown timer
				x_timer_stop(ch_gsm->timers.waitviodown);
				//
				if (!ch_gsm->flags.restart_now) {
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
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
#if 0
			if(chnl->owner){
				// hangup - GSM
				if(chnl->module_type == EGGSM_MODULE_TYPE_SIM900)
					eggsm_atcommand_queue_append(chnl, AT_H, AT_OPER_EXEC, 0, at_resp_timeout, 0, "0");
				else
					eggsm_atcommand_queue_append(chnl, AT_H, AT_OPER_EXEC, 0, at_resp_timeout, 0, NULL);
				// hangup - asterisk core call
				eggsm_call_sm(chnl, CALL_MSG_RELEASE_IND, AST_CAUSE_NORMAL_CLEARING);
			}
			do{
				is_wait = 0;
				//
				if(chnl->owner){
					// unlock eggsm channel pvt
					ast_mutex_unlock(&chnl->pvt_lock);
					// unlock eggsm subsystem
					ast_mutex_unlock(&eggsm_lock);
					//
					usleep(1);
					is_wait = 1;
					// lock eggsm subsystem
					ast_mutex_lock(&eggsm_lock);
					// lock eggsm channel pvt
					ast_mutex_lock(&chnl->pvt_lock);
					}
			} while(is_wait);
#endif
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
			usleep(1000000);
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
#if 0
			if(chnl->owner){
				// hangup - GSM
				if(chnl->module_type == EGGSM_MODULE_TYPE_SIM900)
					eggsm_atcommand_queue_append(chnl, AT_H, AT_OPER_EXEC, 0, at_resp_timeout, 0, "0");
				else
					eggsm_atcommand_queue_append(chnl, AT_H, AT_OPER_EXEC, 0, at_resp_timeout, 0, NULL);
				// hangup - asterisk core call
				eggsm_call_sm(chnl, CALL_MSG_RELEASE_IND, AST_CAUSE_NORMAL_CLEARING);
				}
			do{
				is_wait = 0;
				//
				if(chnl->owner){
					// unlock eggsm channel pvt
					ast_mutex_unlock(&chnl->pvt_lock);
					// unlock eggsm subsystem
					ast_mutex_unlock(&eggsm_lock);
					//
					usleep(1);
					is_wait = 1;
					// lock eggsm subsystem
					ast_mutex_lock(&eggsm_lock);
					// lock eggsm channel pvt
					ast_mutex_lock(&chnl->pvt_lock);
					}
				}while(is_wait);
#endif
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
			usleep(1000000);
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
				ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				// reset registration status
				ch_gsm->reg_stat = REG_STAT_NOTREG_NOSEARCH;
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
				pg_atcommand_queue_flush(ch_gsm);
				ch_gsm->cmd_done = 1;
				if (tcflush(ch_gsm->tty_fd, TCIOFLUSH) < 0)
					ast_log(LOG_ERROR, "GSM channel=\"%s\": can't flush tty device: %s\n", ch_gsm->alias, strerror(errno));
				// set functionality test flags
				ch_gsm->flags.func_test_run = 0;
				ch_gsm->flags.func_test_done = 1;
				// check VIO status
				if ((ch_gsm->vio = pg_channel_gsm_vio_get(ch_gsm)) < 0) {
					ast_log(LOG_ERROR, "GSM channel=\"%s\": can't get channel power VIO status\n", ch_gsm->alias);
					ast_mutex_unlock(&ch_gsm->lock);
					goto pg_channel_gsm_workthread_end;
				}
				if (ch_gsm->vio) {
					// init test functionality
					ch_gsm->state = PG_CHANNEL_GSM_STATE_TEST_FUN;
					ast_debug(3, "GSM channel=\"%s\": state=%s\n", ch_gsm->alias, pg_cahnnel_gsm_state_to_string(ch_gsm->state));
				} else {
					// enable GSM module - key press imitation
					// key press
					if (pg_channel_gsm_key_press(ch_gsm, 1) < 0) {
						ast_log(LOG_ERROR, "GSM channel=\"%s\": key press failure: %s\n", ch_gsm->alias, strerror(errno));
						ast_mutex_unlock(&ch_gsm->lock);
						goto pg_channel_gsm_workthread_end;
					}
					ast_mutex_unlock(&ch_gsm->lock);
					usleep(1000000);
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
	// close TTY device
	close(ch_gsm->tty_fd);
	// reset GSM channel flags
	memset(&ch_gsm->flags, 0, sizeof(struct pg_channel_gsm_flags));
	ch_gsm->flags.power = 1;
	ch_gsm->thread = AST_PTHREADT_NULL;
	ast_mutex_unlock(&ch_gsm->lock);
	ast_debug(4, "GSM channel=\"%s\": thread stop\n", ch_gsm->alias);
	ast_verbose("Polygator: GSM channel=\"%s\" disabled\n", ch_gsm->alias);
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

				vin_init(&vin->context, "/dev/polygator/%s", vin->name);
				vin_set_dev_name(&vin->context, vin->name);
				vin_set_pram(&vin->context, "%s/polygator/edspPRAMfw_%s.bin", ast_config_AST_DATA_DIR, vin->firmware);
				vin_set_dram(&vin->context, "%s/polygator/edspDRAMfw_%s.bin", ast_config_AST_DATA_DIR, vin->firmware);
				vin_set_alm_dsp_ab(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->almab);
				vin_set_alm_dsp_cd(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->almcd);
				vin_set_cram(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->cram);
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
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// reset
				if (vin_reset(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// reset rdyq
				if (vin_reset_rdyq(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_reset_rdyq(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// check rdyq status
				for (i=0; i<5000; i++)
				{
					if (!vin_is_not_ready(&vin->context)) break;
					usleep(1000);
				}
				if (i == 5000) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": not ready\n", vin->name);
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// disable all interrupt
				if (vin_phi_disable_interrupt(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_phi_disable_interrupt(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// revision
				if (!vin_phi_revision(&vin->context)) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_phi_revision(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				ast_verb(3, "vinetic=\"%s\": revision %s\n", vin->name, vin_revision_str(&vin->context));
				// download EDSP firmware
				if (vin_download_edsp_firmware(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_edsp_firmware(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_edsp_firmware(): line %d\n", vin->name, vin->context.errorline);
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// enable polling
				if (vin_poll_enable(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_poll_enable(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// firmware version
				if (vin_read_fw_version(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_read_fw_version(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				ast_verb(3, "vinetic=\"%s\": EDSP firmware version %u.%u.%u\n", vin->name,
					(vin->context.edsp_sw_version_register.mv << 13) +
					(vin->context.edsp_sw_version_register.prt << 12) +
					(vin->context.edsp_sw_version_register.features << 0),
					vin->context.edsp_sw_version_register.main_version,
					vin->context.edsp_sw_version_register.release);
				// download ALM DSP AB firmware
				if (vin_download_alm_dsp(&vin->context, vin->context.alm_dsp_ab_path) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_alm_dsp(AB): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_alm_dsp(AB): line %d\n", vin->name, vin->context.errorline);
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				if (vin_jump_alm_dsp(&vin->context, 0) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_jump_alm_dsp(AB): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_jump_alm_dsp(AB): line %d\n", vin->name, vin->context.errorline);
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// download ALM DSP CD firmware...", vin_dev_name(&vin)); 
				if (vin_download_alm_dsp(&vin->context, vin->context.alm_dsp_cd_path) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_alm_dsp(CD): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_alm_dsp(CD): line %d\n", vin->name, vin->context.errorline);
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				if (vin_jump_alm_dsp(&vin->context, 2) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_jump_alm_dsp(CD): %s\n", vin->name, vin_error_str(&vin->context));
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_jump_alm_dsp(CD): line %d\n", vin->name, vin->context.errorline);
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// download CRAM
				for (i=0; i<4; i++)
				{
					if (vin_download_cram(&vin->context, i, vin->context.cram_path) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_cram(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_download_cram(): line %d\n", vin->name, vin->context.errorline);
						ast_mutex_unlock(&vin->lock);
						goto pg_vinetic_workthread_end;
					}
					if (vin_alm_channel_test_set(&vin->context, i, 1) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_alm_channel_test_set(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_alm_channel_test_set(): line %d\n", vin->name, vin->context.errorline);
						ast_mutex_unlock(&vin->lock);
						goto pg_vinetic_workthread_end;
					}
					if (vin_alm_channel_dcctl_pram_set(&vin->context, i, 0) < 0) {
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_alm_channel_dcctl_pram_set(): %s\n", vin->name, vin_error_str(&vin->context));
						ast_log(LOG_ERROR, "vinetic=\"%s\": vin_alm_channel_dcctl_pram_set(): line %d\n", vin->name, vin->context.errorline);
						ast_mutex_unlock(&vin->lock);
						goto pg_vinetic_workthread_end;
					}
				}
				// switch to little endian mode
				if (vin_set_little_endian_mode(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_set_little_endian_mode(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				ast_verb(3, "vinetic=\"%s\": firmware downloading succeeded\n", vin->name);
				vin->state = PG_VINETIC_STATE_RUN;
				ast_debug(3, "vinetic=\"%s\": run\n", vin->name);
				break;
			case PG_VINETIC_STATE_RUN:
				ast_mutex_unlock(&vin->lock);
				if (vin_get_status(&vin->context) < 0) {
					ast_verbose("vinetic=\"%s\": status error\n", vin->name);
					ast_mutex_lock(&vin->lock);
					vin->state = PG_VINETIC_STATE_IDLE;
				} else {
					ast_verbose("vinetic=\"%s\": status ok\n", vin->name);
					ast_mutex_lock(&vin->lock);
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
			len += fprintf(fp, "[%s:vin%u]\n", vin->board->name, vin->position_on_board);
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
			if (ch_gsm->alias)
				len += fprintf(fp, "alias=%s\n", ch_gsm->alias);
			// enable
			len += fprintf(fp, "enable=%s\n", ch_gsm->flags.enable ? "yes" : "no");
			// place separator
			len += fprintf(fp, FORMAT_SEPARATOR_LINE);
			
			ast_mutex_unlock(&ch_gsm->lock);
		}
		ast_mutex_unlock(&brd->lock);
	}

	ast_mutex_unlock(&pg_lock);

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
static struct ast_channel *pg_gsm_requester(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause)
#elif ASTERISK_VERSION_NUM >= 10800
static struct ast_channel *pg_gsm_requester(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause)
#else
static struct ast_channel *pg_gsm_requester(const char *type, int format, void *data, int *cause)
#endif
{
	struct ast_channel *ast_ch = NULL;

	return ast_ch;
}
//------------------------------------------------------------------------------
// pg_gsm_requester()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_call()
//------------------------------------------------------------------------------
static int pg_gsm_call(struct ast_channel *ast_ch, char *dest, int timeout)
{
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
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_hangup()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_answer()
//------------------------------------------------------------------------------
static int pg_gsm_answer(struct ast_channel *ast_ch)
{
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_answer()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_indicate()
//------------------------------------------------------------------------------
static int pg_gsm_indicate(struct ast_channel *ast_ch, int condition, const void *data, size_t datalen)
{
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_indicate()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_write()
//------------------------------------------------------------------------------
static int pg_gsm_write(struct ast_channel *ast_ch, struct ast_frame *frame)
{
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
	return &ast_null_frame;
}
//------------------------------------------------------------------------------
// end of pg_gsm_read()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_fixup()
//------------------------------------------------------------------------------
static int pg_gsm_fixup(struct ast_channel *old_ast_ch, struct ast_channel *new_ast_ch)
{
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
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_dtmf_end()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_get_board_by_name()
//------------------------------------------------------------------------------
static struct pg_board *pg_get_board_by_name(const char *name)
{
	struct pg_board *brd = NULL;
	// check for name present
	if (name) {
		// check for board list is not empty
		if ((brd = pg_general_board_list.first)) {
			// traverse board list for matching entry name
			brd = NULL;
			AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
			{
				// compare name strings
				if (!strcmp(name, brd->name)) break;
			}
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
		// check for board list is not empty
		if ((ch_gsm = pg_general_channel_gsm_list.first)) {
			// traverse board list for matching entry name
			ch_gsm = NULL;
			AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
			{
				// compare name strings
				if (!strcmp(name, ch_gsm->alias)) break;
			}
		}
	}
	return ch_gsm;
}
//------------------------------------------------------------------------------
// end of pg_get_channel_gsm_by_name()
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

	ast_mutex_lock(&pg_lock);

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

	ast_mutex_unlock(&pg_lock);

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

	ast_mutex_lock(&pg_lock);

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

	ast_mutex_unlock(&pg_lock);

	return res;
}
//------------------------------------------------------------------------------
// end of pg_cli_generate_complete_board_name()
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
static char *pg_cli_generate_complete_channel_gsm_name(const char *begin, int count)
{  
	struct pg_channel_gsm *ch_gsm;
	char *res;
	int beginlen;
	int which;

	res = NULL;
	ch_gsm = NULL;
	which = 0;
	beginlen = strlen(begin);

	ast_mutex_lock(&pg_lock);

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
	if ((!res) && (pg_general_channel_gsm_list.first)) {
		if ((!strncmp(begin, "all", beginlen)) && (++which > count))
			res = ast_strdup("all");
	}

	ast_mutex_unlock(&pg_lock);

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

	ast_mutex_lock(&pg_lock);

	// get board by name
	brd = pg_get_board_by_name(a->argv[3]);

	if (brd) {
		ast_mutex_lock(&brd->lock);

		ast_cli(a->fd, "  Board \"%s\"\n", brd->name);
		ast_cli(a->fd, "  -- type = %s\n", brd->type);

		ast_mutex_unlock(&brd->lock);
	} else
		ast_cli(a->fd, "  Board \"%s\" not found\n", a->argv[3]);

	ast_mutex_unlock(&pg_lock);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_board()
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

	ast_mutex_lock(&pg_lock);

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

	ast_mutex_unlock(&pg_lock);

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

	char buf[4];
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

	ast_mutex_lock(&pg_lock);

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
		sim_fl = mmax(sim_fl, snprintf(buf, sizeof(buf), "%s", ch_gsm->flags.sim_present?"inserted":""));
		reg_fl = mmax(reg_fl, strlen(reg_status_print_short(ch_gsm->reg_stat)));
		count++;
		ast_mutex_unlock(&ch_gsm->lock);
	}
	if (count) {
		ast_cli(a->fd, "  GSM channels:\n");
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
					sim_fl, ch_gsm->flags.sim_present?"inserted":"",
					reg_fl, reg_status_print_short(ch_gsm->reg_stat));
			ast_mutex_unlock(&ch_gsm->lock);
		}
		total += count;
		ast_cli(a->fd, "  Total %lu GSM channel%s\n", (unsigned long int)count, ESS(count));
	}

	if (!total)
		ast_cli(a->fd, "  No channels found\n");

	ast_mutex_unlock(&pg_lock);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_channels()
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
				return pg_cli_generate_complete_channel_gsm_name(a->word, a->n);
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

	if (a->argc == 5) {
		snprintf(pg_cli_channel_gsm_actions_usage, sizeof(pg_cli_channel_gsm_actions_usage),
				"Usage: polygator channel gsm <channel> power on|off\n");
		return CLI_SHOWUSAGE;
	}

	ast_mutex_lock(&pg_lock);

	total = 0;
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		if (!strcmp(a->argv[3], "all") || !strcmp(a->argv[3], ch_gsm->alias)) {
			total++;
			ast_cli(a->fd, "  <%s>: ", ch_gsm->alias);
			if (ast_true(a->argv[5])) {
				// on 
				if (!ch_gsm->flags.power) {
					if (!pg_channel_gsm_power_set(ch_gsm, 1)) {
						ch_gsm->flags.power = 1;
						ast_mutex_unlock(&ch_gsm->lock);
						ast_mutex_unlock(&pg_lock);
						usleep(750000);
						ast_mutex_lock(&pg_lock);
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

	ast_mutex_unlock(&pg_lock);

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

	ast_mutex_lock(&pg_lock);

	total = 0;
	pwr_seq_num = 0;
	AST_LIST_TRAVERSE(&pg_general_channel_gsm_list, ch_gsm, pg_general_channel_gsm_list_entry)
	{
		ast_mutex_lock(&ch_gsm->lock);
		if (!strcmp(a->argv[3], "all") || !strcmp(a->argv[3], ch_gsm->alias)) {
			total++;
			ast_cli(a->fd, "  <%s>: ", ch_gsm->alias);
			if (!strcmp(a->argv[4], "enable")) {
				// enable
				if (!ch_gsm->flags.enable) {
					// start GSM channel workthread
					ch_gsm->flags.enable = 1;
					if (!ch_gsm->flags.power)
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
	ast_mutex_unlock(&pg_lock);

	if (!total)
		ast_cli(a->fd, "  Channel \"%s\" not found\n", a->argv[3]);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_channel_gsm_action_enable_disable()
//------------------------------------------------------------------------------
#if 0
//------------------------------------------------------------------------------
// pg_cli_trunk_gsm_actions()
//------------------------------------------------------------------------------
static char *pg_cli_trunk_gsm_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct eggsm_trunk *trunk;
	struct eggsm_chnl *chnl;
	struct eggsm_channel_trunk_entry *tr_tren, *ch_tren;

	char *gline;
	char *gargv[AST_MAX_ARGS];
	int gargc;

	switch(cmd){
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "eggsm trunk [create|delete|rename]";
			sprintf(eggsm_cli_trunk_actions_usage, "Usage: eggsm trunk <action> <trunk_name> [...]\n");
			e->usage = eggsm_cli_trunk_actions_usage;
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_GENERATE:
			gline = ast_strdupa(a->line);
			if(!(eggsm_cli_generating_prepare(gline, &gargc, gargv))){
				// try to generate complete trunk name
				if((a->pos == 3) && (strcmp(gargv[2], "create")))
					return eggsm_cli_ch_complete_trunk_name(a->word, a->n);
				}
			//
			return NULL;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_HANDLER:
			break;
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		default:
			ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
			return CLI_FAILURE;
		}

	if(a->argc < 3){
		sprintf(eggsm_cli_trunk_actions_usage, "Usage: eggsm trunk <action> <trunk_name> [...]\n");
		return CLI_SHOWUSAGE;
		}
	else if((a->argc == 3) && (strcmp(a->argv[2], "rename"))){
		sprintf(eggsm_cli_trunk_actions_usage, "Usage: eggsm trunk %s <trunk_name>\n", a->argv[2]);
		return CLI_SHOWUSAGE;
		}
	else if((a->argc == 3) && (!strcmp(a->argv[2], "rename"))){
		sprintf(eggsm_cli_trunk_actions_usage, "Usage: eggsm trunk %s <trunk_name> <new_name>\n", a->argv[2]);
		return CLI_SHOWUSAGE;
		}
	else if((a->argc == 4) && (!strcmp(a->argv[2], "rename"))){
		sprintf(eggsm_cli_trunk_actions_usage, "Usage: eggsm trunk rename %s <new_name>\n", a->argv[3]);
		return CLI_SHOWUSAGE;
		}
	// select operation
	if(!strcmp(a->argv[2], "create")){
		// create
		// lock eggsm subsystem
		ast_mutex_lock(&eggsm_lock);
		// search this trunk name in list
		AST_LIST_TRAVERSE(&trunks, trunk, entry)
			if(!strcmp(trunk->name, a->argv[3])) break;
		// check for trunk already exist
		if(!trunk){
			// create trunk storage
			if((trunk = ast_calloc(1, sizeof(struct eggsm_trunk)))){
				//
				ast_copy_string(trunk->name, a->argv[3], sizeof(trunk->name));
				AST_LIST_INSERT_TAIL(&trunks, trunk, entry);
				// init trunk channel list
				AST_LIST_HEAD_SET_NOLOCK(&trunk->channels, NULL);
				trunk->last = NULL;
				//
				ast_cli(a->fd, "  trunk \"%s\" registered\n", a->argv[3]);
				}
			}
		else
			ast_cli(a->fd, "  trunk \"%s\" already exist\n", a->argv[3]);
		// unlock eggsm subsystem
		ast_mutex_unlock(&eggsm_lock);
		}
	else if(!strcmp(a->argv[2], "delete")){
		// delete
		// lock eggsm subsystem
		ast_mutex_lock(&eggsm_lock);
		// search this trunk name in list
		AST_LIST_TRAVERSE(&trunks, trunk, entry)
			if(!strcmp(trunk->name, a->argv[3])) break;
		// check for trunk already exist
		if(trunk){
			// clean trunk binding in trunk channel list member
			while((tr_tren = AST_LIST_REMOVE_HEAD(&trunk->channels, tr_entry))){
				if((chnl = tr_tren->channel)){
					// lock channel pvt
					ast_mutex_lock(&chnl->pvt_lock);
					//
					AST_LIST_TRAVERSE(&chnl->trunks, ch_tren, ch_entry)
				  		if(!strcmp(trunk->name, ch_tren->name)) break;
					if(ch_tren){
						// remove trunk entry from channel
						AST_LIST_REMOVE(&chnl->trunks, ch_tren, ch_entry);
						ast_free(ch_tren);
						}
					// unlock channel pvt
					ast_mutex_unlock(&chnl->pvt_lock);
					}
				}
			// remove trunk from list
			AST_LIST_REMOVE(&trunks, trunk, entry);
			ast_free(trunk);
			//
			ast_cli(a->fd, "  trunk \"%s\" deleted\n", a->argv[3]);
			}
		else
			ast_cli(a->fd, "  trunk \"%s\" not found\n", a->argv[3]);
		// unlock eggsm subsystem
		ast_mutex_unlock(&eggsm_lock);
		}
	else if(!strcmp(a->argv[2], "rename")){
		// rename
		// lock eggsm subsystem
		ast_mutex_lock(&eggsm_lock);
		// check for trunk already exist
		AST_LIST_TRAVERSE(&trunks, trunk, entry)
			if(!strcmp(trunk->name, a->argv[4])) break;
		if(!trunk){
			// search this trunk name in list
			AST_LIST_TRAVERSE(&trunks, trunk, entry)
				if(!strcmp(trunk->name, a->argv[3])) break;
			if(trunk){
				// rename trunk binding in trunk channel list member
				AST_LIST_TRAVERSE(&trunk->channels, tr_tren, tr_entry){
					if((chnl = tr_tren->channel)){
						// lock channel pvt
						ast_mutex_lock(&chnl->pvt_lock);
						//
						ast_copy_string(tr_tren->name, a->argv[4], EGGSM_MAX_TRUNK_NAME_LEN);
						// unlock channel pvt
						ast_mutex_unlock(&chnl->pvt_lock);
						}
					}
				// rename trunk
				ast_copy_string(trunk->name, a->argv[4], sizeof(trunk->name));
				//
				ast_cli(a->fd, "  trunk \"%s\" successfully renamed to \"%s\"\n", a->argv[3], a->argv[4]);
				}
			else
				ast_cli(a->fd, "  trunk \"%s\" not found\n", a->argv[3]);
			}
		else
			ast_cli(a->fd, "  trunk \"%s\" already exist\n", a->argv[4]);
		// unlock eggsm subsystem
		ast_mutex_unlock(&eggsm_lock);
		}
	else
		// unknown
		ast_cli(a->fd, "  unknown trunk operation \"%s\"\n", a->argv[2]);
	//
	return CLI_SUCCESS;
	}
//------------------------------------------------------------------------------
// end of eggsm_cli_trunk_actions()
//------------------------------------------------------------------------------
#endif
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
	if (pg_gsm_tech_registered) ast_channel_unregister(&pg_gsm_tech);

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
			if (ch_gsm->device) ast_free(ch_gsm->device);
			if (ch_gsm->tty_path) ast_free(ch_gsm->tty_path);
			if (ch_gsm->alias) ast_free(ch_gsm->alias);
			ast_free(ch_gsm);
		}
		// destroy board vinetic list
		while ((vin = AST_LIST_REMOVE_HEAD(&brd->vinetic_list, pg_board_vinetic_list_entry)))
		{
			// destroy vinetic RTP channel list
			while ((rtp = AST_LIST_REMOVE_HEAD(&vin->channel_rtp_list, pg_vinetic_channel_rtp_list_entry)))
			{
				if (rtp->name) ast_free(rtp->name);
				ast_free(rtp);
			}
			ast_free(vin);
		}
		// free dynamic allocated memory
		if (brd->name) ast_free(brd->name);
		if (brd->type) ast_free(brd->type);
		ast_free(brd);
	}

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


	unsigned int pos;
	unsigned int index;
	unsigned int vio;

	char path[PATH_MAX];
	
	struct pg_board *brd;
	struct pg_channel_gsm *ch_gsm;
	struct pg_vinetic *vin;
	struct pg_channel_rtp *rtp;

	struct ast_config *ast_cfg;
	struct ast_flags ast_cfg_flags;
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

	// scan polygator subsystem
	snprintf(path, PATH_MAX, "/dev/polygator/%s", "subsystem");
	if ((fp = fopen(path, "r"))) {
		while (fgets(buf, sizeof(buf), fp))
		{
			if (sscanf(buf, "%[0-9a-z-] %[0-9a-z-]", type, name) == 2) {
				ast_verbose("Polygator: found board type=\"%s\" name=\"%s\"\n", type, name);
				if (!(brd = ast_calloc(1, sizeof(struct pg_board)))) {
					ast_log(LOG_ERROR, "can't get memory for struct pg_board\n");
					goto pg_load_error;
				}
				// add board into general board list
				AST_LIST_INSERT_TAIL(&pg_general_board_list, brd, pg_general_board_list_entry);
				// init board
				ast_mutex_init(&brd->lock);
				brd->type = ast_strdup(type);
				brd->name = ast_strdup(name);
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
		snprintf(path, PATH_MAX, "/dev/polygator/%s", brd->name);
		if (!(fp = fopen(path, "r"))) {
			ast_log(LOG_ERROR, "unable to scan Polygator board \"%s\": %s\n", brd->name, strerror(errno));
			goto pg_load_error;
		}
		while (fgets(buf, sizeof(buf), fp))
		{
			if (sscanf(buf, "GSM%u %[0-9A-Za-z-] %[0-9A-Za-z-] VIO=%u", &pos, name, type, &vio) == 4) {
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
				snprintf(path, sizeof(path), "/dev/polygator/%s", name);
				ch_gsm->tty_path = ast_strdup(path);
				ch_gsm->gsm_module_type = pg_gsm_module_type_get(type);
				// get config variables
				// enable
				ch_gsm->config.enable = 0;
				if ((cvar = pg_get_config_variable(ast_cfg, ch_gsm->device, "enable")))
					ch_gsm->config.enable = -ast_true(cvar);
				// baudrate
				ch_gsm->config.baudrate = 115200;
				if ((cvar = ast_variable_retrieve(ast_cfg, ch_gsm->device, "baudrate")) && (is_str_digit(cvar)))
					ch_gsm->config.baudrate = atoi(cvar);

				if (ch_gsm->config.enable) {
					// start GSM channel workthread
					ch_gsm->flags.enable = 1;
					if (!ch_gsm->flags.power)
						ch_gsm->power_sequence_number = pwr_seq_num++;
					if (ast_pthread_create_detached(&ch_gsm->thread, NULL, pg_channel_gsm_workthread, ch_gsm) < 0) {
						ast_log(LOG_ERROR, "can't start workthread for GSM channel=\"%s\"\n", ch_gsm->alias);
						ch_gsm->flags.enable = 0;
						ch_gsm->thread = AST_PTHREADT_NULL;
						goto pg_load_error;
					}
				}
			} else if (sscanf(buf, "VIN%uRTP%u %[0-9A-Za-z-]", &index, &pos, name) == 3) {
				ast_verbose("Polygator: found RTP channel=\"%s\"\n", name);
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
					rtp->pos_on_vinetic = pos;
					rtp->name = ast_strdup(name);
				} else 
					ast_log(LOG_WARNING, "PG board=\"%s\": VIN%u not found\n", brd->name, index);
			} else if (sscanf(buf, "VIN%u %[0-9A-Za-z-]", &pos, name) == 2) {
				ast_verbose("Polygator: found vinetic=\"%s\"\n", name);
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
				vin->name = ast_strdup(name);
				vin->firmware = "RTP_0_15_56_V14";
				vin->almab = "ALM_2484_AB_01.dwl";
				vin->almcd = "ALM_2484_CD_01.dwl";
				vin->cram = "cram.byt";
				vin->run = 1;
				vin->thread = AST_PTHREADT_NULL;
				vin->state = PG_VINETIC_STATE_INIT;
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
/* end of chan_pg.c                                                           */
/******************************************************************************/
