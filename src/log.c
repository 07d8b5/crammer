// SPDX-License-Identifier: MIT
#include "config.h"
#include "log.h"
#include "model.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int g_log_fd = -1;

static int write_all_fd(int fd, const char *buf, size_t len)
{
	if (!validate_ok(fd >= 0))
		return -1;
	if (!validate_ptr(buf))
		return -1;
	if (!validate_ok(len <= MAX_LINE_LEN))
		return -1;

	const char *ptr = buf;
	size_t remaining = len;

	for (size_t i = 0; i < MAX_WRITE_LOOPS; i++) {
		if (remaining == 0)
			break;
		ssize_t n = write(fd, ptr, remaining);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;
		ptr += (size_t)n;
		remaining -= (size_t)n;
	}
	if (remaining != 0)
		return -1;
	return 0;
}

static int log_write(const char *tag, const char *msg)
{
	if (!validate_ptr(tag))
		return -1;
	if (!validate_ptr(msg))
		return -1;
	if (!assert_ok(g_log_fd >= 0))
		return -1;

	struct timespec ts;
	int rc = clock_gettime(CLOCK_REALTIME, &ts);

	if (rc != 0)
		return -1;

	u64 sec = (u64)ts.tv_sec;
	u64 ms = (u64)(ts.tv_nsec / 1000000L);
	char line[256];

	rc = snprintf(line, sizeof(line), "%llu.%03llu [%s] %s\n",
		      (unsigned long long)sec,
		      (unsigned long long)ms, tag, msg);
	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(line)))
		return -1;

	char *ptr = line;
	size_t len = (size_t)rc;

	for (size_t i = 0; i < MAX_WRITE_LOOPS; i++) {
		ssize_t n = write(g_log_fd, ptr, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;
		ptr += (size_t)n;
		len -= (size_t)n;
		if (len == 0)
			break;
	}
	if (len != 0)
		return -1;
	return 0;
}

int log_simple(const char *tag, const char *msg)
{
	if (!validate_ptr(tag))
		return -1;
	if (!validate_ptr(msg))
		return -1;
	if (g_log_fd < 0)
		return 0;
	return log_write(tag, msg);
}

int log_key(int key)
{
	if (!validate_ok(key >= 0))
		return -1;
	if (!validate_ok(key <= 255))
		return -1;
	if (g_log_fd < 0)
		return 0;

	char msg[64];
	int rc = snprintf(msg, sizeof(msg), "key=%d", key);

	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(msg)))
		return -1;
	return log_write("key", msg);
}

int log_prompt(size_t group_index, size_t item_index)
{
	if (!assert_ok(group_index < MAX_GROUPS))
		return -1;
	if (!assert_ok(item_index < MAX_ITEMS_TOTAL))
		return -1;
	if (g_log_fd < 0)
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

int log_group(const char *tag, size_t group_index)
{
	if (!validate_ptr(tag))
		return -1;
	if (!assert_ok(group_index < MAX_GROUPS))
		return -1;
	if (g_log_fd < 0)
		return 0;

	char msg[64];
	int rc = snprintf(msg, sizeof(msg), "group=%zu", group_index);

	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(msg)))
		return -1;
	return log_write(tag, msg);
}

int log_shuffle(const char *tag, size_t group_index)
{
	if (!validate_ptr(tag))
		return -1;
	if (!assert_ok(group_index < MAX_GROUPS))
		return -1;
	if (g_log_fd < 0)
		return 0;

	char msg[64];
	int rc = snprintf(msg, sizeof(msg), "group=%zu", group_index);

	if (!assert_ok(rc > 0))
		return -1;
	if (!assert_ok((size_t)rc < sizeof(msg)))
		return -1;
	return log_write(tag, msg);
}

int log_open(const struct Session *session)
{
	if (!validate_ptr(session))
		return -1;
	if (!assert_ok(session->group_count <= MAX_GROUPS))
		return -1;
	if (!assert_ok(session->item_count <= MAX_ITEMS_TOTAL))
		return -1;

	g_log_fd = open("cram.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (g_log_fd < 0) {
		const char *err = strerror(errno);

		if (!err)
			err = "unknown error";
		char msg[256];
		int rc = snprintf(msg, sizeof(msg),
				  "Warning: failed to open cram.log: %s\n",
				  err);
		if (rc < 0 || (size_t)rc >= sizeof(msg))
			return -1;
		int wrc = write_all_fd(STDERR_FILENO, msg, (size_t)rc);

		if (wrc != 0)
			return -1;
		return 0;
	}
	return log_simple("start", "session started");
}

int log_close(const struct Session *session)
{
	if (!validate_ptr(session))
		return -1;
	if (!assert_ok(session->group_count <= MAX_GROUPS))
		return -1;
	if (!assert_ok(session->item_count <= MAX_ITEMS_TOTAL))
		return -1;
	if (g_log_fd < 0)
		return 0;

	int rc = log_simple("exit", "session end");

	if (rc != 0)
		return -1;
	rc = close(g_log_fd);
	if (rc != 0)
		return -1;
	g_log_fd = -1;
	return 0;
}
