/* SPDX-License-Identifier: MIT */
#ifndef CRAM_RNG_H
#define CRAM_RNG_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

struct Rng {
  u64 state;
};

int rng_init(struct Rng* rng);
u64 rng_next_u64(struct Rng* rng);
size_t rng_range(struct Rng* rng, size_t upper);
int rng_shuffle_groups(struct Rng* rng, size_t* values, size_t count);
int rng_shuffle_items(struct Rng* rng, size_t* values, size_t count);

#endif
