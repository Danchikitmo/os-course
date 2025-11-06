#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int id;
    char value[9];
} row_t;

static double elapsed_sec(struct timespec a, struct timespec b) {
    long sec = b.tv_sec - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }
    return (double)sec + (double)nsec / 1e9;
}

static int read_table(const char *path, row_t **out_rows, size_t *out_n) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen");
        return -1;
    }
    size_t n = 0;
    if (fscanf(f, "%zu", &n) != 1) {
        fprintf(stderr, "Failed to read count from %s\n", path);
        fclose(f);
        return -1;
    }
    row_t *rows = calloc(n, sizeof(row_t));
    if (!rows) {
        perror("calloc");
        fclose(f);
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        int id = 0;
        char buf[32];
        if (fscanf(f, "%d %8s", &id, buf) != 2) {
            fprintf(stderr, "Failed to read row %zu from %s\n", i, path);
            free(rows);
            fclose(f);
            return -1;
        }
        rows[i].id = id;
        memset(rows[i].value, 0, sizeof(rows[i].value));
        strncpy(rows[i].value, buf, 8);
    }
    fclose(f);
    *out_rows = rows;
    *out_n = n;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <left> <right> <out> [--repeats N] [--quiet]\n", argv[0]);
        return 2;
    }

    const char *left_path = argv[1];
    const char *right_path = argv[2];
    const char *out_path = argv[3];
    int repeats = 1;
    int quiet = 0;

    for (int i = 4; i < argc; ++i) {
        if (!strcmp(argv[i], "--repeats") && i + 1 < argc) {
            repeats = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--quiet")) {
            quiet = 1;
        } else {
            fprintf(stderr, "Usage: %s <left> <right> <out> [--repeats N] [--quiet]\n", argv[0]);
            return 2;
        }
    }

    if (repeats <= 0) {
        fprintf(stderr, "Repeats must be positive\n");
        return 2;
    }

    row_t *left = NULL;
    row_t *right = NULL;
    size_t n_left = 0;
    size_t n_right = 0;

    if (read_table(left_path, &left, &n_left) != 0) return 1;
    if (read_table(right_path, &right, &n_right) != 0) {
        free(left);
        return 1;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    size_t matches = 0;
    for (size_t i = 0; i < n_left; ++i) {
        for (size_t j = 0; j < n_right; ++j) {
            if (left[i].id == right[j].id) {
                matches++;
            }
        }
    }

    FILE *out = fopen(out_path, "w");
    if (!out) {
        perror("fopen");
        free(left);
        free(right);
        return 1;
    }

    fprintf(out, "%zu\n", matches);
    for (size_t i = 0; i < n_left; ++i) {
        for (size_t j = 0; j < n_right; ++j) {
            if (left[i].id == right[j].id) {
                fprintf(out, "%d %s %s\n", left[i].id, left[i].value, right[j].value);
            }
        }
    }
    fclose(out);

    for (int r = 1; r < repeats; ++r) {
        size_t dummy = 0;
        for (size_t i = 0; i < n_left; ++i) {
            for (size_t j = 0; j < n_right; ++j) {
                if (left[i].id == right[j].id) {
                    dummy++;
                }
            }
        }
        (void)dummy;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = elapsed_sec(t0, t1);

    if (!quiet) {
        printf("Nested Loop Join completed in %.6f s (repeats=%d, left=%zu, right=%zu)\n",
               dt, repeats, n_left, n_right);
    } else {
        printf("%.6f\n", dt);
    }

    free(left);
    free(right);
    return 0;
}
