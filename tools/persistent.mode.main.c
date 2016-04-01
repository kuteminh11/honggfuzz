#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define HF_FUZZ_FD 1023
#define HF_BUF_SIZE (1024 * 1024 * 16)

#define NO_SAN __attribute__((no_sanitize("address", "thread", "leak", "undefined")))

NO_SAN static inline ssize_t readFromFd(int fd, uint8_t * buf, size_t len)
{
    ssize_t readSz = 0;
    while (readSz < len) {
        ssize_t sz = read(fd, &buf[readSz], len - readSz);
        if (sz < 0 && errno == EINTR)
            continue;

        if (sz <= 0)
            break;

        readSz += sz;
    }
    return readSz;
}

NO_SAN static inline bool readFromFdAll(int fd, uint8_t * buf, size_t len)
{
    return (readFromFd(fd, buf, len) == len);
}

NO_SAN static bool writeToFd(int fd, uint8_t * buf, size_t len)
{
    ssize_t writtenSz = 0;
    while (writtenSz < len) {
        ssize_t sz = write(fd, &buf[writtenSz], len - writtenSz);
        if (sz < 0 && errno == EINTR)
            continue;

        if (sz < 0)
            return false;

        writtenSz += sz;
    }
    return (writtenSz == len);
}

NO_SAN static inline ssize_t readFileToBuf(const char *fname, uint8_t * buf, size_t len)
{
    int fd = open((char *)fname, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return -1;
    }
    ssize_t rsz = readFromFd(fd, buf, len);
    if (rsz < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return rsz;
}

int LLVMFuzzerTestOneInput(uint8_t * buf, size_t len);
__attribute__ ((weak))
int LLVMFuzzerInitialize(int *argc, char ***argv);

NO_SAN int main(int argc, char **argv)
{
    uint8_t *buf = (uint8_t *) malloc(HF_BUF_SIZE);
    if (buf == NULL) {
        perror("malloc");
        _exit(1);
    }

    if (LLVMFuzzerInitialize) {
        LLVMFuzzerInitialize(&argc, &argv);
    }

    for (;;) {
        /* Receive file-name from the parent (len == PATH_MAX) */
        char fname[PATH_MAX];
        if (readFromFdAll(HF_FUZZ_FD, (uint8_t *) fname, PATH_MAX) == false) {
            fprintf(stderr, "readFromFdAll()");
            _exit(1);
        }

        ssize_t rsz = readFileToBuf(fname, buf, HF_BUF_SIZE);
        if (rsz < 0) {
            _exit(1);
        }

        int ret = LLVMFuzzerTestOneInput(buf, rsz);
        if (ret != 0) {
            printf("LLVMFuzzerTestOneInpu returned '%d'", ret);
            _exit(1);
        }

        /*
         * Send the 'done' marker to the parent */
        uint8_t z = 'A';
        if (writeToFd(HF_FUZZ_FD, &z, sizeof(z)) == false) {
            fprintf(stderr, "readFromFdAll()");
            _exit(1);
        }
        /*
         * Inform the parent that we're done, so it can break out of its wait()
         * sleep cycle
         * */
        raise(SIGCONT);
    }
}
