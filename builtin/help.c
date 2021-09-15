/*
 * Builtin help command
 */
#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "exec-cmd.h"
#include "parse-options.h"
#include "run-command.h"
#include "column.h"
#include "config-list.h"
#include "help.h"
#include "alias.h"

#ifndef DEFAULT_HELP_FORMAT
#define DEFAULT_HELP_FORMAT "man"
#endif

static struct man_viewer_list {
	struct man_viewer_list *next;
	char name[FLEX_ARRAY];
} *man_viewer_list;

static struct man_viewer_info_list {
	struct man_viewer_info_list *next;
	const char *info;
	char name[FLEX_ARRAY];
} *man_viewer_info_list;

enum help_format {
	HELP_FORMAT_NONE,
	HELP_FORMAT_MAN,
	HELP_FORMAT_INFO,
	HELP_FORMAT_WEB
};

static const char *html_path;

static int show_all = 0;
static int show_guides = 0;
static enum show_config_type {
	SHOW_CONFIG_UNSET = 0,
	SHOW_CONFIG_HUMAN,
	SHOW_CONFIG_VARS,
	SHOW_CONFIG_SECTIONS,
} show_config;
static int verbose = 1;
static unsigned int colopts;
static enum help_format help_format = HELP_FORMAT_NONE;
static int exclude_guides;
static struct option builtin_help_options[] = {
	OPT_BOOL('a', "all", &show_all, N_("print all available commands")),
	OPT_HIDDEN_BOOL(0, "exclude-guides", &exclude_guides, N_("exclude guides")),
	OPT_BOOL('g', "guides", &show_guides, N_("print list of useful guides")),
	OPT_BOOL('c', "config", &show_config, N_("print all configuration variable names")),
	OPT_SET_INT_F(0, "config-for-completion-vars", &show_config, "",
		      SHOW_CONFIG_VARS, PARSE_OPT_HIDDEN),
	OPT_SET_INT_F(0, "config-for-completion-sections", &show_config, "",
		      SHOW_CONFIG_SECTIONS, PARSE_OPT_HIDDEN),
	OPT_SET_INT('m', "man", &help_format, N_("show man page"), HELP_FORMAT_MAN),
	OPT_SET_INT('w', "web", &help_format, N_("show manual in web browser"),
			HELP_FORMAT_WEB),
	OPT_SET_INT('i', "info", &help_format, N_("show info page"),
			HELP_FORMAT_INFO),
	OPT__VERBOSE(&verbose, N_("print command description")),
	OPT_END(),
};

static const char * const builtin_help_usage[] = {
	N_("git help [-a|--all] [--[no-]verbose]]\n"
	   "         [[-i|--info] [-m|--man] [-w|--web]] [<command>]"),
	N_("git help [-g|--guides]"),
	N_("git help [-c|--config]"),
	NULL
};

struct slot_expansion {
	const char *prefix;
	const char *placeholder;
	void (*fn)(struct string_list *list, const char *prefix);
	int found;
};

static void list_config_help(enum show_config_type type)
{
	struct slot_expansion slot_expansions[] = {
		{ "advice", "*", list_config_advices },
		{ "color.branch", "<slot>", list_config_color_branch_slots },
		{ "color.decorate", "<slot>", list_config_color_decorate_slots },
		{ "color.diff", "<slot>", list_config_color_diff_slots },
		{ "color.grep", "<slot>", list_config_color_grep_slots },
		{ "color.interactive", "<slot>", list_config_color_interactive_slots },
		{ "color.remote", "<slot>", list_config_color_sideband_slots },
		{ "color.status", "<slot>", list_config_color_status_slots },
		{ "fsck", "<msg-id>", list_config_fsck_msg_ids },
		{ "receive.fsck", "<msg-id>", list_config_fsck_msg_ids },
		{ NULL, NULL, NULL }
	};
	const char **p;
	struct slot_expansion *e;
	struct string_list keys = STRING_LIST_INIT_DUP;
	struct string_list keys_uniq = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int i;

	for (p = config_name_list; *p; p++) {
		const char *var = *p;
		struct strbuf sb = STRBUF_INIT;

		for (e = slot_expansions; e->prefix; e++) {

			strbuf_reset(&sb);
			strbuf_addf(&sb, "%s.%s", e->prefix, e->placeholder);
			if (!strcasecmp(var, sb.buf)) {
				e->fn(&keys, e->prefix);
				e->found++;
				break;
			}
		}
		strbuf_release(&sb);
		if (!e->prefix)
			string_list_append(&keys, var);
	}

	for (e = slot_expansions; e->prefix; e++)
		if (!e->found)
			BUG("slot_expansion %s.%s is not used",
			    e->prefix, e->placeholder);

	string_list_sort(&keys);
	for (i = 0; i < keys.nr; i++) {
		const char *var = keys.items[i].string;
		const char *wildcard, *tag, *cut;
		const char *dot = NULL;
		struct strbuf sb = STRBUF_INIT;

		switch (type) {
		case SHOW_CONFIG_HUMAN:
			puts(var);
			continue;
		case SHOW_CONFIG_SECTIONS:
			dot = strchr(var, '.');
			break;
		case SHOW_CONFIG_VARS:
			break;
		case SHOW_CONFIG_UNSET:
			BUG("should not get SHOW_CONFIG_UNSET here");
		}
		wildcard = strchr(var, '*');
		tag = strchr(var, '<');

		if (!dot && !wildcard && !tag) {
			string_list_append(&keys_uniq, var);
			continue;
		}

		if (dot)
			cut = dot;
		else if (wildcard && !tag)
			cut = wildcard;
		else if (!wildcard && tag)
			cut = tag;
		else
			cut = wildcard < tag ? wildcard : tag;

		strbuf_add(&sb, var, cut - var);
		string_list_append(&keys_uniq, sb.buf);
		strbuf_release(&sb);

	}
	string_list_clear(&keys, 0);
	string_list_remove_duplicates(&keys_uniq, 0);
	for_each_string_list_item(item, &keys_uniq)
		puts(item->string);
	string_list_clear(&keys_uniq, 0);
}

static enum help_format parse_help_format(const char *format)
{
	if (!strcmp(format, "man"))
		return HELP_FORMAT_MAN;
	if (!strcmp(format, "info"))
		return HELP_FORMAT_INFO;
	if (!strcmp(format, "web") || !strcmp(format, "html"))
		return HELP_FORMAT_WEB;
	/*
	 * Please update _git_config() in git-completion.bash when you
	 * add new help formats.
	 */
	die(_("unrecognized help format '%s'"), format);
}

static const char *get_man_viewer_info(const char *name)
{
	struct man_viewer_info_list *viewer;

	for (viewer = man_viewer_info_list; viewer; viewer = viewer->next)
	{
		if (!strcasecmp(name, viewer->name))
			return viewer->info;
	}
	return NULL;
}

static int check_emacsclient_version(void)
{
	struct strbuf buffer = STRBUF_INIT;
	struct child_process ec_process = CHILD_PROCESS_INIT;
	const char *argv_ec[] = { "emacsclient", "--version", NULL };
	int version;

	/* emacsclient prints its version number on stderr */
	ec_process.argv = argv_ec;
	ec_process.err = -1;
	ec_process.stdout_to_stderr = 1;
	if (start_command(&ec_process))
		return error(_("Failed to start emacsclient."));

	strbuf_read(&buffer, ec_process.err, 20);
	close(ec_process.err);

	/*
	 * Don't bother checking return value, because "emacsclient --version"
	 * seems to always exits with code 1.
	 */
	finish_command(&ec_process);

	if (!starts_with(buffer.buf, "emacsclient")) {
		strbuf_release(&buffer);
		return error(_("Failed to parse emacsclient version."));
	}

	strbuf_remove(&buffer, 0, strlen("emacsclient"));
	version = atoi(buffer.buf);

	if (version < 22) {
		strbuf_release(&buffer);
		return error(_("emacsclient version '%d' too old (< 22)."),
			version);
	}

	strbuf_release(&buffer);
	return 0;
}

static void exec_woman_emacs(const char *path, const char *page)
{
	if (!check_emacsclient_version()) {
		/* This works only with emacsclient version >= 22. */
		struct strbuf man_page = STRBUF_INIT;

		if (!path)
			path = "emacsclient";
		strbuf_addf(&man_page, "(woman \"%s\")", page);
		execlp(path, "emacsclient", "-e", man_page.buf, (char *)NULL);
		warning_errno(_("failed to exec '%s'"), path);
		strbuf_release(&man_page);
	}
}

static void exec_man_konqueror(const char *path, const char *page)
{
	const char *display = getenv("DISPLAY");
	if (display && *display) {
		struct strbuf man_page = STRBUF_INIT;
		const char *filename = "kfmclient";

		/* It's simpler to launch konqueror using kfmclient. */
		if (path) {
			size_t len;
			if (strip_suffix(path, "/konqueror", &len))
				path = xstrfmt("%.*s/kfmclient", (int)len, path);
			filename = basename((char *)path);
		} else
			path = "kfmclient";
		strbuf_addf(&man_page, "man:%s(1)", page);
		execlp(path, filename, "newTab", man_page.buf, (char *)NULL);
		warning_errno(_("failed to exec '%s'"), path);
		strbuf_release(&man_page);
	}
}

static void exec_man_man(const char *path, const char *page)
{
	if (!path)
		path = "man";
	execlp(path, "man", page, (char *)NULL);
	warning_errno(_("failed to exec '%s'"), path);
}

static void exec_man_cmd(const char *cmd, const char *page)
{
	struct strbuf shell_cmd = STRBUF_INIT;
	strbuf_addf(&shell_cmd, "%s %s", cmd, page);
	execl(SHELL_PATH, SHELL_PATH, "-c", shell_cmd.buf, (char *)NULL);
	warning(_("failed to exec '%s'"), cmd);
	strbuf_release(&shell_cmd);
}

static void add_man_viewer(const char *name)
{
	struct man_viewer_list **p = &man_viewer_list;

	while (*p)
		p = &((*p)->next);
	FLEX_ALLOC_STR(*p, name, name);
}

static int supported_man_viewer(const char *name, size_t len)
{
	return (!strncasecmp("man", name, len) ||
		!strncasecmp("woman", name, len) ||
		!strncasecmp("konqueror", name, len));
}

static void do_add_man_viewer_info(const char *name,
				   size_t len,
				   const char *value)
{
	struct man_viewer_info_list *new_man_viewer;
	FLEX_ALLOC_MEM(new_man_viewer, name, name, len);
	new_man_viewer->info = xstrdup(value);
	new_man_viewer->next = man_viewer_info_list;
	man_viewer_info_list = new_man_viewer;
}

static int add_man_viewer_path(const char *name,
			       size_t len,
			       const char *value)
{
	if (supported_man_viewer(name, len))
		do_add_man_viewer_info(name, len, value);
	else
		warning(_("'%s': path for unsupported man viewer.\n"
			  "Please consider using 'man.<tool>.cmd' instead."),
			name);

	return 0;
}

static int add_man_viewer_cmd(const char *name,
			      size_t len,
			      const char *value)
{
	if (supported_man_viewer(name, len))
		warning(_("'%s': cmd for supported man viewer.\n"
			  "Please consider using 'man.<tool>.path' instead."),
			name);
	else
		do_add_man_viewer_info(name, len, value);

	return 0;
}

static int add_man_viewer_info(const char *var, const char *value)
{
	const char *name, *subkey;
	size_t namelen;

	if (parse_config_key(var, "man", &name, &namelen, &subkey) < 0 || !name)
		return 0;

	if (!strcmp(subkey, "path")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer_path(name, namelen, value);
	}
	if (!strcmp(subkey, "cmd")) {
		if (!value)
			return config_error_nonbool(var);
		return add_man_viewer_cmd(name, namelen, value);
	}

	return 0;
}

static int git_help_config(const char *var, const char *value, void *cb)
{
	if (starts_with(var, "column."))
		return git_column_config(var, value, "help", &colopts);
	if (!strcmp(var, "help.format")) {
		if (!value)
			return config_error_nonbool(var);
		help_format = parse_help_format(value);
		return 0;
	}
	if (!strcmp(var, "help.htmlpath")) {
		if (!value)
			return config_error_nonbool(var);
		html_path = xstrdup(value);
		return 0;
	}
	if (!strcmp(var, "man.viewer")) {
		if (!value)
			return config_error_nonbool(var);
		add_man_viewer(value);
		return 0;
	}
	if (starts_with(var, "man."))
		return add_man_viewer_info(var, value);

	return git_default_config(var, value, cb);
}

static struct cmdnames main_cmds, other_cmds;

static int is_git_command(const char *s)
{
	if (is_builtin(s))
		return 1;

	load_command_list("git-", &main_cmds, &other_cmds);
	return is_in_cmdlist(&main_cmds, s) ||
		is_in_cmdlist(&other_cmds, s);
}

static const char *cmd_to_page(const char *git_cmd)
{
	if (!git_cmd)
		return "git";
	else if (starts_with(git_cmd, "git"))
		return git_cmd;
	else if (is_git_command(git_cmd))
		return xstrfmt("git-%s", git_cmd);
	else
		return xstrfmt("git%s", git_cmd);
}

static void setup_man_path(void)
{
	struct strbuf new_path = STRBUF_INIT;
	const char *old_path = getenv("MANPATH");
	char *git_man_path = system_path(GIT_MAN_PATH);

	/* We should always put ':' after our path. If there is no
	 * old_path, the ':' at the end will let 'man' to try
	 * system-wide paths after ours to find the manual page. If
	 * there is old_path, we need ':' as delimiter. */
	strbuf_addstr(&new_path, git_man_path);
	strbuf_addch(&new_path, ':');
	if (old_path)
		strbuf_addstr(&new_path, old_path);

	free(git_man_path);
	setenv("MANPATH", new_path.buf, 1);

	strbuf_release(&new_path);
}

static void exec_viewer(const char *name, const char *page)
{
	const char *info = get_man_viewer_info(name);

	if (!strcasecmp(name, "man"))
		exec_man_man(info, page);
	else if (!strcasecmp(name, "woman"))
		exec_woman_emacs(info, page);
	else if (!strcasecmp(name, "konqueror"))
		exec_man_konqueror(info, page);
	else if (info)
		exec_man_cmd(info, page);
	else
		warning(_("'%s': unknown man viewer."), name);
}

static void show_man_page(const char *page)
{
	struct man_viewer_list *viewer;
	const char *fallback = getenv("GIT_MAN_VIEWER");

	setup_man_path();
	for (viewer = man_viewer_list; viewer; viewer = viewer->next)
	{
		exec_viewer(viewer->name, page); /* will return when unable */
	}
	if (fallback)
		exec_viewer(fallback, page);
	exec_viewer("man", page);
	die(_("no man viewer handled the request"));
}

static void show_info_page(const char *page)
{
	setenv("INFOPATH", system_path(GIT_INFO_PATH), 1);
	execlp("info", "info", "gitman", page, (char *)NULL);
	die(_("no info viewer handled the request"));
}

static void get_html_page_path(struct strbuf *page_path, const char *page)
{
	struct stat st;
	char *to_free = NULL;

	if (!html_path)
		html_path = to_free = system_path(GIT_HTML_PATH);

	/*
	 * Check that the page we're looking for exists.
	 */
	if (!strstr(html_path, "://")) {
		if (stat(mkpath("%s/%s.html", html_path, page), &st)
		    || !S_ISREG(st.st_mode))
			die("'%s/%s.html': documentation file not found.",
				html_path, page);
	}

	strbuf_init(page_path, 0);
	strbuf_addf(page_path, "%s/%s.html", html_path, page);
	free(to_free);
}

static void open_html(const char *path)
{
	execl_git_cmd("web--browse", "-c", "help.browser", path, (char *)NULL);
}

static void show_html_page(const char *page)
{
	struct strbuf page_path; /* it leaks but we exec bellow */

	get_html_page_path(&page_path, page);

	open_html(page_path.buf);
}

static const char *check_git_cmd(const char* cmd)
{
	char *alias;

	if (is_git_command(cmd))
		return cmd;

	alias = alias_lookup(cmd);
	if (alias) {
		const char **argv;
		int count;

		/*
		 * handle_builtin() in git.c rewrites "git cmd --help"
		 * to "git help --exclude-guides cmd", so we can use
		 * exclude_guides to distinguish "git cmd --help" from
		 * "git help cmd". In the latter case, or if cmd is an
		 * alias for a shell command, just print the alias
		 * definition.
		 */
		if (!exclude_guides || alias[0] == '!') {
			printf_ln(_("'%s' is aliased to '%s'"), cmd, alias);
			free(alias);
			exit(0);
		}
		/*
		 * Otherwise, we pretend that the command was "git
		 * word0 --help". We use split_cmdline() to get the
		 * first word of the alias, to ensure that we use the
		 * same rules as when the alias is actually
		 * used. split_cmdline() modifies alias in-place.
		 */
		fprintf_ln(stderr, _("'%s' is aliased to '%s'"), cmd, alias);
		count = split_cmdline(alias, &argv);
		if (count < 0)
			die(_("bad alias.%s string: %s"), cmd,
			    split_cmdline_strerror(count));
		free(argv);
		UNLEAK(alias);
		return alias;
	}

	if (exclude_guides)
		return help_unknown_cmd(cmd);

	return cmd;
}

int cmd_help(int argc, const char **argv, const char *prefix)
{
	int nongit;
	enum help_format parsed_help_format;
	const char *page;
	int need_config = 0;

	argc = parse_options(argc, argv, prefix, builtin_help_options,
			builtin_help_usage, 0);
	parsed_help_format = help_format;

	/* Incompatible options */
	if (show_all && show_config)
		usage_msg_opt(_("--config and --all cannot be combined"),
			      builtin_help_usage, builtin_help_options);

	if (show_config && show_guides)
		usage_msg_opt(_("--config and --guides cannot be combined"),
			      builtin_help_usage, builtin_help_options);

	/* Options that take no further arguments */
	if (argc && show_config)
		usage_msg_opt(_("--config cannot be combined with command or guide names"),
			      builtin_help_usage, builtin_help_options);
	if (argc && show_guides)
		usage_msg_opt(_("--guides cannot be combined with command or guide names"),
			      builtin_help_usage, builtin_help_options);

	need_config = show_all || show_config;
	if (need_config)
		git_config(git_help_config, NULL);

	if (show_all) {
		if (verbose) {
			setup_pager();
			list_all_cmds_help();
			return 0;
		}
		printf(_("usage: %s%s"), _(git_usage_string), "\n\n");
		load_command_list("git-", &main_cmds, &other_cmds);
		list_commands(colopts, &main_cmds, &other_cmds);
	}

	if (show_guides)
		list_guides_help();

	if (show_all || show_guides) {
		printf("%s\n", _(git_more_info_string));
		return 0;
	}

	switch (show_config) {
	case SHOW_CONFIG_UNSET:
		break;
	case SHOW_CONFIG_VARS:
	case SHOW_CONFIG_SECTIONS:
		list_config_help(show_config);

		return 0;
	case SHOW_CONFIG_HUMAN:
		setup_pager();
		list_config_help(show_config);
		printf("\n%s\n", _("'git help config' for more information"));

		return 0;
	}

	if (!argv[0]) {
		printf(_("usage: %s%s"), _(git_usage_string), "\n\n");
		list_common_cmds_help();
		printf("\n%s\n", _(git_more_info_string));
		return 0;
	}

	setup_git_directory_gently(&nongit);
	git_config(git_help_config, NULL);

	if (parsed_help_format != HELP_FORMAT_NONE)
		help_format = parsed_help_format;
	if (help_format == HELP_FORMAT_NONE)
		help_format = parse_help_format(DEFAULT_HELP_FORMAT);

	argv[0] = check_git_cmd(argv[0]);

	page = cmd_to_page(argv[0]);
	switch (help_format) {
	case HELP_FORMAT_NONE:
	case HELP_FORMAT_MAN:
		show_man_page(page);
		break;
	case HELP_FORMAT_INFO:
		show_info_page(page);
		break;
	case HELP_FORMAT_WEB:
		show_html_page(page);
		break;
	}

	return 0;
}
