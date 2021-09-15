#ifndef HOOK_H
#define HOOK_H
#include "strbuf.h"
#include "strvec.h"
#include "run-command.h"
#include "list.h"

struct hook {
	struct list_head list;
	/*
	 * The friendly name of the hook. NULL indicates the hook is from the
	 * hookdir.
	 */
	char *name;

	/*
	 * Use this to keep state for your feed_pipe_fn if you are using
	 * run_hooks_opt.feed_pipe. Otherwise, do not touch it.
	 */
	void *feed_pipe_cb_data;
};

struct run_hooks_opt
{
	/* Environment vars to be set for each hook */
	struct strvec env;

	/* Args to be passed to each hook */
	struct strvec args;

	/*
	 * Number of threads to parallelize across. Set to 0 to use the
	 * 'hook.jobs' config or, if that config is unset, the number of cores
	 * on the system.
	 */
	int jobs;

	/*
	 * Resolve and run the "absolute_path(hook)" instead of
	 * "hook". Used for "git worktree" hooks
	 */
	int absolute_path;

	/* Path to initial working directory for subprocess */
	const char *dir;

	/* Path to file which should be piped to stdin for each hook */
	const char *path_to_stdin;

	/*
	 * Callback and state pointer to ask for more content to pipe to stdin.
	 * Will be called repeatedly, for each hook. See
	 * hook.c:pipe_from_stdin() for an example. Keep per-hook state in
	 * hook.feed_pipe_cb_data (per process). Keep initialization context in
	 * feed_pipe_ctx (shared by all processes).
	 *
	 * See 'pipe_from_string_list()' for info about how to specify a
	 * string_list as the stdin input instead of writing your own handler.
	 */
	feed_pipe_fn feed_pipe;
	void *feed_pipe_ctx;

	/*
	 * Populate this to capture output and prevent it from being printed to
	 * stderr. This will be passed directly through to
	 * run_command:run_parallel_processes(). See t/helper/test-run-command.c
	 * for an example.
	 */
	consume_sideband_fn consume_sideband;

	/*
	 * A pointer which if provided will be set to 1 or 0 depending
	 * on if a hook was invoked (i.e. existed), regardless of
	 * whether or not that was successful. Used for avoiding
	 * TOCTOU races in code that would otherwise call hook_exist()
	 * after a "maybe hook run" to see if a hook was invoked.
	 */
	int *invoked_hook;
};

#define RUN_HOOKS_OPT_INIT_SERIAL { \
	.jobs = 1, \
	.env = STRVEC_INIT, \
	.args = STRVEC_INIT, \
}

#define RUN_HOOKS_OPT_INIT_PARALLEL { \
	.jobs = 0, \
	.env = STRVEC_INIT, \
	.args = STRVEC_INIT, \
}

struct hook_cb_data {
	/* rc reflects the cumulative failure state */
	int rc;
	const char *hook_name;
	struct list_head *head;
	struct hook *run_me;
	struct run_hooks_opt *options;
	int *invoked_hook;
};

/**
 * Returns the path to the hook file, or NULL if the hook is missing
 * or disabled. Note that this points to static storage that will be
 * overwritten by further calls to find_hook().
 */
const char *find_hook(const char *name);

/**
 * Provides a linked list of 'struct hook' detailing commands which should run
 * in response to the 'hookname' event, in execution order.
 */
struct list_head *list_hooks(const char *hookname);

/**
 * A boolean version of list_hooks()
 */
int hook_exists(const char *hookname);

/**
 * Clear data from an initialized "struct run_hooks-opt".
 */
void run_hooks_opt_clear(struct run_hooks_opt *o);

/**
 * Takes an already resolved hook found via find_hook() and runs
 * it. Does not call run_hooks_opt_clear() for you, but does call
 * clear_hook_list().
 *
 * See run_hooks_oneshot() for the simpler one-shot API.
 */
int run_hooks(const char *hookname, struct list_head *hooks,
	      struct run_hooks_opt *options);

/**
 * Empties the list at 'head', calling 'free_hook()' on each
 * entry. Called implicitly by run_hooks() (and run_hooks_oneshot()).
 */
void clear_hook_list(struct list_head *head);

/**
 * Calls find_hook() on your "hook_name" and runs the hooks (if any)
 * with run_hooks().
 *
 * If "options" is provided calls run_hooks_opt_clear() on it for
 * you. If "options" is NULL the default options from
 * RUN_HOOKS_OPT_INIT will be used.
 */
int run_hooks_oneshot(const char *hook_name, struct run_hooks_opt *options);

/**
 * To specify a 'struct string_list', set 'run_hooks_opt.feed_pipe_ctx' to the
 * string_list and set 'run_hooks_opt.feed_pipe' to pipe_from_string_list().
 * This will pipe each string in the list to stdin, separated by newlines.  (Do
 * not inject your own newlines.)
 */
int pipe_from_string_list(struct strbuf *pipe, void *pp_cb, void *pp_task_cb);

#endif
