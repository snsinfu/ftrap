// Copyright snsinfu 2019.
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef FTRAP_FTRAP_H
#define FTRAP_FTRAP_H

#include "watch_list.h"


// Function: ftrap_start
//
// Parameters:
//   queue  - List of paths to watch.
//   sig    - Signal to send to the child process.
//   argv   - Command argv to execute.
//   status - Pointer to variable to return exit status to.
//
// Returns:
//   0 on success, or -1 on failure.
//
int ftrap_start(struct watch_list *queue, int sig, char **argv, int *status);

#endif
