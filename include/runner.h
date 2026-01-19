/* SPDX-License-Identifier: MIT */
#ifndef CRAM_RUNNER_H
#define CRAM_RUNNER_H

#include <stddef.h>

struct Session;
struct Rng;

int runner_run(struct Session *session,
	       struct Rng *rng,
	       size_t *group_order,
	       size_t group_order_cap,
	       size_t *item_order,
	       size_t item_order_cap);

#endif
