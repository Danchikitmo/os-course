#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char **argv;
    int argc;
    int background;                  
    char *in_redir;                  
    char *out_redir;                 
    int out_append;                  
    int redirect_stderr_to_stdout;   
    int pipe_after;                  
} command_t;

typedef struct {
    char **data;
    int size;
    int cap;
} vec_t;

static int g_quiet = 0;

static double elapsed_sec(struct timespec a, struct timespec b) {
    long sec = b.tv_sec - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }
    return (double)sec + (double)nsec / 1e9;
}

static void vpush(vec_t *v, char *s) {
    if (v->size == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->data = realloc(v->data, v->cap * sizeof(char *));
    }
    v->data[v->size++] = s;
}

static char *substr(const char *s, size_t len) {
    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

static vec_t tokenize(const char *line) {
    vec_t v = (vec_t){0};
    const char *p = line;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;


        if (p[0] == '2' && p[1] == '>' && p[2] == '&' && p[3] == '1') {
            vpush(&v, substr(p, 4));
            p += 4;
            continue;
        }

        if (p[0] == '>' && p[1] == '>') {
            vpush(&v, substr(p, 2));
            p += 2;
            continue;
        }


        if (*p == ';' || *p == '&' || *p == '|' || *p == '<' || *p == '>') {
            vpush(&v, substr(p, 1));
            p++;
            continue;
        }

        if (*p == '\'' || *p == '"') {
            char q = *p++;
            const char *start = p;
            while (*p && *p != q) p++;
            vpush(&v, substr(start, (size_t)(p - start)));
            if (*p == q) p++;
            continue;
        }

        const char *start = p;
        while (*p && !isspace((unsigned char)*p) &&
               *p != ';' && *p != '&' && *p != '|' &&
               *p != '<' && *p != '>') {
            if (p[0] == '>' && p[1] == '>') break;
            if (p[0] == '2' && p[1] == '>' && p[2] == '&' && p[3] == '1') break;
            p++;
        }
        vpush(&v, substr(start, (size_t)(p - start)));
    }

    vpush(&v, NULL);  

    for (int i = 0; i + 1 < v.size; ++i) {
        char *t = v.data[i];
        if (t && t[0] == '$' && t[1] != '\0') {
            const char *val = getenv(t + 1);
            free(t);
            if (val) v.data[i] = strdup(val);
            else     v.data[i] = strdup("");
        }
    }

    return v;
}

static void free_tokens(vec_t *v) {
    for (int i = 0; i + 1 < v->size; ++i) {
        free(v->data[i]);
    }
    free(v->data);
    v->data = NULL;
    v->size = v->cap = 0;
}

static command_t *parse_commands(vec_t *tok, int *out_n, char **out_cmdline) {
    int n = 0;
    int cap = 8;
    command_t *arr = calloc(cap, sizeof(*arr));
    if (!arr) return NULL;

    size_t totlen = 0;
    for (int i = 0; i + 1 < tok->size; ++i) {
        totlen += strlen(tok->data[i]) + 1;
    }
    char *cmdline = malloc(totlen + 1);
    if (!cmdline) {
        free(arr);
        return NULL;
    }
    cmdline[0] = '\0';
    for (int i = 0; i + 1 < tok->size; ++i) {
        strcat(cmdline, tok->data[i]);
        if (i + 2 < tok->size) strcat(cmdline, " ");
    }
    if (out_cmdline) *out_cmdline = cmdline; else free(cmdline);

    char **argv = NULL;
    int argc = 0;
    int argc_cap = 0;

    char *cur_in = NULL;
    char *cur_out = NULL;
    int cur_out_append = 0;
    int cur_err_to_out = 0;

    for (int i = 0; i + 1 < tok->size; ++i) {
        char *t = tok->data[i];

        if (!strcmp(t, "2>&1")) {
            cur_err_to_out = 1;
            continue;
        }

        if (!strcmp(t, "<")) {
            if (i + 2 <= tok->size) {
                free(cur_in);
                cur_in = strdup(tok->data[++i]);
            }
            continue;
        }
        if (!strcmp(t, ">") || !strcmp(t, ">>")) {
            int append = (t[1] == '>');
            if (i + 2 <= tok->size) {
                free(cur_out);
                cur_out = strdup(tok->data[++i]);
                cur_out_append = append;
            }
            continue;
        }
        if (!strcmp(t, ";") || !strcmp(t, "&") || !strcmp(t, "|")) {
            if (argc > 0) {
                if (n == cap) {
                    cap *= 2;
                    arr = realloc(arr, cap * sizeof(*arr));
                }
                arr[n].argv = malloc((argc + 1) * sizeof(char *));
                for (int k = 0; k < argc; ++k) {
                    arr[n].argv[k] = strdup(argv[k]);
                }
                arr[n].argv[argc] = NULL;
                arr[n].argc = argc;
                arr[n].in_redir = cur_in;
                arr[n].out_redir = cur_out;
                arr[n].out_append = cur_out_append;
                arr[n].redirect_stderr_to_stdout = cur_err_to_out;
                arr[n].background = (!strcmp(t, "&")) ? 1 : 0;
                arr[n].pipe_after = (!strcmp(t, "|")) ? 1 : 0;
                n++;

                free(argv);
                argv = NULL;
                argc = argc_cap = 0;
                cur_in = NULL;
                cur_out = NULL;
                cur_out_append = 0;
                cur_err_to_out = 0;
            }
            continue;
        }

        if (argc == argc_cap) {
            argc_cap = argc_cap ? argc_cap * 2 : 8;
            argv = realloc(argv, argc_cap * sizeof(char *));
        }
        argv[argc++] = t;
    }

    if (argc > 0) {
        if (n == cap) {
            cap *= 2;
            arr = realloc(arr, cap * sizeof(*arr));
        }
        arr[n].argv = malloc((argc + 1) * sizeof(char *));
        for (int k = 0; k < argc; ++k) {
            arr[n].argv[k] = strdup(argv[k]);
        }
        arr[n].argv[argc] = NULL;
        arr[n].argc = argc;
        arr[n].in_redir = cur_in;
        arr[n].out_redir = cur_out;
        arr[n].out_append = cur_out_append;
        arr[n].redirect_stderr_to_stdout = cur_err_to_out;
        arr[n].background = 0;
        arr[n].pipe_after = 0;
        n++;
        free(argv);
    }

    *out_n = n;
    return arr;
}

static void free_commands(command_t *arr, int n) {
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < arr[i].argc; ++k) {
            free(arr[i].argv[k]);
        }
        free(arr[i].argv);
        free(arr[i].in_redir);
        free(arr[i].out_redir);
    }
    free(arr);
}

static int run_pipeline(command_t *cmds, int ncmd, const char *cmdline, int *out_status) {
    if (ncmd <= 0) {
        *out_status = 0;
        return 0;
    }


    if (ncmd == 1 && cmds[0].argc > 0) {
        command_t *c = &cmds[0];
        if (strcmp(c->argv[0], "cd") == 0) {
            const char *dir = c->argc >= 2 ? c->argv[1] : getenv("HOME");
            if (!dir) dir = "/";
            int rc = chdir(dir);
            if (rc != 0) perror("cd");
            *out_status = (rc == 0) ? 0 : 1;
            return 0;
        }
        if (strcmp(c->argv[0], "exit") == 0) {
            exit(0);
        }
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int (*pipes)[2] = NULL;
    if (ncmd > 1) {
        pipes = malloc(sizeof(int[2]) * (size_t)(ncmd - 1));
        if (!pipes) {
            perror("malloc pipes");
            *out_status = 1;
            return -1;
        }
        for (int i = 0; i < ncmd - 1; ++i) {
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
                *out_status = 1;
                for (int j = 0; j < i; ++j) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                free(pipes);
                return -1;
            }
        }
    }

    pid_t *pids = malloc(sizeof(pid_t) * (size_t)ncmd);
    if (!pids) {
        perror("malloc pids");
        *out_status = 1;
        if (pipes) {
            for (int i = 0; i < ncmd - 1; ++i) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
            free(pipes);
        }
        return -1;
    }

    for (int i = 0; i < ncmd; ++i) {
        command_t *c = &cmds[i];
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            *out_status = 1;
            for (int k = 0; k < i; ++k) waitpid(pids[k], NULL, 0);
            if (pipes) {
                for (int j = 0; j < ncmd - 1; ++j) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                free(pipes);
            }
            free(pids);
            return -1;
        }
        if (pid == 0) {


            if (pipes && i > 0 && !c->in_redir) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("dup2 stdin");
                    _exit(127);
                }
            }
            if (c->in_redir) {
                int fd = open(c->in_redir, O_RDONLY);
                if (fd < 0) {
                    perror("open <");
                    _exit(127);
                }
                if (dup2(fd, STDIN_FILENO) < 0) {
                    perror("dup2 <");
                    _exit(127);
                }
                close(fd);
            }

            if (pipes && i < ncmd - 1 && !c->out_redir) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("dup2 stdout");
                    _exit(127);
                }
            }
            if (c->out_redir) {
                int flags = O_WRONLY | O_CREAT;
                if (c->out_append) flags |= O_APPEND;
                else flags |= O_TRUNC;
                int fd = open(c->out_redir, flags, 0666);
                if (fd < 0) {
                    perror("open >");
                    _exit(127);
                }
                if (dup2(fd, STDOUT_FILENO) < 0) {
                    perror("dup2 >");
                    _exit(127);
                }
                close(fd);
            }

            if (c->redirect_stderr_to_stdout) {
                if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
                    perror("dup2 2>&1");
                    _exit(127);
                }
            }

            if (pipes) {
                for (int j = 0; j < ncmd - 1; ++j) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }

            if (c->argc == 0) _exit(0);

            if (strcmp(c->argv[0], "./shell") == 0) {
#ifdef __linux__
                char self_path[4096];
                ssize_t r = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
                if (r >= 0) {
                    self_path[r] = '\0';
                    execv(self_path, c->argv);  
                }
#endif
            }


            execvp(c->argv[0], c->argv);

            const char *msg = "Command not found\n";
            (void)write(STDOUT_FILENO, msg, strlen(msg));
            _exit(127);
        }
        pids[i] = pid;
    }

    if (pipes) {
        for (int j = 0; j < ncmd - 1; ++j) {
            close(pipes[j][0]);
            close(pipes[j][1]);
        }
        free(pipes);
    }

    int background = cmds[ncmd - 1].background;
    int last_status = 0;

    if (background) {
        if (!g_quiet) {
            printf("[bg pid=%d] %s\n", pids[ncmd - 1], cmdline);
            fflush(stdout);
        }
        *out_status = 0;
        free(pids);
        return 0;
    }

    for (int i = 0; i < ncmd; ++i) {
        int status;
        if (waitpid(pids[i], &status, 0) < 0) {
            perror("waitpid");
            continue;
        }
        if (i == ncmd - 1) {
            if (WIFEXITED(status)) {
                last_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                last_status = 128 + WTERMSIG(status);
            } else {
                last_status = 1;
            }
        }
    }

    free(pids);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = elapsed_sec(t0, t1);

    if (!g_quiet) {
        printf("exit=%d, time=%.6f s — %s\n", last_status, dt, cmdline);
        fflush(stdout);
    }

    *out_status = last_status;
    return 0;
}

int main(void) {
    g_quiet = !isatty(STDIN_FILENO);

    if (g_quiet) {
        setvbuf(stdin, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IONBF, 0);
    }

    char *line = NULL;
    size_t cap = 0;

    for (;;) {
        if (!g_quiet) {
            printf("vtsh> ");
            fflush(stdout);
        }

        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) {
            if (!g_quiet) printf("\n");
            break;
        }
        if (n == 0) continue;
        if (line[n - 1] == '\n') line[n - 1] = '\0';

        vec_t tok = tokenize(line);
        if (tok.size <= 1) {
            free_tokens(&tok);
            continue;
        }

        int ncmd = 0;
        char *cmdline = NULL;
        command_t *arr = parse_commands(&tok, &ncmd, &cmdline);
        free_tokens(&tok);
        if (!arr || ncmd == 0) {
            free(cmdline);
            continue;
        }

        int last_status = 0;

        for (int i = 0; i < ncmd; ) {
            int start = i;
            int end = i;
            while (end < ncmd - 1 && arr[end].pipe_after) {
                end++;
            }
            int seg_len = end - start + 1;
            run_pipeline(&arr[start], seg_len, cmdline, &last_status);
            i = end + 1;
        }

        free_commands(arr, ncmd);
        free(cmdline);
    }

    free(line);
    return 0;
}
