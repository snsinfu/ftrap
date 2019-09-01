// Copyright snsinfu 2019. MIT License.

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
static void dummy_handler(int sig);


int main(int argc, char **argv)
{
    int ino = inotify_init1(IN_CLOEXEC);
    if (ino == -1) {
        fprintf(stderr, "error: Cannot create inotify instance - %s\n", strerror(errno));
        return ftrap_error;
    }

    for (int ch; (ch = getopt(argc, argv, "hf:")) != -1; ) {
        switch (ch) {
          case 'h':
            show_usage();
            return 0;

          case 'f':
            if (inotify_add_watch(ino, optarg, IN_MODIFY) == -1) {
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
        fprintf(stderr, "error: Command is not specified. See ftrap -h for help.\n");
        return ftrap_error;
    }

    // Need to set a handler to detect SIGCHLD with signalfd.
    if (signal(SIGCHLD, dummy_handler) == SIG_ERR) {
        fprintf(stderr, "error: ");
        return ftrap_error;
    }

    // We will wait for child process using poll(2) via this signalfd.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    int sig = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sig == -1) {
        fprintf(stderr, "error: cannot monitor SIGCHLD - %s\nn", strerror(errno));
        return ftrap_error;
    }

    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "error: cannot fork - %s\n", strerror(errno));
        return ftrap_error;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "error: cannot execute the command - %s\n", strerror(errno));
        exit(ftrap_error);
    }

    enum {
        poll_inotify,
        poll_signalfd,
        poll_count
    };
    struct pollfd polls[poll_count] = {
        [poll_inotify] = { .fd = ino, .events = POLLIN },
        [poll_signalfd] = { .fd = sig, .events = POLLIN }
    };

    for (;;) {
        if (poll(polls, poll_count, -1) == -1) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "error: poll failed - %s\n", strerror(errno));
            return ftrap_error;
        }

        if (polls[poll_signalfd].revents & POLLIN) {
            int status = 0;
            if (waitpid(pid, &status, 0) == -1) {
                fprintf(stderr, "error: waitpid failed - %s\n", strerror(errno));
                return ftrap_error;
            }

            // Exit with the same code as the child.
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }

            // Die with the same signal as the child.
            if (WIFSIGNALED(status)) {
                kill(getpid(), WTERMSIG(status));
            }

            return ftrap_error;
        }

        if (polls[poll_inotify].revents & POLLIN) {
            char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

            if (read(ino, buf, sizeof buf) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "error: Cannot read inotify event - %s\n", strerror(errno));
                return ftrap_error;
            }

            struct inotify_event *ev = (void *) buf;

            if (ev->mask & IN_MODIFY) {
                if (kill(pid, SIGHUP) == -1) {
                    fprintf(stderr, "error: Cannot signal the child - %s\n", strerror(errno));
                    return ftrap_error;
                }
            }
        }
    }

    assert(0);
}

// show_usage prints usage information to stderr.
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

void dummy_handler(int sig)
{
    (void) sig;
}
