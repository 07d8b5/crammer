// SPDX-License-Identifier: MIT
#include "config.h"
#include "model.h"
#include "parser.h"
#include "rng.h"
#include "term.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct runtime {
	size_t order_pos;
	size_t group_index;
	size_t item_pos;
	size_t item_index;
	u64 group_end;
	int pending_switch;
};

static struct Session g_session;
static struct Rng g_rng;
static size_t g_group_order[MAX_GROUPS];
static size_t g_item_order[MAX_ITEMS_PER_GROUP];
static FILE *g_log;

static int now_ms(u64 *out_ms)
{
	if (!assert_ptr(out_ms))
		return -1;
	if (!assert_ok(((size_t)out_ms % sizeof(u64)) == 0))
		return -1;

	struct timespec ts;
	int rc = clock_gettime(CLOCK_MONOTONIC, &ts);

	if (rc != 0)
		return -1;
	*out_ms = (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000ULL);
	return 0;
}

static int print_usage(const char *prog)
{
	if (!assert_ptr(prog))
		return -1;

	int rc = fprintf(stdout, "Usage: %s <session-file>\n", prog);

	if (rc < 0)
		return -1;
	rc = fprintf(stdout, "       %s -h\n\n", prog);
	if (rc < 0)
		return -1;
	rc = fprintf(stdout,
		     "Keys: Enter/Space/alnum = next, Ctrl+C = quit\n");
	if (rc < 0)
		return -1;
	return 0;
}

static int log_write(const char *tag, const char *msg)
{
	if (!assert_ptr(tag))
		return -1;
	if (!assert_ptr(msg))
		return -1;
	if (!assert_ptr(g_log))
		return -1;

	struct timespec ts;
	int rc = clock_gettime(CLOCK_REALTIME, &ts);

	if (rc != 0)
		return -1;

	struct tm tm_val;
	const struct tm *tm_ptr = localtime_r(&ts.tv_sec, &tm_val);

	if (!assert_ptr(tm_ptr))
		return -1;

	char timebuf[32];
	size_t tlen = strftime(timebuf, sizeof(timebuf),
			       "%Y-%m-%d %H:%M:%S", &tm_val);
	if (!assert_ok(tlen > 0))
		return -1;

	char line[256];

	rc = snprintf(line, sizeof(line), "%s.%03ld [%s] %s\n",
		      timebuf, ts.tv_nsec / 1000000L, tag, msg);
	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(line)))
		return -1;

	size_t written = fwrite(line, 1, (size_t)rc, g_log);

	if (!assert_ok(written == (size_t)rc))
		return -1;
	rc = fflush(g_log);
	if (rc != 0)
		return -1;
	return 0;
}

static int log_simple(const char *tag, const char *msg)
{
	if (!assert_ptr(tag))
		return -1;
	if (!assert_ptr(msg))
		return -1;
	if (!g_log)
		return 0;
	return log_write(tag, msg);
}

static int log_key(int key)
{
	if (!assert_ok(key >= 0))
		return -1;
	if (!assert_ok(key <= 255))
		return -1;
	if (!g_log)
		return 0;

	char msg[64];
	int rc = snprintf(msg, sizeof(msg), "key=%d", key);

	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(msg)))
		return -1;
	return log_write("key", msg);
}

static int log_prompt(size_t group_index, size_t item_index)
{
	if (!assert_ok(group_index < MAX_GROUPS))
		return -1;
	if (!assert_ok(item_index < MAX_ITEMS_TOTAL))
		return -1;
	if (!g_log)
		return 0;

	char msg[96];
	int rc = snprintf(msg, sizeof(msg), "group=%zu item=%zu",
		      group_index, item_index);
	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(msg)))
		return -1;
	return log_write("prompt", msg);
}

static int log_group(const char *tag, size_t group_index)
{
	if (!assert_ptr(tag))
		return -1;
	if (!assert_ok(group_index < MAX_GROUPS))
		return -1;
	if (!g_log)
		return 0;

	char msg[64];
	int rc = snprintf(msg, sizeof(msg), "group=%zu", group_index);

	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(msg)))
		return -1;
	return log_write(tag, msg);
}

static int log_shuffle(const char *tag, size_t group_index)
{
	if (!assert_ptr(tag))
		return -1;
	if (!assert_ok(group_index < MAX_GROUPS))
		return -1;
	if (!g_log)
		return 0;

	char msg[64];
	int rc = snprintf(msg, sizeof(msg), "group=%zu", group_index);

	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(msg)))
		return -1;
	return log_write(tag, msg);
}

static int draw_prompt(size_t item_index)
{
	if (!assert_ok(item_index < g_session.item_count))
		return -1;
	if (!assert_ok(g_session.buffer_len > 0))
		return -1;

	struct Item item = g_session.items[item_index];

	if (!assert_ok(item.length > 0))
		return -1;
	if (!assert_ok((size_t)item.offset + (size_t)item.length <=
		       g_session.buffer_len))
		return -1;

	int rc = term_clear_screen();

	if (rc != 0)
		return -1;

	const char *text = g_session.buffer + item.offset;
	size_t written = fwrite(text, 1, item.length, stdout);

	if (!assert_ok(written == item.length))
		return -1;
	rc = fputc('\n', stdout);
	if (!assert_ok(rc != EOF))
		return -1;
	rc = fflush(stdout);
	if (rc != 0)
		return -1;
	return 0;
}

static int is_advance_key(int key)
{
	if (!assert_ok(key >= 0))
		return 0;
	if (!assert_ok(key <= 255))
		return 0;
	if (key == ' ' || key == '\r' || key == '\n')
		return 1;
	return isalnum((unsigned char)key) != 0;
}

static int init_group_order(void)
{
	if (!assert_ok(g_session.group_count <= MAX_GROUPS))
		return -1;
	if (!assert_ok(g_session.group_count > 0))
		return -1;

	for (size_t i = 0; i < MAX_GROUPS; i++) {
		if (i < g_session.group_count)
			g_group_order[i] = i;
		else
			g_group_order[i] = 0;
	}
	return 0;
}

static int init_item_order(size_t group_index)
{
	if (!assert_ok(group_index < g_session.group_count))
		return -1;
	if (!assert_ok(MAX_ITEMS_PER_GROUP > 0))
		return -1;

	size_t count = g_session.groups[group_index].item_count;
	size_t start = g_session.groups[group_index].item_start;

	if (!assert_ok(count > 0))
		return -1;
	if (!assert_ok(count <= MAX_ITEMS_PER_GROUP))
		return -1;

	for (size_t i = 0; i < MAX_ITEMS_PER_GROUP; i++) {
		if (i < count)
			g_item_order[i] = start + i;
		else
			g_item_order[i] = 0;
	}
	return 0;
}

static int select_next_group(struct runtime *rt)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ok(g_session.group_count > 0))
		return -1;

	if (rt->order_pos >= g_session.group_count) {
		int rc = rng_shuffle_groups(&g_rng, g_group_order,
					 g_session.group_count);
		if (rc != 0)
			return -1;
		rt->order_pos = 0;
		rc = log_simple("shuffle", "groups");
		if (rc != 0)
			return -1;
	}
	rt->group_index = g_group_order[rt->order_pos];
	rt->order_pos++;
	return 0;
}

static int select_next_item(struct runtime *rt)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ok(rt->group_index < g_session.group_count))
		return -1;

	size_t count = g_session.groups[rt->group_index].item_count;

	if (!assert_ok(count > 0))
		return -1;

	if (rt->item_pos >= count)
		rt->item_pos = 0;
	rt->item_index = g_item_order[rt->item_pos];
	return 0;
}

static int update_group_timer(struct runtime *rt)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ok(rt->group_index < g_session.group_count))
		return -1;

	unsigned int seconds = g_session.groups[rt->group_index].seconds;

	if (!assert_ok(seconds > 0))
		return -1;

	u64 now = 0;
	int rc = now_ms(&now);

	if (rc != 0)
		return -1;
	rt->group_end = now + (u64)seconds * 1000ULL;
	return 0;
}

static int advance_prompt(struct runtime *rt, int due_to_switch)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ok(rt->group_index < g_session.group_count))
		return -1;

	size_t count = g_session.groups[rt->group_index].item_count;

	if (!assert_ok(count > 0))
		return -1;

	if (due_to_switch) {
		int rc = init_item_order(rt->group_index);

		if (rc != 0)
			return -1;
		rc = rng_shuffle_items(&g_rng, g_item_order, count);
		if (rc != 0)
			return -1;
		rt->item_pos = 0;
		rc = update_group_timer(rt);
		if (rc != 0)
			return -1;
		rc = log_group("group", rt->group_index);
		if (rc != 0)
			return -1;
	} else {
		rt->item_pos++;
		if (rt->item_pos >= count) {
			int rc = rng_shuffle_items(&g_rng, g_item_order,
						   count);
			if (rc != 0)
				return -1;
			rt->item_pos = 0;
			rc = log_shuffle("items", rt->group_index);
			if (rc != 0)
				return -1;
		}
	}

	int rc = select_next_item(rt);

	if (rc != 0)
		return -1;
	rc = draw_prompt(rt->item_index);
	if (rc != 0)
		return -1;
	rc = log_prompt(rt->group_index, rt->item_index);
	if (rc != 0)
		return -1;
	return 0;
}

static int update_expiry(struct runtime *rt, u64 *remaining_ms)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ptr(remaining_ms))
		return -1;
	if (!assert_ok(rt->group_index < g_session.group_count))
		return -1;

	*remaining_ms = 0;
	if (rt->pending_switch)
		return 0;

	u64 now = 0;
	int rc = now_ms(&now);

	if (rc != 0)
		return -1;
	if (now >= rt->group_end) {
		rt->pending_switch = 1;
		rc = log_group("expired", rt->group_index);
		if (rc != 0)
			return -1;
		return 0;
	}
	*remaining_ms = rt->group_end - now;
	return 0;
}

static int read_key(struct runtime *rt, u64 remaining_ms, int *key_out)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ptr(key_out))
		return -1;
	if (!assert_ok(remaining_ms <= 86400000ULL))
		return -1;

	int timeout = rt->pending_switch ? -1 : (int)remaining_ms;
	int rc = term_read_key_timeout(timeout, key_out);

	if (rc < 0)
		return -1;
	return rc;
}

static int handle_key(struct runtime *rt, int key, int *advanced)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ptr(advanced))
		return -1;

	int rc = log_key(key);

	if (rc != 0)
		return -1;
	if (key == 3)
		return 1;
	if (!is_advance_key(key))
		return 0;

	if (rt->pending_switch) {
		rc = select_next_group(rt);
		if (rc != 0)
			return -1;
		rt->pending_switch = 0;
		rc = advance_prompt(rt, 1);
		if (rc != 0)
			return -1;
	} else {
		rc = advance_prompt(rt, 0);
		if (rc != 0)
			return -1;
	}
	*advanced = 1;
	return 0;
}

static int run_wait_loop(struct runtime *rt, int *advanced)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ptr(advanced))
		return -1;

	for (size_t wait = 0; wait < MAX_WAIT_LOOPS; wait++) {
		u64 remaining_ms = 0;
		int rc = update_expiry(rt, &remaining_ms);

		if (rc != 0)
			return -1;
		int key = 0;

		rc = read_key(rt, remaining_ms, &key);
		if (rc < 0)
			return -1;
		if (rc == 0)
			continue;
		int key_rc = handle_key(rt, key, advanced);

		if (key_rc < 0)
			return -1;
		if (key_rc > 0 || *advanced)
			return key_rc;
	}
	return -1;
}

static int run_loop(struct runtime *rt)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ok(g_session.group_count > 0))
		return -1;

	for (size_t step = 1; step < MAX_PROMPTS_PER_RUN; step++) {
		int advanced = 0;
		int rc = run_wait_loop(rt, &advanced);

		if (rc < 0)
			return -1;
		if (rc > 0)
			return 0;
		if (!advanced) {
			rc = log_simple("error", "wait loop exceeded");
			if (rc != 0)
				return -1;
			return -1;
		}
	}
	return 0;
}

static int init_runtime(struct runtime *rt)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ok(g_session.group_count > 0))
		return -1;

	rt->order_pos = 0;
	rt->group_index = 0;
	rt->item_pos = 0;
	rt->item_index = 0;
	rt->group_end = 0;
	rt->pending_switch = 0;

	int rc = init_group_order();

	if (rc != 0)
		return -1;
	rc = rng_shuffle_groups(&g_rng, g_group_order,
				g_session.group_count);
	if (rc != 0)
		return -1;
	rc = select_next_group(rt);
	if (rc != 0)
		return -1;
	rc = init_item_order(rt->group_index);
	if (rc != 0)
		return -1;

	size_t count = g_session.groups[rt->group_index].item_count;

	if (!assert_ok(count > 0))
		return -1;
	rc = rng_shuffle_items(&g_rng, g_item_order, count);
	if (rc != 0)
		return -1;
	rc = select_next_item(rt);
	if (rc != 0)
		return -1;
	rc = draw_prompt(rt->item_index);
	if (rc != 0)
		return -1;
	rc = log_prompt(rt->group_index, rt->item_index);
	if (rc != 0)
		return -1;
	rc = update_group_timer(rt);
	if (rc != 0)
		return -1;
	return 0;
}

static int setup_session(const char *path, char *err_buf, size_t err_len)
{
	if (!assert_ptr(path))
		return -1;
	if (!assert_ptr(err_buf))
		return -1;
	if (!assert_ok(err_len > 0))
		return -1;

	int rc = parse_session_file(path, &g_session, err_buf, err_len);

	if (rc != 0) {
		int prc = fprintf(stderr, "Error: %s\n", err_buf);

		if (prc < 0)
			return -1;
		return -1;
	}
	return 0;
}

static int open_log(void)
{
	if (!assert_ok(g_session.group_count <= MAX_GROUPS))
		return -1;
	if (!assert_ok(g_session.item_count <= MAX_ITEMS_TOTAL))
		return -1;

	g_log = fopen("cram.log", "a");
	if (!g_log) {
		const char *err = strerror(errno);

		if (!err)
			err = "unknown error";
		int rc = fprintf(stderr,
				 "Warning: failed to open cram.log: %s\n",
				 err);
		if (rc < 0)
			return -1;
		return 0;
	}
	return log_simple("start", "session started");
}

static int run_with_terminal(struct runtime *rt, char *err_buf, size_t err_len)
{
	if (!assert_ptr(rt))
		return -1;
	if (!assert_ptr(err_buf))
		return -1;
	if (!assert_ok(err_len > 0))
		return -1;

	struct TermState term = {0};
	int rc = term_enter_raw(&term, err_buf, err_len);

	if (rc != 0) {
		int prc = fprintf(stderr, "Error: %s\n", err_buf);

		if (prc < 0)
			return -1;
		return -1;
	}
	rc = term_hide_cursor();
	if (rc != 0)
		return -1;

	rc = run_loop(rt);

	int restore_rc = term_restore(&term);
	int show_rc = term_show_cursor();
	int clear_rc = term_clear_screen();

	if (!assert_ok(restore_rc == 0))
		return -1;
	if (!assert_ok(show_rc == 0))
		return -1;
	if (!assert_ok(clear_rc == 0))
		return -1;
	return rc;
}

static int close_log(void)
{
	if (!assert_ok(g_session.group_count <= MAX_GROUPS))
		return -1;
	if (!assert_ok(g_session.item_count <= MAX_ITEMS_TOTAL))
		return -1;
	if (!g_log)
		return 0;

	int rc = log_simple("exit", "session end");

	if (rc != 0)
		return -1;
	rc = fclose(g_log);
	if (rc != 0)
		return -1;
	g_log = NULL;
	return 0;
}

static int run_program(const char *path)
{
	if (!assert_ptr(path))
		return -1;
	if (!assert_ok(path[0] != '\0'))
		return -1;

	char err_buf[256];
	int rc = setup_session(path, err_buf, sizeof(err_buf));

	if (rc != 0)
		return -1;
	rc = open_log();
	if (rc != 0)
		return -1;
	rc = rng_init(&g_rng);
	if (rc != 0)
		return -1;

	struct runtime rt;

	rc = init_runtime(&rt);
	if (rc != 0)
		return -1;
	rc = run_with_terminal(&rt, err_buf, sizeof(err_buf));
	if (rc != 0)
		return -1;
	rc = close_log();
	if (rc != 0)
		return -1;
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 &&
	    (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
		int rc = print_usage(argv[0]);

		return rc == 0 ? 0 : 1;
	}
	if (argc != 2) {
		(void)print_usage(argv[0]);
		return 1;
	}
	return run_program(argv[1]) == 0 ? 0 : 1;
}
