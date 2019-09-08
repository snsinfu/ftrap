// Copyright snsinfu 2019.
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

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

struct watch_list {
    const char        *path;
    int                wd;
    struct watch_list *prev;
    struct watch_list *next;
};

struct watch_list *watch_list_init(struct watch_list *node);
struct watch_list *watch_list_insert(struct watch_list *ent, struct watch_list *node);
struct watch_list *watch_list_drop(struct watch_list *ent);
struct watch_list *watch_list_find(struct watch_list *list, int wd);

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

void show_usage(void);
int  ftrap_start(struct watch_list *queue, char **argv, int *status);
int  ftrap_watch(int inofd, struct watch_list *queue, struct watch_list *active);

static void dummy_handler(int sig);


int main(int argc, char **argv)
{
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

    // Exit with the exact same status as that of the command.
    if (WIFEXITED(status)) {
        exit(WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        kill(getpid(), WTERMSIG(status));
    }

    return ftrap_error;
}

// Function: ftrap_start
//
// Parameters:
//   queue  - List of paths to watch.
//   argv   - Command argv to execute.
//   status - Pointer to variable to return exit status to.
//
// Returns:
//   0 on success, or -1 on failure.
//
int ftrap_start(struct watch_list *queue, char **argv, int *status)
{
    // We use inotify to watch for file changes.
    int inofd;
    struct watch_list sentinel, *active = watch_list_init(&sentinel);

    inofd = inotify_init1(IN_CLOEXEC);
    if (inofd == -1) {
        fprintf(stderr, "ftrap: Cannot start inotify - %s\n", strerror(errno));
        return -1;
    }

    if (ftrap_watch(inofd, queue, active) == -1) {
        return -1;
    }

    // Watch SIGCHLD to detect command exit. We poll on a signalfd instead of
    // doing something in the signal handler so that we can wait for both file
    // changes and the child process in the same loop.
    int sigfd;

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
    sigfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sigfd == -1) {
        fprintf(stderr, "ftrap: Failed to create signalfd - %s\n", strerror(errno));
        return -1;
    }

    // Start child process.
    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "ftrap: Failed to fork - %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "ftrap: Failed to execute the command - %s\n", strerror(errno));
        return -1;
    }

    // Beware! From here we cannot just return -1 on error. Always wait for the
    // child. Otherwise the child will become a zombie.
    enum {
        poll_inotify,
        poll_signalfd,
        poll_count
    };
    struct pollfd polls[poll_count] = {
        [poll_inotify ] = { .fd = inofd, .events = POLLIN },
        [poll_signalfd] = { .fd = sigfd, .events = POLLIN }
    };

    int watch_interval_ms = 3000;

    for (;;) {
        if (poll(polls, poll_count, watch_interval_ms) == -1) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "ftrap: Failed to poll - %s\n", strerror(errno));
            fprintf(stderr, "ftrap: Waiting for the command to finish\n");
            while (waitpid(pid, status, 0) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "ftrap: Error waiting for the command - %s\n", strerror(errno));
                break;
            }
            return -1;
        }

        // Child process is terminated.
        if (polls[poll_signalfd].revents & POLLIN) {
            if (waitpid(pid, status, 0) == -1) {
                fprintf(stderr, "ftrap: Error waiting for the command - %s\n", strerror(errno));
                return -1;
            }
            return 0;
        }

        // inotify event.
        if (polls[poll_inotify].revents & POLLIN) {
            char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
            ssize_t n_read;

            for (;;) {
                n_read = read(inofd, buf, sizeof buf);
                if (n_read == -1) {
                    if (errno == EINTR) {
                        continue;
                    }
                    fprintf(stderr, "ftrap: Failed to read inotify event - %s\n", strerror(errno));
                }
                break;
            }

            for (ssize_t offset = 0; offset < n_read; ) {
                struct inotify_event *ev = (void *) (buf + offset);
                int should_rewatch = 0;

                if (ev->mask & (IN_CLOSE_WRITE | IN_CREATE)) {
                    if (kill(pid, SIGHUP) == -1) {
                        fprintf(stderr, "ftrap: Failed to send SIGHUP - %s\n", strerror(errno));
                        // Proceed anyway.
                    }
                }

                if (ev->mask & (IN_MOVE_SELF | IN_ATTRIB)) {
                    if (inotify_rm_watch(inofd, ev->wd) == -1) {
                        // The wd can already be unwatched depending on the
                        // order of the events read and processed.
                    } else {
                        should_rewatch = 1;
                    }
                }

                if (ev->mask & IN_IGNORED) {
                    should_rewatch = 1;
                }

                if (should_rewatch) {
                    // target can not be found in the active list (target has
                    // already been moved to the queue) depending on the order
                    // of the events read and processed.
                    struct watch_list *target = watch_list_find(active, ev->wd);
                    if (target != NULL) {
                        watch_list_insert(queue->next, watch_list_drop(target));
                    }
                }

                offset += (ssize_t) (sizeof *ev + ev->len);
            }
        }

        // Try to rewatch unwatched paths.
        int n_watch = ftrap_watch(inofd, queue, active);
        if (n_watch == -1) {
            // Retry in the next time.
        }
        if (n_watch > 0) {
            // Path became watchable, i.e., the file has been created.
            if (kill(pid, SIGHUP) == -1) {
                fprintf(stderr, "ftrap: Failed to send SIGHUP - %s\n", strerror(errno));
            }
        }
    }

    assert(0);
}

// Function: ftrap_watch
//
// Start watching paths in the queue.
//
// Parameters:
//   inofd  - inotify file descriptor.
//   queue  - List of paths to watch.
//   active - List of watched paths.
//
// Returns:
//   The number of paths newly watched, or -1 on failure.
//
int ftrap_watch(int inofd, struct watch_list *queue, struct watch_list *active)
{
    int n_watch = 0;

    for (struct watch_list *ent = queue->next; ent != queue; ent = ent->next) {
        int wd = inotify_add_watch(inofd, ent->path, in_watch_mask);
        if (wd == -1) {
            if (errno == ENOENT) {
                // OK. Keep it in the queue.
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

// Function: watch_list_init
//
// Creates an empty cyclic watch list using given node as the sentinel.
//
// Parameters:
//   node - Pointer to a watch_list node.
//
// Returns:
//   Sentinel of the created empty list, i.e., the `node` argument.
//
struct watch_list *watch_list_init(struct watch_list *node)
{
    node->prev = node;
    node->next = node;
    return node;
}

// Function: watch_list_insert
//
// Inserts a node to the specified position of a watch list.
//
// Parameters:
//   ent  - Node in a list where the new node is inserted before.
//   node - New node to insert.
//
// Returns:
//   The new node inserted, i.e., the `node` argument.
//
struct watch_list *watch_list_insert(struct watch_list *ent, struct watch_list *node)
{
    // Precondition:  prev -> ent
    // Postcondition: prev -> node -> ent
    struct watch_list *prev = ent->prev;
    node->prev = prev;
    node->next = ent;
    prev->next = node;
    ent->prev = node;
    return node;
}

// Function: watch_list_drop
//
// Drops a node from a watch list.
//
// Parameters:
//   ent - Node to drop.
//
// Returns:
//   The node dropped, i.e., the `ent` argument.
//
struct watch_list *watch_list_drop(struct watch_list *ent)
{
    struct watch_list *prev = ent->prev;
    struct watch_list *next = ent->next;
    prev->next = next;
    next->prev = prev;
    return ent;
}

// Function: watch_list_find
//
// Finds a node by wd value.
//
// Parameters:
//   list - Sentinel node of the list to find a node in.
//   wd   - wd value to identify a node.
//
// Returns:
//   First node having the given wd, or NULL if no such node exists.
//
struct watch_list *watch_list_find(struct watch_list *list, int wd)
{
    for (struct watch_list *ent = list->next; ent != list; ent = ent->next) {
        if (ent->wd == wd) {
            return ent;
        }
    }
    return NULL;
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


static void dummy_handler(int sig)
{
    (void) sig;
}
