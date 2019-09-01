// MIT License
//
// Copyright (c) 2019 snsinfu
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

#include <limits.h>
#include <poll.h>
#include <unistd.h>


enum { ftrap_error = 112 };

static void show_usage(void);
static int  start_ftrap(pid_t pid, int inofd, int sigfd, int *status);
static void dummy_handler(int sig);


int main(int argc, char **argv)
{
    pid_t pid;
    int inofd;
    int sigfd;

    inofd = inotify_init1(IN_CLOEXEC);
    if (inofd == -1) {
        fprintf(stderr, "error: Cannot create inotify instance - %s\n", strerror(errno));
        return ftrap_error;
    }

    for (int ch; (ch = getopt(argc, argv, "hf:")) != -1; ) {
        switch (ch) {
        case 'h':
            show_usage();
            return 0;

        case 'f':
            if (inotify_add_watch(inofd, optarg, IN_MODIFY) == -1) {
                fprintf(stderr, "error: Cannot watch file %s - %s\n", optarg, strerror(errno));
                return ftrap_error;
            }
            break;

        default:
            return ftrap_error;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
        fprintf(stderr, "error: Command is not specified. See ftrap -h for usage.\n");
        return ftrap_error;
    }

    // Looks like a handler needs to be set to detect SIGCHLD with signalfd.
    if (signal(SIGCHLD, dummy_handler) == SIG_ERR) {
        fprintf(stderr, "error: Failed to watch SIGCHLD - %s\n", strerror(errno));
        return ftrap_error;
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sigfd == -1) {
        fprintf(stderr, "error: Cannot create signalfd - %s\nn", strerror(errno));
        return ftrap_error;
    }

    pid = fork();
    if (pid == -1) {
        fprintf(stderr, "error: Cannot fork - %s\n", strerror(errno));
        return ftrap_error;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "error: Cannot execute the command - %s\n", strerror(errno));
        return ftrap_error;
    }

    int status;
    if (start_ftrap(pid, inofd, sigfd, &status) == -1) {
        return ftrap_error;
    }

    // Exit with the same status as that of the child.
    if (WIFEXITED(status)) {
        exit(WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        kill(getpid(), WTERMSIG(status));
    }

    return ftrap_error;
}

// Function: show_usage
//
// Outputs program usage to stdout.
//
void show_usage(void)
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

// Function: start_ftrap
//
// Sends SIGHUP to the child process on detecting any inotify event.
//
// Parameters:
//    pid    - pid of the child process
//    inofd  - inotify file descriptor to watch
//    sigfd  - signalfd file descriptor for SIGCHLD
//    status - child process' exit status is returned to *status
//
// Returns:
//    -1 on failure. It does not return on success (instead the process exits).
//
int start_ftrap(pid_t pid, int inofd, int sigfd, int *status)
{
    enum {
        poll_inotify,
        poll_signalfd,
        poll_count
    };
    struct pollfd polls[poll_count] = {
        [poll_inotify ] = { .fd = inofd, .events = POLLIN },
        [poll_signalfd] = { .fd = sigfd, .events = POLLIN }
    };

    for (;;) {
        if (poll(polls, poll_count, -1) == -1) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "error: Cannot poll - %s\n", strerror(errno));
            return -1;
        }

        // Child process is terminated.
        if (polls[poll_signalfd].revents & POLLIN) {
            if (waitpid(pid, status, 0) == -1) {
                fprintf(stderr, "error: Cannot waitpid - %s\n", strerror(errno));
                return -1;
            }
            return 0;
        }

        // One of the target files is changed.
        if (polls[poll_inotify].revents & POLLIN) {
            char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

            while (read(inofd, buf, sizeof buf) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "error: Cannot read inotify event - %s\n", strerror(errno));
                return -1;
            }

            if (kill(pid, SIGHUP) == -1) {
                fprintf(stderr, "error: Cannot signal the child - %s\n", strerror(errno));
                return -1;
            }
        }
    }

    assert(0);
}

// Function: dummy_handler
//
// Dummy signal handler that does nothing.
//
void dummy_handler(int sig)
{
    (void) sig;
}
