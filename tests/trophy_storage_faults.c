/* Host link-time fault injection. Production storage source is unmodified. */
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
static int bytes_before_failure = -1, sync_failure, close_failure;
void trophy_test_fault(int bytes, int sync, int close) {
    bytes_before_failure=bytes; sync_failure=sync; close_failure=close;
}
extern size_t __real_fwrite(const void *,size_t,size_t,FILE *);
extern int __real_fsync(int);
extern int __real_fclose(FILE *);
size_t __wrap_fwrite(const void *ptr,size_t size,size_t count,FILE *stream) {
    if (bytes_before_failure < 0) return __real_fwrite(ptr,size,count,stream);
    size_t bytes=(size_t)bytes_before_failure;
    bytes_before_failure=-1;
    if (bytes > size*count) bytes=size*count;
    return size ? __real_fwrite(ptr,1,bytes,stream)/size : 0;
}
int __wrap_fsync(int fd) {
    if (sync_failure > 0 && --sync_failure == 0) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
int __wrap_fclose(FILE *file) {
    /* Fail only writable stream close, after its successful real close. Reads
     * during slot selection must not consume this injection. */
    int error = 0;
    long position = ftell(file);
    if (close_failure && position == 40 && !feof(file)) { close_failure=0; error=1; }
    int result=__real_fclose(file);
    if (error) { errno=EIO; return -1; }
    return result;
}
