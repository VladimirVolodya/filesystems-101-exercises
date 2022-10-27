#include <solution.h>
#include <liburing.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

typedef struct {
    char* buf;
    __u64 offset;
    unsigned nbytes;
    char read;
} io_data;


void schedule_read(struct io_uring_sqe* p_sqe, int fd, io_data* p_data) {
    op_info->read = 1;
    io_uring_prep_read(p_sqe, fd, p_data->buf, p_data->nbytes, p_data->offset);
    io_uring_sqe_set_data(p_sqe, p_data);
}

void schedule_write(struct io_uring_sqe* p_sqe, int fd, io_data* p_data) {
    op_info->read = 0;
    io_uring_prep_write(p_sqe, fd, p_data->buf, p_data->nbytes, p_data->offset);
    io_uring_sqe_set_data(p_sqe, p_data);
}

void reschedule(struct io_uring_sqe* p_sqe, int in, int out, io_data* p_data) {
    if (p_data->read) {
        io_uring_prep_read(p_sqe, in, p_data->buf, p_data->nbytes, p_data->offset);
    } else {
        io_uring_prep_write(p_sqe, out, p_data->buf, p_data->nbytes, p_data->offset);
    }
    io_uring_sqe_set_data(p_sqe, p_data);
}

int schedule_max_reads(struct io_uring* p_ring, int fd, unsigned* p_read_left,
                       __u64* p_offset, const unsigned block_size,
                       unsigned* p_scheduled_reads) {
    unsigned buf_size = *p_read_left > block_size ? block_size : *p_read_left;
    struct io_data* p_data = malloc(buf_size + sizeof(io_data));
    struct io_uring_sqe* p_sqe;
    unsigned reads = 0;
    while (p_data && *p_left_read && p_sqe = io_uring_get_sqe(p_ring)) {
        p_data->buf = p_data + sizeof(io_data);
        p_data->nbytes = buf_size;
        p_data->offset = *p_offset;
        schedule_read(p_sqe, fd, p_data);
        ++reads;
        *p_read_left -= buf_size;
        *offset += buf_size;
        buf_size = *p_read_left > block_size ? block_size : *p_read_left;
        p_data = malloc(buf_size + sizeof(io_data));
    }
    int ret;
    if (reads && ret = io_uring_submit(p_ring)) {
        return ret;
    }
    *p_scheduled_reads += reads;
    if (p_data) {
        free(p_data);
    }
    return 0;
}

int schedule_max_writes(struct io_uring* p_ring, int in, int out,
                        unsigned* p_writes_left, unsigned* p_scheduled_reads,
                        unsigned* p_scheduled_writes) {
    if (!*p_writes_left) {
        return 0;
    }
    struct io_uring_cqe* p_cqe;
    struct io_data* p_data;
    int ret = io_uring_wait_cqe(p_ring, &p_cqe);
    struct io_uring_sqe* p_sqe;
    assert(p_sqe);
    int scheduled = 0;
    do {
        if (!(p_sqe = io_uring_get_sqe(p_ring))) {
            break;
        }
        if (ret) {
            return ret;
        }
        p_data = io_uring_cqe_get_data(p_cqe);
        if (p_cqe->res < 0) {
            if (p_cqe == -EAGAIN) {
                reschedule(p_sqe, in, out, p_data);
                ++scheduled;
                io_uring_cqe_seen(p_ring, p_cqe);
                continue;
            }
            return p_cqe->res;
        } else if (p_cqe->res < p_data->nbytes) {
            p_data->nbytes -= p_cqe->res;
            p_data->offset += p_cqe->res;
            memcpy(p_data->buf, p_data->buf + p_cqe->res, p_data->nbytes);
            reschedule(p_sqe, in, out, p_data);
            ++scheduled;
            io_uring_cqe_seen(p_ring, p_cqe);
            continue;
        }
        if (p_data->read) {
            schedule_write(p_sqe, out, p_data);
            ++scheduled;
            ++(*p_scheduled_writes);
            --(*p_scheduled_reads);
            *p_writes_left -= p_data->nbytes;
        } else {
            free(p_data);
            --(*p_scheduled_writes);
        }
        io_uring_cqe_seen(p_ring, p_cqe);
    } while ((ret = io_uring_peek_cqe(p_ring, &p_cqe)) != -EAGAIN);
    if (scheduled && ret = io_uring_submit(p_ring)) {
        return ret;
    }
    return 0;
}

int copy(int in, int out) {
    const unsigned int queue_size = 4;
    const unsigned block_size = 256 * 1024;
    struct io_uring ring;
    int res;
    if (res = io_uring_queue_init(queue_size, &ring, 0)) {
        return res;
    }
    struct stat statbuf;
    if (fstat(in, &statbuf)) {
        return -errno;
    }
    unsigned write_left = statbuf.st_size;
    unsigned read_left = statbuf.st_size;
    __u64 offset = 0;
    size_t scheduled_writes = 0;
    size_t scheduled_reads = 0;
    while (write_left || scheduled_writes) {
        if (read_left && res = schedule_max_reads(p_ring, in, &read_left, &offset,
                                     block_size, &scheduled_reads)) {
            return res;
        }
        if (res = schedule_max_writes(p_ring, in, out, &writes_left,
                                      &scheduled_reads, &scheduled_writes)) {
            return res;
        }
    }
	return 0;
}
