// Copyright snsinfu 2019.
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <limits.h>
#include <poll.h>
#include <unistd.h>

#include "ftrap.h"
#include "watch_list.h"


enum {
    // Polling interval, in milliseconds, for detecting newly created files.
    watch_interval = 3000
};

enum {
    // inotify events to watch. The model use case of ftrap is config file
    // reloading. The following events are relevant:
    //
    // IN_CLOSE_WRITE
    //   - Watched file is closed in write mode.
    //
    // IN_DELETE_SELF
    //   - Watched file is deleted.
    //   - Watched file is clobbered by moving a file to the same path.
    //
    // IN_MOVE_SELF
    //   - Watched file is moved to somewhere.
    //
    // IN_ATTRIB
    //   - Watched file is deleted while hardlinks remain (no IN_DELETE_SELF).
    //
    // IN_CREATE
    //   - A file is created in the watched directory.
    //
    in_watch_mask =
        IN_CLOSE_WRITE |
        IN_DELETE_SELF |
        IN_MOVE_SELF |
        IN_ATTRIB |
        IN_CREATE
};

struct ftrap {
    int                inofd;
    int                sigfd;
    pid_t              pid;
    struct watch_list *queue;
    struct watch_list *active;
    int                interval;
    int                signal;
};

static int  ftrap_init_inotify(struct ftrap *ftrap);
static int  ftrap_init_signal(struct ftrap *ftrap);
static int  ftrap_spawn_command(struct ftrap *ftrap, char **argv);
static int  ftrap_mainloop(struct ftrap *ftrap);
static int  ftrap_handle_inotify(struct ftrap *ftrap, struct inotify_event *ev);
static int  ftrap_send_signal(struct ftrap *ftrap);
static int  ftrap_watch_queue(struct ftrap *ftrap);
static int  ftrap_wait(struct ftrap *ftrap, int *status);
static void ftrap_close(struct ftrap *ftrap);
static void dummy_handler(int sig);


int ftrap_start(struct watch_list *queue, int sig, char **argv, int *status)
{
    struct ftrap ftrap = {
        .inofd    = -1,
        .sigfd    = -1,
        .pid      = -1,
        .interval = watch_interval,
        .signal   = sig
    };

    struct watch_list sentinel;
    ftrap.active = watch_list_init(&sentinel);
    ftrap.queue = queue;

    if (ftrap_init_inotify(&ftrap) == -1) {
        ftrap_close(&ftrap);
        return -1;
    }

    if (ftrap_init_signal(&ftrap) == -1) {
        ftrap_close(&ftrap);
        return -1;
    }

    if (ftrap_spawn_command(&ftrap, argv) == -1) {
        ftrap_close(&ftrap);
        return -1;
    }

    if (ftrap_mainloop(&ftrap) == -1) {
        ftrap_close(&ftrap);
        fprintf(stderr, "ftrap: Waiting for the command to exit\n");
        ftrap_wait(&ftrap, status);
        return -1;
    }

    ftrap_close(&ftrap);
    return ftrap_wait(&ftrap, status);
}

// Function: ftrap_init_inotify
//
// Starts watching queued paths (if actually exist). The `inofd` member is set
// to the file descriptor of the inotify instance used to watch paths.
//
// Returns:
//   0 on success, or -1 on failure.
//
int ftrap_init_inotify(struct ftrap *ftrap)
{
    int inofd = inotify_init1(IN_CLOEXEC);
    if (inofd == -1) {
        fprintf(stderr, "ftrap: Cannot start inotify - %s\n", strerror(errno));
        return -1;
    }
    ftrap->inofd = inofd;

    return ftrap_watch_queue(ftrap);
}

// Function: ftrap_init_signal
//
// Starts watching SIGCHLD signal. The `sigfd` member is set to the signal file
// descriptor for watching the signal.
//
// Returns:
//   0 on success, or -1 on failure.
//
int ftrap_init_signal(struct ftrap *ftrap)
{
    // Signal handler needs to be installed to catch signal via signalfd. Also,
    // SA_NOCLDSTOP should be set to prevent SIGCHLD from being fired when child
    // process is stopped or resumed.
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = dummy_handler;
    sa.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        fprintf(stderr, "ftrap: Failed to watch SIGCHLD - %s\n", strerror(errno));
        return -1;
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    int sigfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sigfd == -1) {
        fprintf(stderr, "ftrap: Failed to create signalfd - %s\n", strerror(errno));
        return -1;
    }

    ftrap->sigfd = sigfd;

    return 0;
}

// Function: ftrap_spawn_command
//
// Spawns a new process that executes the command given in `argv`. The `pid`
// member is set to the process ID of the child process.
//
// If this function succeeds, the caller must call `ftrap_wait` later to wait
// for the child process to exit. Otherwise, the child will become a zombie.
//
// Returns:
//   0 on success, or -1 on failure.
//
int ftrap_spawn_command(struct ftrap *ftrap, char **argv)
{
    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "ftrap: Failed to fork - %s\n", strerror(errno));
        return -1;
    }

    // Child process.
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "ftrap: Failed to execute the command - %s\n", strerror(errno));
        return -1;
    }

    ftrap->pid = pid;

    return 0;
}

// Function: ftrap_mainloop
//
// Processes inotify events and notifies file changes to the child process. The
// function returns when the child process exits.
//
// Returns:
//   0 if the child process exits, or -1 on error.
//
int ftrap_mainloop(struct ftrap *ftrap)
{
    enum {
        poll_inotify,
        poll_signalfd,
        poll_count
    };
    struct pollfd polls[poll_count] = {
        [poll_inotify ] = { .fd = ftrap->inofd, .events = POLLIN },
        [poll_signalfd] = { .fd = ftrap->sigfd, .events = POLLIN }
    };

    for (;;) {
        while (poll(polls, poll_count, ftrap->interval) == -1) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "ftrap: Failed to poll - %s\n", strerror(errno));
            return -1;
        }

        // Child process is terminated.
        if (polls[poll_signalfd].revents & POLLIN) {
            return 0;
        }

        // Received one or more inotify events.
        if (polls[poll_inotify].revents & POLLIN) {
            char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
            ssize_t n_read;

            for (;;) {
                n_read = read(ftrap->inofd, buf, sizeof buf);
                if (n_read == -1) {
                    if (errno == EINTR) {
                        continue;
                    }
                    fprintf(stderr, "ftrap: Failed to read inotify event - %s\n", strerror(errno));
                    return -1;
                }
                break;
            }

            for (ssize_t offset = 0; offset < n_read; ) {
                struct inotify_event *ev = (void *) (buf + offset);
                if (ftrap_handle_inotify(ftrap, ev) == -1) {
                    // Proceed anyway.
                }
                offset += (ssize_t) (sizeof *ev + ev->len);
            }
        }

        // Try to rewatch unwatched paths.
        int n_watch = ftrap_watch_queue(ftrap);
        if (n_watch == -1) {
            // Retry in the next time. Proceed.
        }
        if (n_watch > 0) {
            // Paths become watchable. This means the paths have been newly
            // created after previous poll. So send a signal.
            if (ftrap_send_signal(ftrap) == -1) {
                // Proceed anyway.
            }
        }
    }

    assert(0);
}

// Function: ftrap_handle_inotify
//
// Handles an inotify event (passed by `ftrap_mainloop`). This function sends
// a signal to the child process when watched paths are changed.
//
// Returns:
//   0 on success, or -1 on failure.
//
int ftrap_handle_inotify(struct ftrap *ftrap, struct inotify_event *ev)
{
    int should_rewatch = 0;

    if (ev->mask & (IN_CLOSE_WRITE | IN_CREATE)) {
        if (ftrap_send_signal(ftrap) == -1) {
            return -1;
        }
    }

    if (ev->mask & (IN_MOVE_SELF | IN_ATTRIB)) {
        if (inotify_rm_watch(ftrap->inofd, ev->wd) == -1) {
            // The wd can already have been unwatched depending on the order of
            // the events read and processed. Ignore this error.
        } else {
            should_rewatch = 1;
        }
    }

    if (ev->mask & IN_IGNORED) {
        should_rewatch = 1;
    }

    if (should_rewatch) {
        // target can not be found in the active list (target has already been
        // moved to the queue) depending on the order of the events read and
        // processed. So NULL is OK and ignored here.
        struct watch_list *active = ftrap->active;
        struct watch_list *queue = ftrap->queue;
        struct watch_list *target = watch_list_find(active, ev->wd);
        if (target != NULL) {
            watch_list_insert(queue->next, watch_list_drop(target));
        }
    }

    return 0;
}

// Function: ftrap_send_signal
//
// Sends notification signal to the child process.
//
// Returns:
//   0 on success, or -1 on failure.
//
int ftrap_send_signal(struct ftrap *ftrap)
{
    if (kill(ftrap->pid, ftrap->signal) == -1) {
        fprintf(stderr, "ftrap: Failed to send signal - %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

// Function: ftrap_watch_queue
//
// Adds queued paths to the inotify instance if the paths exist.
//
// Returns:
//   The number of newly watched paths, or -1 on failure.
//
int ftrap_watch_queue(struct ftrap *ftrap)
{
    struct watch_list *queue = ftrap->queue;
    struct watch_list *active = ftrap->active;
    int n_watch = 0;

    for (struct watch_list *ent = queue->next; ent != queue; ent = ent->next) {
        int wd = inotify_add_watch(ftrap->inofd, ent->path, in_watch_mask);
        if (wd == -1) {
            if (errno == ENOENT) {
                // Path currently does not exist. That's OK, keep this entry in
                // the queue.
                continue;
            }
            fprintf(stderr, "ftrap: Cannot watch file '%s' - %s\n", ent->path, strerror(errno));
            return -1;
        }
        ent->wd = wd;

        struct watch_list *prev = ent->prev;
        watch_list_insert(active, watch_list_drop(ent));
        n_watch++;

        // prev->next is the next node to see.
        ent = prev;
    }

    return n_watch;
}

// Function: ftrap_wait
//
// Waits for the child process to exit. The exit code is assigned to `*status`.
//
// Returns:
//   0 on success, or -1 on failure.
//
int ftrap_wait(struct ftrap *ftrap, int *status)
{
    if (ftrap->pid == -1) {
        return 0;
    }
    while (waitpid(ftrap->pid, status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }
        fprintf(stderr, "ftrap: Error waiting for the command - %s\n", strerror(errno));
        return -1;
    }
    ftrap->pid = -1;
    return 0;
}

// Function: ftrap_close
//
// Closes file descriptors.
//
void ftrap_close(struct ftrap *ftrap)
{
    close(ftrap->inofd);
    close(ftrap->sigfd);
    ftrap->inofd = -1;
    ftrap->sigfd = -1;
}

// Function: dummy_handler
//
// Signal handler that does nothing.
//
void dummy_handler(int sig)
{
    (void) sig;
}
