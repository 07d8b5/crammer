/* SPDX-License-Identifier: MIT */
#ifndef CRAM_MODEL_H
#define CRAM_MODEL_H

#include <stddef.h>

#include "config.h"

struct Item {
	u32 offset;
	u32 length;
};

struct Group {
	u32 name_offset;
	u32 name_length;
	u32 seconds;
	u32 item_start;
	u32 item_count;
};

struct Session {
	char buffer[MAX_FILE_BYTES + 1];
	size_t buffer_len;
	struct Group groups[MAX_GROUPS];
	size_t group_count;
	struct Item items[MAX_ITEMS_TOTAL];
	size_t item_count;
};

int session_init(struct Session *session);

#endif
