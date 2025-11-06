#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t crc_table[256];

static void crc32_init(void) {
    uint32_t poly = 0xEDB88320u;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            if (c & 1) c = poly ^ (c >> 1);
            else c >>= 1;
        }
        crc_table[i] = c;
    }
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *buf, size_t len) {
    crc = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = buf[i];
        crc = crc_table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

static double elapsed_sec(struct timespec a, struct timespec b) {
    long sec = b.tv_sec - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }
    return (double)sec + (double)nsec / 1e9;
}

static uint32_t xr = 123456789u;

static uint32_t xrand32(void) {
    xr ^= xr << 13;
    xr ^= xr >> 17;
    xr ^= xr << 5;
    return xr;
}

int main(int argc, char **argv) {
    int fragments = 0;
    int fragment_size = 0;
    int repeats = 1;
    int quiet = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--fragments") && i + 1 < argc) {
            fragments = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--fragment-size") && i + 1 < argc) {
            fragment_size = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--repeats") && i + 1 < argc) {
            repeats = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--quiet")) {
            quiet = 1;
        } else {
            fprintf(stderr, "Usage: %s --fragments N --fragment-size M [--repeats R] [--quiet]\n", argv[0]);
            return 2;
        }
    }

    if (fragments <= 0 || fragment_size <= 0 || repeats <= 0) {
        fprintf(stderr, "Provide positive --fragments, --fragment-size and --repeats\n");
        return 2;
    }

    crc32_init();

    char **pool = malloc((size_t)fragments * sizeof(char *));
    if (!pool) {
        perror("malloc");
        return 1;
    }
    for (int i = 0; i < fragments; ++i) {
        pool[i] = malloc((size_t)fragment_size);
        if (!pool[i]) {
            perror("malloc");
            for (int j = 0; j < i; ++j) free(pool[j]);
            free(pool);
            return 1;
        }
        for (int j = 0; j < fragment_size; ++j) {
            uint32_t r = xrand32();
            pool[i][j] = (char)('a' + (r % 26u));
        }
    }

    size_t text_size = (size_t)fragments * (size_t)fragment_size;
    uint32_t last_crc = 0;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int r = 0; r < repeats; ++r) {
        uint32_t crc = 0;
        for (int i = 0; i < fragments; ++i) {
            int idx = (int)(xrand32() % (uint32_t)fragments);
            crc = crc32_update(crc, (unsigned char *)pool[idx], (size_t)fragment_size);
        }
        last_crc = crc;
        if (!quiet) {
            printf("Run %d: CRC32=0x%08" PRIx32 "\n", r + 1, crc);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double dt = elapsed_sec(t0, t1);

    printf("Total time: %.6f s over %d repeats, text size=%zu bytes, last_crc=0x%08" PRIx32 "\n",
           dt, repeats, text_size, last_crc);

    for (int i = 0; i < fragments; ++i) free(pool[i]);
    free(pool);

    return 0;
}
