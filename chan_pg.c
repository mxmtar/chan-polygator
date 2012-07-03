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
// #include "asterisk/utils.h"
#include "asterisk/version.h"

#include "polygator/polygator-base.h"

#include "libvinetic.h"

#include "at.h"

enum {
	PG_VINETIC_STATE_INIT = 1,
	PG_VINETIC_STATE_IDLE = 2,
	PG_VINETIC_STATE_RUN = 3,
};

struct pg_vinetic {
	// vinetic private lock
	ast_mutex_t lock;

	pthread_t thread;

	int state;

	struct vinetic_context context;

	// entry for board vinetic list
	AST_LIST_ENTRY(pg_vinetic) pg_board_vinetic_list_entry;
};

struct pg_gsm_channel;
struct pg_board {
	// board private lock
	ast_mutex_t lock;

	char *type;
	char *name;

	// board channel list
	AST_LIST_HEAD_NOLOCK(gsm_channel_list, pg_gsm_channel) gsm_channel_list;
	// board vinetic list
	AST_LIST_HEAD_NOLOCK(vinetic_list, pg_vinetic) vinetic_list;


	// entry for general board list
	AST_LIST_ENTRY(pg_board) pg_general_board_list_entry;
};

struct pg_gsm_channel {
	// channel private lock
	ast_mutex_t lock;
	pthread_t thread;

	char *name;
	unsigned int gsm_module_type;	// type of GSM module ("SIM300", "M10", "SIM900", "SIM5215")

	unsigned int pos_on_board;
	struct pg_board *board;
	
	struct pg_gsm_channel_flags {
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
	} flags;

	// Registration status
	int reg_stat;
	int reg_stat_old;

	// entry for board channel list
	AST_LIST_ENTRY(pg_gsm_channel) pg_board_gsm_channel_list_entry;
	// entry for general channel list
	AST_LIST_ENTRY(pg_gsm_channel) pg_general_gsm_channel_list_entry;
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

struct pg_cli_channel_param {
	int id;
	char name[AST_MAX_CMD_LEN];
};
#define PG_CLI_CH_PARAM(prm, prmid) \
	{.id = prmid, .name = prm}

#define PG_CLI_PARAM_COUNT(prm) \
	sizeof(prm) / sizeof(prm[0])

static char *pg_cli_show_board(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_boards(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_gsm_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_modinfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry pg_cli[] = {
	AST_CLI_DEFINE(pg_cli_show_board, "Show PG board information"),
	AST_CLI_DEFINE(pg_cli_show_boards, "Show PG boards information summary"),
	AST_CLI_DEFINE(pg_cli_show_gsm_channels, "Show PG GSM channels information summary"),
	AST_CLI_DEFINE(pg_cli_show_modinfo, "Show PG module information"),
};

// gsm module types
struct pg_cli_channel_param pg_gsm_module_types[] = {
	PG_CLI_CH_PARAM("SIM300", POLYGATOR_MODULE_TYPE_SIM300),
	PG_CLI_CH_PARAM("SIM900", POLYGATOR_MODULE_TYPE_SIM900),
	PG_CLI_CH_PARAM("M10", POLYGATOR_MODULE_TYPE_M10),
	PG_CLI_CH_PARAM("SIM5215", POLYGATOR_MODULE_TYPE_SIM5215),
};

AST_MUTEX_DEFINE_STATIC(pg_lock);

static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_board_list, pg_board);
static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_gsm_channel_list, pg_gsm_channel);

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
// pg_get_gsm_module_type()
//------------------------------------------------------------------------------
static unsigned int pg_get_gsm_module_type(const char *module_type)
{
	size_t i;
	int res = POLYGATOR_MODULE_TYPE_UNKNOWN;

	for (i=0; i<PG_CLI_PARAM_COUNT(pg_gsm_module_types); i++)
	{
		if (!strcasecmp(module_type, pg_gsm_module_types[i].name))
			return pg_gsm_module_types[i].id;
	}
	return res;
}
//------------------------------------------------------------------------------
// end of pg_get_gsm_module_type()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_module_type_to_string()
//------------------------------------------------------------------------------
static char *pg_gsm_module_type_to_string(unsigned int type)
{
	size_t i;

	for (i=0; i<PG_CLI_PARAM_COUNT(pg_gsm_module_types); i++)
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
	struct ast_channel *ast_chnl = NULL;

	return ast_chnl;
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
static int pg_gsm_answer(struct ast_channel *ast)
{
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_gsm_answer()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_gsm_indicate()
//------------------------------------------------------------------------------
static int pg_gsm_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
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
static int pg_gsm_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
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
	struct pg_board *brd;
	int namelen;

	// check for name present
	if (!name)
		return NULL;
	// check length of param
	if (!(namelen = strlen(name)))
		return NULL;
	// check for board list is not empty
	brd = pg_general_board_list.first;
	if (!brd)
		return NULL;
	// traverse board list for matching entry name
	brd = NULL;
	AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
	{
		// compare name length
		if (namelen != strlen(brd->name)) continue;
			// compare name strings
		if (!strncmp(name, brd->name, namelen)) break;
	}

	return brd;
}
//------------------------------------------------------------------------------
// end of pg_get_board_by_name()
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

	//
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

	//
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
// pg_cli_show_board()
//------------------------------------------------------------------------------
static char *pg_cli_show_board(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct pg_board *brd;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "pg show board";
			e->usage = "Usage: pg show board <board>\n";
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
			e->command = "pg show boards";
			e->usage = "Usage: pg show boards [<type>]\n";
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
			// try to generate complete board type
			if (a->pos == 3)
				return pg_cli_generate_complete_board_type(a->word, a->n);


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
// pg_cli_show_gsm_channels()
//------------------------------------------------------------------------------
static char *pg_cli_show_gsm_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	size_t count;
	struct pg_gsm_channel *ch;

	char buf[256];
	int number_fl;
	int device_fl;
	int module_fl;
	int power_fl;
	int sim_fl;
	int reg_fl;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "pg show gsm channels";
			e->usage = "Usage: pg show gsm channels\n";
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

	if (a->argc < 4)
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&pg_lock);

	count = 0;
	number_fl = strlen("#");
	device_fl = strlen("Device");
	module_fl = strlen("Module");
	power_fl = strlen("Power");
	sim_fl = strlen("SIM");
	reg_fl = strlen("Registered");
	AST_LIST_TRAVERSE(&pg_general_gsm_channel_list, ch, pg_general_gsm_channel_list_entry)
	{
		ast_mutex_lock(&ch->lock);
		number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count));
		device_fl = mmax(device_fl, snprintf(buf, sizeof(buf), "%s:%u", ch->board->name, ch->pos_on_board));
		module_fl = mmax(module_fl, strlen(pg_gsm_module_type_to_string(ch->gsm_module_type)));
		power_fl = mmax(power_fl, strlen(AST_CLI_ONOFF(ch->flags.enable)));
		sim_fl = mmax(sim_fl, snprintf(buf, sizeof(buf), "%s", (ch->flags.sim_present)?("inserted"):("")));
		reg_fl = mmax(reg_fl, strlen(reg_status_print_short(ch->reg_stat)));
		count++;
		ast_mutex_unlock(&ch->lock);
	}
	if (count) {
		ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
				number_fl, "#",
				device_fl, "Device",
				module_fl, "Module",
				power_fl, "Power",
				sim_fl, "SIM",
				reg_fl, "Registered");
		count = 0;
		AST_LIST_TRAVERSE(&pg_general_gsm_channel_list, ch, pg_general_gsm_channel_list_entry)
		{
			ast_mutex_lock(&ch->lock);

			snprintf(buf, sizeof(buf), "%s:%u", ch->board->name, ch->pos_on_board);
			ast_cli(a->fd, "| %-*lu | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
					number_fl, (unsigned long int)count++,
					device_fl, buf,
					module_fl, pg_gsm_module_type_to_string(ch->gsm_module_type),
					power_fl, AST_CLI_ONOFF(ch->flags.enable),
					sim_fl, (ch->flags.sim_present)?("inserted"):(""),
					reg_fl, reg_status_print_short(ch->reg_stat));
			ast_mutex_unlock(&ch->lock);
		}
		ast_cli(a->fd, "  Total %lu channel%s\n", (unsigned long int)count, ESS(count));
	} else 
		ast_cli(a->fd, "  No channels found\n");

	ast_mutex_unlock(&pg_lock);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_gsm_channels()
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
			e->command = "pg show modinfo";
			e->usage = "Usage: pg show modinfo\n";
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
	ast_cli(a->fd, "  PG GSM module Info:\n");
	// show eggsm module version
	ast_cli(a->fd, "  -- module version: %s\n", VERSION);
	// show eggsm asterisk version
	ast_cli(a->fd, "  -- asterisk version: %s\n", ASTERISK_VERSION);
	// show eggsm module uptime
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
// pg_atexit()
//------------------------------------------------------------------------------
void pg_atexit(void)
{
	return;
}
//------------------------------------------------------------------------------
// end of pg_atexit()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_load()
//------------------------------------------------------------------------------
static int pg_load(void)
{
	FILE *fp;
	char buf[256];
	char type[64];
	char name[64];

	unsigned int pos;
	unsigned int vio;

	char path[PATH_MAX];
	
	struct pg_board *brd;
	struct pg_gsm_channel *ch;
	struct pg_vinetic *vin;

	ast_mutex_lock(&pg_lock);

	ast_verbose("PG: module \"%s\" loading...\n", VERSION);

	gettimeofday(&pg_start_time, NULL);

	// register atexit function
	if (ast_register_atexit(pg_atexit) < 0) {
		ast_log(LOG_ERROR, "unable to register atexit function\n");
		goto pg_load_error;
	}

#if 0
	// load configuration
	if(eggsm_load_config(config_file) < 0){
		ast_log(LOG_ERROR, "unable to get configuration from \"%s\"\n", config_file);
		goto pg_load_error;
		}
#endif

	// scan polygator subsystem
	snprintf(path, PATH_MAX, "/dev/polygator/%s", "subsystem");
	if (!(fp = fopen(path, "r"))) {
		ast_log(LOG_ERROR, "unable to scan Polygator subsystem: %s\n", strerror(errno));
		goto pg_load_error;
	}
	while (fgets(buf, sizeof(buf), fp))
	{
		if (sscanf(buf, "%[0-9a-z-] %[0-9a-z-]", type, name) == 2) {
			ast_verbose("PG: found board type=\"%s\" name=\"%s\"\n", type, name);
			if (!(brd = ast_calloc(1, sizeof(struct pg_board)))) {
				ast_log(LOG_ERROR, "can't get memory for struct pg_board\n");
				goto pg_load_error;
			}
			// init board
			ast_mutex_init(&brd->lock);
			brd->type = ast_strdup(type);
			brd->name = ast_strdup(name);
			// add board into general board list
			AST_LIST_INSERT_TAIL(&pg_general_board_list, brd, pg_general_board_list_entry);
		}
	}
	fclose(fp);

	// scan polygator board
	AST_LIST_TRAVERSE(&pg_general_board_list, brd, pg_general_board_list_entry)
	{
		snprintf(path, PATH_MAX, "/dev/polygator/%s", brd->name);
		if (!(fp = fopen(path, "r"))) {
			ast_log(LOG_ERROR, "unable to scan Polygator board \"%s\": %s\n", brd->name, strerror(errno));
			goto pg_load_error;
		}
		while (fgets(buf, sizeof(buf), fp))
		{
			if (sscanf(buf, "AT%u %[0-9A-Za-z-] %[0-9A-Za-z-] VIO=%u", &pos, name, type, &vio) == 4) {

				if (!(ch = ast_calloc(1, sizeof(struct pg_gsm_channel)))) {
					ast_log(LOG_ERROR, "can't get memory for struct pg_gsm_channel\n");
					goto pg_load_error;
				}
				// init channel
				ast_mutex_init(&ch->lock);
				ch->thread = AST_PTHREADT_NULL;
				ch->pos_on_board = pos;
				ch->board = brd;
				ch->name = ast_strdup(name);
				ch->gsm_module_type = pg_get_gsm_module_type(type);
				// add channel into board channel list
				AST_LIST_INSERT_TAIL(&brd->gsm_channel_list, ch, pg_board_gsm_channel_list_entry);
				// add channel into general channel list
				AST_LIST_INSERT_TAIL(&pg_general_gsm_channel_list, ch, pg_general_gsm_channel_list_entry);

			} else if (sscanf(buf, "VIN%u %[0-9A-Za-z-]", &pos, name) == 2) {

				if (!(vin = ast_calloc(1, sizeof(struct pg_vinetic)))) {
					ast_log(LOG_ERROR, "can't get memory for struct pg_vinetic\n");
					goto pg_load_error;
				}
				// init vinetic
				ast_mutex_init(&vin->lock);
				vin->thread = AST_PTHREADT_NULL;
				vin->state = PG_VINETIC_STATE_INIT;
				// add vinetic into board vinetic list
				AST_LIST_INSERT_TAIL(&brd->vinetic_list, vin, pg_board_vinetic_list_entry);

			} else {
				ast_verbose("%s\n", buf);
			}
		}
		fclose(fp);
	}

#if ASTERISK_VERSION_NUM >= 100000
	struct ast_format tmpfmt;
	// adding capabilities
	if (!(pg_gsm_tech.capabilities = ast_format_cap_alloc())) {
		goto pg_load_error;
	}
	ast_format_cap_add(pg_gsm_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0));
	ast_format_cap_add(pg_gsm_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_G726, 0));
	ast_format_cap_add(pg_gsm_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
	ast_format_cap_add(pg_gsm_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
#endif

	// registering channel class PGGSM in asterisk PBX
	if (ast_channel_register(&pg_gsm_tech)) {
		ast_log(LOG_ERROR, "unable to register channel class 'PGGSM'\n");
		goto pg_load_error;
	}

	// registering Polygator CLI interface
	ast_cli_register_multiple(pg_cli, sizeof(pg_cli)/sizeof(pg_cli[0]));

	ast_verbose("PG: module loaded successfull\n");
	ast_mutex_unlock(&pg_lock);
	return AST_MODULE_LOAD_SUCCESS;

pg_load_error:
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
	struct pg_board *brd;
	struct pg_gsm_channel *ch;

	ast_mutex_lock(&pg_lock);

	// unregistering K5GSM channel class
	ast_channel_unregister(&pg_gsm_tech);

	// unregistering Polygator CLI interface
	ast_cli_unregister_multiple(pg_cli, sizeof(pg_cli)/sizeof(pg_cli[0]));
	ast_verbose("PG: CLI unregistered\n");

#if ASTERISK_VERSION_NUM >= 100000
	// destroy capabilities
	pg_gsm_tech.capabilities = ast_format_cap_destroy(pg_gsm_tech.capabilities);
#endif

	// destroy general channel list
	while ((ch = AST_LIST_REMOVE_HEAD(&pg_general_gsm_channel_list, pg_general_gsm_channel_list_entry)));

	// destroy board list
	while ((brd = AST_LIST_REMOVE_HEAD(&pg_general_board_list, pg_general_board_list_entry)))
	{
		// destroy general channel list
		while ((ch = AST_LIST_REMOVE_HEAD(&brd->gsm_channel_list, pg_board_gsm_channel_list_entry)))
		{
			ast_free(ch->name);
			ast_free(ch);
		}
		// free dynamic allocated memory
		ast_free(brd->name);
		ast_free(brd->type);
		ast_free(brd);
	}

	// unregister atexit function
	ast_unregister_atexit(pg_atexit);

	ast_verbose("PG: module unloaded successfull\n");
	ast_mutex_unlock(&pg_lock);
	return 0;
}
//------------------------------------------------------------------------------
// end of pg_unload()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
AST_MODULE_INFO(
	ASTERISK_GPL_KEY,
	AST_MODFLAG_DEFAULT,
	"Polygator GSM",
	.load = pg_load,
	.unload = pg_unload,
);
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of chan_pg.c                                                           */
/******************************************************************************/
