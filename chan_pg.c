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

#include "at.h"


enum {
	PG_VINETIC_STATE_INIT = 1,
	PG_VINETIC_STATE_IDLE = 2,
	PG_VINETIC_STATE_RUN = 3,
};

struct pg_gsm_channel;
struct pg_vinetic;
struct pg_rtp_channel;

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

struct pg_vinetic {
	// vinetic private lock
	ast_mutex_t lock;
	pthread_t thread;

	unsigned int pos_on_board;
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
	AST_LIST_HEAD_NOLOCK(rtp_channel_list, pg_rtp_channel) rtp_channel_list;

	// entry for board vinetic list
	AST_LIST_ENTRY(pg_vinetic) pg_board_vinetic_list_entry;
};

struct pg_rtp_channel {
	char *name;

	unsigned int pos_on_vinetic;
	struct pg_vinetic *vinetic;

	int state;

	struct vinetic_context context;

	// entry for board vinetic list
	AST_LIST_ENTRY(pg_rtp_channel) pg_vinetic_rtp_channel_list_entry;
};

struct pg_gsm_channel {
	// channel private lock
	ast_mutex_t lock;
	pthread_t thread;

	char *name;
	unsigned int gsm_module_type;	// type of GSM module ("SIM300", "M10", "SIM900", "SIM5215")

	unsigned int pos_on_board;
	struct pg_board *board;
	
	char *alias;
	
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

static int pg_atexit_registered = 0;
static int pg_gsm_tech_neededed = 0;
static int pg_gsm_tech_registered = 0;

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
static char *pg_cli_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_show_modinfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *pg_cli_config_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static char pg_cli_config_actions_usage[256];

static struct ast_cli_entry pg_cli[] = {
	AST_CLI_DEFINE(pg_cli_show_board, "Show PG board information"),
	AST_CLI_DEFINE(pg_cli_show_boards, "Show PG boards information summary"),
	AST_CLI_DEFINE(pg_cli_show_channels, "Show PG channels information summary"),
	AST_CLI_DEFINE(pg_cli_show_modinfo, "Show PG module information"),
	AST_CLI_DEFINE(pg_cli_config_actions, "Save Polygator configuration"),
};

// gsm module types
struct pg_cli_channel_param pg_gsm_module_types[] = {
	PG_CLI_CH_PARAM("SIM300", POLYGATOR_MODULE_TYPE_SIM300),
	PG_CLI_CH_PARAM("SIM900", POLYGATOR_MODULE_TYPE_SIM900),
	PG_CLI_CH_PARAM("M10", POLYGATOR_MODULE_TYPE_M10),
	PG_CLI_CH_PARAM("SIM5215", POLYGATOR_MODULE_TYPE_SIM5215),
};

static char pg_config_file[] = "polygator.conf";

static int pg_at_response_timeout = 2;

AST_MUTEX_DEFINE_STATIC(pg_lock);

static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_board_list, pg_board);
static AST_LIST_HEAD_NOLOCK_STATIC(pg_general_gsm_channel_list, pg_gsm_channel);

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
// pg_get_vinetic_from_board()
//------------------------------------------------------------------------------
static inline struct pg_vinetic *pg_get_vinetic_from_board(struct pg_board *brd, unsigned int pos)
{
	struct pg_vinetic *vin = NULL;

	if (brd) {
		ast_mutex_lock(&brd->lock);
		AST_LIST_TRAVERSE(&brd->vinetic_list, vin, pg_board_vinetic_list_entry)
			if (vin->pos_on_board == pos) break;
		ast_mutex_unlock(&brd->lock);
	}

	return vin;
}
//------------------------------------------------------------------------------
// end of pg_get_vinetic_from_board()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_build_config_file()
//------------------------------------------------------------------------------
static int pg_build_config_file(char *filename)
{
	FILE *fp;
	char path[PATH_MAX];
	int len = 0;
	struct timeval curtime;

	struct pg_board *brd;
	struct pg_gsm_channel *gsm_ch;
	struct pg_vinetic *vin;

	sprintf(path, "%s/%s", ast_config_AST_CONFIG_DIR, filename);
	if (!(fp = fopen(path, "w"))) {
		ast_log(LOG_ERROR, "fopen(%s): %s\n", path, strerror(errno));
		return -1;
	}

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
			len += fprintf(fp, "[%s:vin%u]\n", vin->board->name, vin->pos_on_board);
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
		AST_LIST_TRAVERSE(&brd->gsm_channel_list, gsm_ch, pg_board_gsm_channel_list_entry)
		{
			ast_mutex_lock(&gsm_ch->lock);
			// GSM channel category
			len += fprintf(fp, "[%s:gsm%u]\n", gsm_ch->board->name, gsm_ch->pos_on_board);
			// alias
			if (gsm_ch->alias)
				len += fprintf(fp, "alias=%s\n", gsm_ch->alias);
			// enable
			len += fprintf(fp, "enable=%s\n", gsm_ch->flags.enable ? "yes" : "no");
			// place separator
			len += fprintf(fp, FORMAT_SEPARATOR_LINE);
			
			ast_mutex_unlock(&gsm_ch->lock);
		}
		ast_mutex_unlock(&brd->lock);
	}

	ast_mutex_unlock(&pg_lock);

	fclose(fp);
	return len;
}
//------------------------------------------------------------------------------
// end of pg_build_config_file()
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
	struct pg_gsm_channel *gsm_ch;

	char buf[256];
	char alias[64];
	int number_fl;
	int alias_fl;
	int device_fl;
	int module_fl;
	int power_fl;
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

	count = 0;
	number_fl = strlen("#");
	alias_fl = strlen("Alias");
	device_fl = strlen("Device");
	module_fl = strlen("Module");
	power_fl = strlen("Power");
	sim_fl = strlen("SIM");
	reg_fl = strlen("Registered");
	AST_LIST_TRAVERSE(&pg_general_gsm_channel_list, gsm_ch, pg_general_gsm_channel_list_entry)
	{
		ast_mutex_lock(&gsm_ch->lock);
		number_fl = mmax(number_fl, snprintf(buf, sizeof(buf), "%lu", (unsigned long int)count));
		alias_fl = mmax(alias_fl, (gsm_ch->alias?strlen(gsm_ch->alias):snprintf(alias, sizeof(alias), "chan-%lu", (unsigned long int)count)));
		device_fl = mmax(device_fl, snprintf(buf, sizeof(buf), "%s:%u", gsm_ch->board->name, gsm_ch->pos_on_board));
		module_fl = mmax(module_fl, strlen(pg_gsm_module_type_to_string(gsm_ch->gsm_module_type)));
		power_fl = mmax(power_fl, strlen(AST_CLI_ONOFF(gsm_ch->flags.enable)));
		sim_fl = mmax(sim_fl, snprintf(buf, sizeof(buf), "%s", (gsm_ch->flags.sim_present)?("inserted"):("")));
		reg_fl = mmax(reg_fl, strlen(reg_status_print_short(gsm_ch->reg_stat)));
		count++;
		ast_mutex_unlock(&gsm_ch->lock);
	}
	if (count) {
		ast_cli(a->fd, "  GSM channels:\n");
		ast_cli(a->fd, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
				number_fl, "#",
		  		alias_fl, "Alias",
				device_fl, "Device",
				module_fl, "Module",
				power_fl, "Power",
				sim_fl, "SIM",
				reg_fl, "Registered");
		count = 0;
		AST_LIST_TRAVERSE(&pg_general_gsm_channel_list, gsm_ch, pg_general_gsm_channel_list_entry)
		{
			ast_mutex_lock(&gsm_ch->lock);

			snprintf(buf, sizeof(buf), "%s:%u", gsm_ch->board->name, gsm_ch->pos_on_board);
			snprintf(alias, sizeof(alias), "chan-%lu", (unsigned long int)count);
			ast_cli(a->fd, "| %-*lu | %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
					number_fl, (unsigned long int)count++,
					alias_fl, (gsm_ch->alias?gsm_ch->alias:alias),
					device_fl, buf,
					module_fl, pg_gsm_module_type_to_string(gsm_ch->gsm_module_type),
					power_fl, AST_CLI_ONOFF(gsm_ch->flags.enable),
					sim_fl, (gsm_ch->flags.sim_present)?("inserted"):(""),
					reg_fl, reg_status_print_short(gsm_ch->reg_stat));
			ast_mutex_unlock(&gsm_ch->lock);
		}
		ast_cli(a->fd, "  Total %lu GSM channel%s\n", (unsigned long int)count, ESS(count));
	} else 
		ast_cli(a->fd, "  No GSM channels found\n");

	ast_mutex_unlock(&pg_lock);

	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_show_channels()
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
// pg_cli_config_actions()
//------------------------------------------------------------------------------
static char *pg_cli_config_actions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char dstfn[64];
	char newfile[PATH_MAX];
	char oldfile[PATH_MAX];
	int sz;

	char *gline;
	char *gargv[AST_MAX_ARGS];
	int gargc;

	switch (cmd)
	{
		//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
		case CLI_INIT:
			e->command = "polygator config [save|copy|delete]";
			sprintf(pg_cli_config_actions_usage, "Usage: polygator config <operation>=\"save\"|\"copy\"|\"load\"|\"delete\"\n");
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
		sprintf(pg_cli_config_actions_usage, "Usage: polygator config <operation>=\"save\"|\"copy\"|\"load\"|\"delete\"\n");
		return CLI_SHOWUSAGE;
	}

	
	if (!strcasecmp(a->argv[2], "save")) {
		// save config file
		// get filename for store configuration
		if (a->argv[3]) {
			// check for file name leaded "polygator"
			if (!strncasecmp(a->argv[3], "polygator", strlen("polygator")))
				sprintf(dstfn, "%s", a->argv[3]);
			else {
				ast_cli(a->fd, "  -- bad file name \"%s\" - must begin \"polygator\"\n", a->argv[3]);
				return CLI_SUCCESS;
			}
		} else
			// use default filename "polygator.conf"
			sprintf(dstfn, "%s", pg_config_file);
		// build full path file name
		// new file -- file to create
		// old file -- file to backup
		sprintf(newfile, "%s/%s", ast_config_AST_CONFIG_DIR, dstfn);
		sprintf(oldfile, "%s/%s.bak", ast_config_AST_CONFIG_DIR, dstfn);
		// if new file is existing file store as backup file
		if (!rename(newfile, oldfile))
			ast_cli(a->fd, "  -- file \"%s\" exist - stored as \"%s.bak\"\n", dstfn, dstfn);
		// build new configuration file
		if((sz = pg_build_config_file(dstfn)) > 0)
			ast_cli(a->fd, "  -- configuration saved to \"%s\" - %d bytes\n", dstfn, sz);
		else
			ast_cli(a->fd, "  -- can't build \"%s\" file\n", dstfn);
	} else if(!strcasecmp(a->argv[2], "copy")) {
		// copy config file
		ast_cli(a->fd, "  -- operation not implemented\n");
	} else if(!strcasecmp(a->argv[2], "delete")) {
		// delete config file
		// get filename for deleting
		if (!a->argv[3]) {
			sprintf(pg_cli_config_actions_usage, "Usage: polygator config delete <file>\n");
			return CLI_SHOWUSAGE;
		}
		if (!strcmp(a->argv[3], pg_config_file)) {
			sprintf(pg_cli_config_actions_usage, "Usage: polygator config delete <file>\n");
			ast_cli(a->fd, "  -- file \"%s\" - cannot be deleted\n", pg_config_file);
			return CLI_SUCCESS;
		}
		// build full path name for deleting file
		sprintf(oldfile, "%s/%s", ast_config_AST_CONFIG_DIR, a->argv[3]);
		// remove file from filesystem
		if (unlink(oldfile) < 0)
			ast_cli(a->fd, "  -- can't delete file \"%s\": %s\n", a->argv[3], strerror(errno));
		ast_cli(a->fd, "  -- file \"%s\" deleted\n", a->argv[3]);
	} else {
		ast_cli(a->fd, "  -- unknown operation - %s\n", a->argv[2]);
		sprintf(pg_cli_config_actions_usage, "Usage: polygator config <operation>=\"save\"|\"copy\"|\"load\"|\"delete\"\n");
		return CLI_SHOWUSAGE;
	}
	return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
// end of pg_cli_config_actions()
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
				vin_set_pram(&vin->context, "%s/polygator/edspPRAMfw_%s.bin", ast_config_AST_DATA_DIR, vin->firmware);
				vin_set_dram(&vin->context, "%s/polygator/edspDRAMfw_%s.bin", ast_config_AST_DATA_DIR, vin->firmware);
				vin_set_alm_dsp_ab(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->almab);
				vin_set_alm_dsp_cd(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->almcd);
				vin_set_cram(&vin->context, "%s/polygator/%s", ast_config_AST_DATA_DIR, vin->cram);

				vin->state = PG_VINETIC_STATE_IDLE;
				break;
			case PG_VINETIC_STATE_IDLE:
				ast_debug(3, "vinetic=\"%s\": idle\n", vin->name);
				// open
				if (vin_open(&vin->context) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_open(): %s\n", vin->name, vin_error_str(&vin->context));
					ast_mutex_unlock(&vin->lock);
					goto pg_vinetic_workthread_end;
				}
				// disable polling
				if (vin_poll_set(&vin->context, 0) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_poll_set(0): %s\n", vin->name, vin_error_str(&vin->context));
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
				if (vin_poll_set(&vin->context, 1) < 0) {
					ast_log(LOG_ERROR, "vinetic=\"%s\": vin_poll_set(1): %s\n", vin->name, vin_error_str(&vin->context));
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

				vin->state = PG_VINETIC_STATE_RUN;
				ast_debug(3, "vinetic=\"%s\": run\n", vin->name);
				break;
			case PG_VINETIC_STATE_RUN:
				ast_mutex_unlock(&vin->lock);
				sleep(1);
				ast_mutex_lock(&vin->lock);
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
	vin_poll_set(&vin->context, 0);
	vin_close(&vin->context);
	ast_mutex_unlock(&vin->lock);
	ast_debug(4, "vinetic=\"%s\": thread stop\n", vin->name);
	vin->thread = AST_PTHREADT_NULL;
	return NULL;
}
//------------------------------------------------------------------------------
// end of pg_vinetic_workthread()
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// pg_atexit()
//------------------------------------------------------------------------------
static void pg_atexit(void)
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
	unsigned int index;
	unsigned int vio;

	char path[PATH_MAX];
	
	struct pg_board *brd;
	struct pg_gsm_channel *gsm_ch;
	struct pg_vinetic *vin;
	struct pg_rtp_channel *rtp;

	struct ast_config *ast_cfg;
	struct ast_flags ast_cfg_flags;

	int is_wait;

	ast_mutex_lock(&pg_lock);

	ast_verbose("Polygator: module \"%s\" loading...\n", VERSION);

	gettimeofday(&pg_start_time, NULL);

	// retrieve configuration from file
	ast_cfg_flags.flags = 0;
	if (!(ast_cfg = ast_config_load(pg_config_file, ast_cfg_flags))) {
		ast_log(LOG_ERROR, "ast_config_load(%s): failed\n", pg_config_file);
		goto pg_load_error;
	}

	// register atexit function
	if (ast_register_atexit(pg_atexit) < 0) {
		ast_log(LOG_ERROR, "unable to register atexit function\n");
		goto pg_load_error;
	}
	pg_atexit_registered = 1;

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
				ast_verbose("  found GSM channel=\"%s\"\n", name);
				if (!(gsm_ch = ast_calloc(1, sizeof(struct pg_gsm_channel)))) {
					ast_log(LOG_ERROR, "can't get memory for struct pg_gsm_channel\n");
					goto pg_load_error;
				}
				pg_gsm_tech_neededed = 1;
				// add channel into board channel list
				AST_LIST_INSERT_TAIL(&brd->gsm_channel_list, gsm_ch, pg_board_gsm_channel_list_entry);
				// add channel into general channel list
				AST_LIST_INSERT_TAIL(&pg_general_gsm_channel_list, gsm_ch, pg_general_gsm_channel_list_entry);
				// init GSM channel
				ast_mutex_init(&gsm_ch->lock);
				gsm_ch->thread = AST_PTHREADT_NULL;
				gsm_ch->pos_on_board = pos;
				gsm_ch->board = brd;
				gsm_ch->name = ast_strdup(name);
				gsm_ch->gsm_module_type = pg_get_gsm_module_type(type);
			} else if (sscanf(buf, "VIN%uRTP%u %[0-9A-Za-z-]", &index, &pos, name) == 3) {
				ast_verbose("  found RTP channel=\"%s\"\n", name);
				if ((vin = pg_get_vinetic_from_board(brd, index))) {
					if (!(rtp = ast_calloc(1, sizeof(struct pg_rtp_channel)))) {
						ast_log(LOG_ERROR, "can't get memory for struct pg_rtp_channel\n");
						goto pg_load_error;
					}
					// add RTP channel into vinetic RTP channel list
					AST_LIST_INSERT_TAIL(&vin->rtp_channel_list, rtp, pg_vinetic_rtp_channel_list_entry);
					// init RTP channel
					rtp->vinetic = vin;
					rtp->pos_on_vinetic = pos;
					rtp->name = ast_strdup(name);
				} else {
					ast_log(LOG_WARNING, "PG board=\"%s\": VIN%u not found\n", brd->name, index);
				}
			} else if (sscanf(buf, "VIN%u %[0-9A-Za-z-]", &pos, name) == 2) {
				ast_verbose("  found vinetic=\"%s\"\n", name);
				if (!(vin = ast_calloc(1, sizeof(struct pg_vinetic)))) {
					ast_log(LOG_ERROR, "can't get memory for struct pg_vinetic\n");
					goto pg_load_error;
				}
				// add vinetic into board vinetic list
				AST_LIST_INSERT_TAIL(&brd->vinetic_list, vin, pg_board_vinetic_list_entry);
				// init VINETIC
				ast_mutex_init(&vin->lock);
				vin->thread = AST_PTHREADT_NULL;
				vin->pos_on_board = pos;
				vin->board = brd;
				vin->name = ast_strdup(name);
				vin->firmware = "RTP_0_15_56_V14";
				vin->almab = "ALM_2484_AB_01.dwl";
				vin->almcd = "ALM_2484_CD_01.dwl";
				vin->cram = "cram.byt";
				vin->state = PG_VINETIC_STATE_INIT;
				vin->run = 1;
				// start vinetic workthread
				if (ast_pthread_create_detached(&vin->thread, NULL, pg_vinetic_workthread, vin) < 0) {
					ast_log(LOG_ERROR, "can't start workthread for vinetic=\"%s\"\n", vin->name);
					vin->thread = AST_PTHREADT_NULL;
					goto pg_load_error;
				}
			} else {
				ast_verbose("%s\n", buf);
			}
		}
		fclose(fp);
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

	// destroy configuration environment
	ast_config_destroy(ast_cfg);

	ast_verbose("Polygator: module loaded successfull\n");
	ast_mutex_unlock(&pg_lock);
	return AST_MODULE_LOAD_SUCCESS;

pg_load_error:

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

	// destroy general GSM channel list
	while ((gsm_ch = AST_LIST_REMOVE_HEAD(&pg_general_gsm_channel_list, pg_general_gsm_channel_list_entry)));

	// destroy general board list
	while ((brd = AST_LIST_REMOVE_HEAD(&pg_general_board_list, pg_general_board_list_entry)))
	{
		// destroy board GSM channel list
		while ((gsm_ch = AST_LIST_REMOVE_HEAD(&brd->gsm_channel_list, pg_board_gsm_channel_list_entry)))
		{
			if (gsm_ch->name) ast_free(gsm_ch->name);
			if (gsm_ch->alias) ast_free(gsm_ch->alias);
			ast_free(gsm_ch);
		}
		// destroy board vinetic list
		while ((vin = AST_LIST_REMOVE_HEAD(&brd->vinetic_list, pg_board_vinetic_list_entry)))
		{
			// destroy vinetic RTP channel list
			while ((rtp = AST_LIST_REMOVE_HEAD(&vin->rtp_channel_list, pg_vinetic_rtp_channel_list_entry)))
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
	struct pg_board *brd;
	struct pg_gsm_channel *gsm_ch;
	struct pg_vinetic *vin;
	struct pg_rtp_channel *rtp;

	int is_wait;

	ast_mutex_lock(&pg_lock);

	// unregistering Polygator CLI interface
	ast_cli_unregister_multiple(pg_cli, sizeof(pg_cli)/sizeof(pg_cli[0]));
	ast_verbose("Polygator: CLI unregistered\n");

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

	// destroy general GSM channel list
	while ((gsm_ch = AST_LIST_REMOVE_HEAD(&pg_general_gsm_channel_list, pg_general_gsm_channel_list_entry)));

	// destroy general board list
	while ((brd = AST_LIST_REMOVE_HEAD(&pg_general_board_list, pg_general_board_list_entry)))
	{
		// destroy board GSM channel list
		while ((gsm_ch = AST_LIST_REMOVE_HEAD(&brd->gsm_channel_list, pg_board_gsm_channel_list_entry)))
		{
			if (gsm_ch->name) ast_free(gsm_ch->name);
			if (gsm_ch->alias) ast_free(gsm_ch->alias);
			ast_free(gsm_ch);
		}
		// destroy board vinetic list
		while ((vin = AST_LIST_REMOVE_HEAD(&brd->vinetic_list, pg_board_vinetic_list_entry)))
		{
			// destroy vinetic RTP channel list
			while ((rtp = AST_LIST_REMOVE_HEAD(&vin->rtp_channel_list, pg_vinetic_rtp_channel_list_entry)))
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
	ast_unregister_atexit(pg_atexit);

	ast_verbose("Polygator: module unloaded successfull\n");
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
	"Polygator",
	.load = pg_load,
	.unload = pg_unload,
);
//------------------------------------------------------------------------------

/******************************************************************************/
/* end of chan_pg.c                                                           */
/******************************************************************************/
