// Copyright snsinfu 2019.
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h> 

#include <signal.h>
#include <unistd.h>

#include "ftrap.h"
#include "watch_list.h"


enum {
    // Exit code for ftrap itself's error.
    ftrap_error = 112
};

static void show_usage(void)
{
    const char *msg =
        "Usage: ftrap [-h] [-f FILE] COMMAND...\n"
        "\n"
        "Send SIGHUP to COMMAND when any one of FILEs is changed.\n"
        "\n"
        "Options\n"
        "  -h       Show this help message and exit.\n"
        "  -f FILE  Add file to watch.\n";
    fputs(msg, stdout);
}

int main(int argc, char **argv)
{
    // Store paths given in -f options into this array of watch_list nodes. We
    // will then build a list from these nodes. We don't free this array.
    struct watch_list *targets = NULL;
    size_t n_targets = 0;

    for (int ch; (ch = getopt(argc, argv, "hf:")) != -1; ) {
        switch (ch) {
        case 'h':
            show_usage();
            return 0;

        case 'f': {
            void *new_targets = realloc(targets, (n_targets + 1) * sizeof *targets);
            if (new_targets == NULL) {
                fprintf(stderr, "ftrap: Failed to allocate memory - %s\n", strerror(errno));
                return ftrap_error;
            }
            targets = new_targets;
            targets[n_targets++] = (struct watch_list) { .path = optarg };
            break;
        }

        default:
            return ftrap_error;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
        fprintf(stderr, "ftrap: Command is not specified. See ftrap -h for usage.\n");
        return ftrap_error;
    }

    struct watch_list sentinel, *queue = watch_list_init(&sentinel);
    for (size_t i = 0; i < n_targets; i++) {
        watch_list_insert(queue, &targets[i]);
    }

    int status = 0;
    if (ftrap_start(queue, argv, &status) == -1) {
        return ftrap_error;
    }

    // Exit with the exact same status as that of the command. exit(status) does
    // not work!
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        if (kill(getpid(), WTERMSIG(status)) == -1) {
            return ftrap_error;
        }
    }

    assert(0);
}
