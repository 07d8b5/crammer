// SPDX-License-Identifier: MIT
#include "rng.h"

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

static u64 mix64(u64 x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

int rng_init(struct Rng* rng) {
  if (!validate_ptr(rng))
    return -1;

  u64 seed = 0;
  int fd = open("/dev/urandom", O_RDONLY);

  if (fd >= 0) {
    size_t want = sizeof(seed);
    ssize_t got = read(fd, &seed, want);
    int rc = close(fd);

    if (rc != 0)
      seed = 0;
    if (got != (ssize_t)want)
      seed = 0;
  }
  if (seed == 0) {
    struct timespec ts;
    int rc = clock_gettime(CLOCK_REALTIME, &ts);

    if (rc != 0) {
      ts.tv_sec = 0;
      ts.tv_nsec = 0;
    }
    seed = (u64)ts.tv_nsec ^ ((u64)ts.tv_sec << 32) ^ (u64)getpid();
  }
  rng->state = mix64(seed);
  if (rng->state == 0)
    rng->state = 0x9e3779b97f4a7c15ULL;
  return 0;
}

u64 rng_next_u64(struct Rng* rng) {
  if (!validate_ptr(rng))
    return 0;
  if (!assert_ok(rng->state != 0))
    return 0;

  u64 x = rng->state;

  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  rng->state = x;
  return x * 0x2545F4914F6CDD1DULL;
}

size_t rng_range(struct Rng* rng, size_t upper) {
  if (!validate_ptr(rng))
    return 0;
  if (!validate_ok(upper > 0))
    return 0;

  u64 threshold = (u64)(-upper) % upper;

  for (size_t i = 0; i < RNG_RETRY_LIMIT; i++) {
    u64 r = rng_next_u64(rng);

    if (r >= threshold)
      return (size_t)(r % upper);
  }
  return (size_t)(rng_next_u64(rng) % upper);
}

int rng_shuffle_groups(struct Rng* rng, size_t* values, size_t count) {
  if (!validate_ptr(rng))
    return -1;
  if (!validate_ptr(values))
    return -1;
  if (!validate_ok(count <= MAX_GROUPS))
    return -1;

  if (count < 2)
    return 0;
  for (size_t i = 1; i < MAX_GROUPS; i++) {
    if (i >= count)
      break;
    size_t j = rng_range(rng, i + 1);
    size_t tmp = values[i];

    values[i] = values[j];
    values[j] = tmp;
  }
  return 0;
}

int rng_shuffle_items(struct Rng* rng, size_t* values, size_t count) {
  if (!validate_ptr(rng))
    return -1;
  if (!validate_ptr(values))
    return -1;
  if (!validate_ok(count <= MAX_ITEMS_PER_GROUP))
    return -1;

  if (count < 2)
    return 0;
  for (size_t i = 1; i < MAX_ITEMS_PER_GROUP; i++) {
    if (i >= count)
      break;
    size_t j = rng_range(rng, i + 1);
    size_t tmp = values[i];

    values[i] = values[j];
    values[j] = tmp;
  }
  return 0;
}
