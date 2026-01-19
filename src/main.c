// SPDX-License-Identifier: MIT
#include "app.h"

#include <stdio.h>
#include <string.h>

static int print_usage(const char* prog) {
  if (!prog)
    return -1;

  int rc = fprintf(stdout, "Usage: %s <session-file>\n", prog);

  if (rc < 0)
    return -1;
  rc = fprintf(stdout, "       %s -h\n\n", prog);
  if (rc < 0)
    return -1;
  rc = fprintf(stdout, "Keys: Enter/Space/alnum = next, Ctrl+C = quit\n");
  if (rc < 0)
    return -1;
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 2 &&
      (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    int rc = print_usage(argv[0]);

    return (rc == 0) ? 0 : 1;
  }
  if (argc != 2) {
    int rc = print_usage(argv[0]);

    /* Usage error.
     * If printing usage fails, signal a distinct failure.
     */
    return (rc == 0) ? 1 : 2;
  }
  return (app_run_file(argv[1]) == 0) ? 0 : 1;
}
