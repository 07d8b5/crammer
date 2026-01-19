// SPDX-License-Identifier: MIT
#include "app.h"
#include "config.h"
#include "log.h"
#include "model.h"
#include "parser.h"
#include "rng.h"
#include "runner.h"

#include <stdio.h>
#include <string.h>

struct app {
	struct Session session;
	struct Rng rng;
	size_t group_order[MAX_GROUPS];
	size_t item_order[MAX_ITEMS_PER_GROUP];
};

static struct app g_app;

static int setup_session(const char *path)
{
	if (!validate_ptr(path))
		return -1;
	if (!validate_ok(path[0] != '\0'))
		return -1;

	char err_buf[256];
	int rc = parse_session_file(path, &g_app.session,
				 err_buf, sizeof(err_buf));

	if (rc != 0) {
		rc = fprintf(stderr, "Error: %s\n", err_buf);
		if (rc < 0)
			return -1;
		return -1;
	}
	return 0;
}

int app_run_file(const char *path)
{
	if (!validate_ptr(path))
		return -1;

	int rc = setup_session(path);

	if (rc != 0)
		return -1;
	rc = log_open(&g_app.session);
	if (rc != 0)
		return -1;
	rc = log_input(&g_app.session, path);
	if (rc != 0)
		return -1;
	rc = rng_init(&g_app.rng);
	if (rc != 0)
		return -1;
	rc = runner_run(&g_app.session, &g_app.rng,
			g_app.group_order, MAX_GROUPS,
			g_app.item_order, MAX_ITEMS_PER_GROUP);
	if (rc != 0)
		return -1;
	rc = log_close(&g_app.session);
	if (rc != 0)
		return -1;
	return 0;
}
