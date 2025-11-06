#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

struct child_ctx {
    int argc;
    char **argv;
};

static double elapsed_sec(struct timespec a, struct timespec b) {
    long sec = b.tv_sec - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }
    return (double)sec + (double)nsec / 1e9;
}

static int child_fn(void *arg) {
    struct child_ctx *ctx = (struct child_ctx *)arg;
    execvp(ctx->argv[0], ctx->argv);
    perror("execvp");
    _exit(127);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
        return 2;
    }

    struct child_ctx ctx;
    ctx.argc = argc - 1;
    ctx.argv = &argv[1];

    void *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        return 1;
    }
    void *stack_top = (char *)stack + STACK_SIZE;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int flags = SIGCHLD;
    pid_t pid = clone(child_fn, stack_top, flags, &ctx);
    if (pid < 0) {
        perror("clone");
        free(stack);
        return 1;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        free(stack);
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = elapsed_sec(t0, t1);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        printf("clone: pid=%d exit=%d time=%.6f s\n", pid, code, dt);
        free(stack);
        return code;
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("clone: pid=%d signaled=%d time=%.6f s\n", pid, sig, dt);
        free(stack);
        return 128 + sig;
    } else {
        printf("clone: pid=%d status=0x%x time=%.6f s\n", pid, status, dt);
        free(stack);
        return 1;
    }
}
