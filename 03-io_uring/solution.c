#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "liburing.h"

#define QD	4
#define BS	(256 * 1024)

struct io_data {
	int read;
	off_t first_offset, second_offset;
	size_t size;
	char* buf;
};

int get_file_size(int fd, off_t* p_size) {
	struct stat st;

	if (fstat(fd, &st)) {
		return -errno;
    }

    *p_size = st.st_size;
	return 0;
}

void reschedule(struct io_uring* p_ring, struct io_data* p_data, int infd, int outfd,
                off_t processed_len) {
	struct io_uring_sqe* p_sqe;

	p_sqe = io_uring_get_sqe(p_ring);
	assert(p_sqe);
    p_data->second_offset += processed_len;

	if (p_data->read) {
		io_uring_prep_read(p_sqe, infd, p_data->buf + p_data->second_offset,
                           p_data->size - p_data->second_offset,
                           p_data->first_offset + p_data->second_offset);
	} else {
		io_uring_prep_write(p_sqe, outfd, p_data->buf + p_data->second_offset,
                            p_data->size - p_data->second_offset,
                            p_data->first_offset + p_data->second_offset);
    }

	io_uring_sqe_set_data(p_sqe, p_data);
    io_uring_submit(p_ring);
}

int schedule_read(struct io_uring* p_ring, off_t size, off_t offset, int infd) {
	struct io_uring_sqe *p_sqe;
	struct io_data *p_data;

	if (!(p_data = malloc(size + sizeof(struct io_data)))) {
        return 1;
    }

	if (!(p_sqe = io_uring_get_sqe(p_ring))) {
        free(p_data);
        return 1;
    }

	p_data->read = 1;
    p_data->first_offset = offset;
    p_data->second_offset = 0;
	p_data->buf = (char*) p_data + sizeof(struct io_data);
	p_data->size = size;

	io_uring_prep_read(p_sqe, infd, p_data->buf, size, offset);
	io_uring_sqe_set_data(p_sqe, p_data);
    io_uring_submit(p_ring);
	return 0;
}

void schedule_write(struct io_uring* p_ring, struct io_data* p_data, int outfd) {
	p_data->read = 0;
	p_data->second_offset = 0;
    
    struct io_uring_sqe* p_sqe = io_uring_get_sqe(p_ring);
    assert(p_sqe);
    
	io_uring_prep_write(p_sqe, outfd, p_data->buf, p_data->size, p_data->first_offset);
    io_uring_sqe_set_data(p_sqe, p_data);
	io_uring_submit(p_ring);
}

int copy(int in, int out) {
	unsigned long reads, writes;
	struct io_uring_cqe* p_cqe;
    struct io_uring ring;
	off_t read_left, write_left, offset;
	int ret;
    
    if ((ret = io_uring_queue_init(QD, &ring, 0))) {
        return ret;
    }

    if ((ret = get_file_size(in, &read_left))) {
        return ret;
    }

    write_left = read_left;
	writes = reads = offset = 0;

	while (read_left || write_left) {
		int got_comp;
	
		/*
		 * Queue up as many reads as we can
		 */
		unsigned long before_reads = reads;
		while (read_left) {
			off_t cur_size = read_left;

			if (reads + writes >= QD) {
				break;
            }
			if (cur_size > BS) {
				cur_size = BS;
            } else if (!cur_size) {
				break;
            }

			if (schedule_read(&ring, cur_size, offset, in)) {
				break;
            }

			read_left -= cur_size;
			offset += cur_size;
			++reads;
		}

		if (before_reads != reads) {
			if ((ret = io_uring_submit(&ring)) < 0) {
				return ret;
			}
		}

		/*
		 * Queue is full at this point. Find at least one completion.
		 */
		got_comp = 0;
		while (write_left) {
			struct io_data* p_data;

			if (!got_comp) {
				ret = io_uring_wait_cqe(&ring, &p_cqe);
				got_comp = 1;
			} else {
				if ((ret = io_uring_peek_cqe(&ring, &p_cqe)) == -EAGAIN) {
					p_cqe = NULL;
					ret = 0;
				}
			}
			if (ret < 0) {
				return ret;
			}
			if (!p_cqe)
				break;

			p_data = io_uring_cqe_get_data(p_cqe);
			if (p_cqe->res < 0) {
				if (p_cqe->res == -EAGAIN) {
					reschedule(&ring, p_data, in, out, 0);
					io_uring_cqe_seen(&ring, p_cqe);
					continue;
				}
				return p_cqe->res;
			} else if ((size_t) p_cqe->res != p_data->size) {
				/* Short read/write, adjust and requeue */
			    reschedule(&ring, p_data, in, out, p_cqe->res);	
				io_uring_cqe_seen(&ring, p_cqe);
				continue;
			}

			/*
			 * All done. if write, nothing else to do. if read,
			 * queue up corresponding write.
			 */
			if (p_data->read) {
				schedule_write(&ring, p_data, out);
				write_left -= p_data->size;
				--reads;
				++writes;
			} else {
				free(p_data);
				--writes;
			}
			io_uring_cqe_seen(&ring, p_cqe);
		}
	}

	/* wait out pending writes */
    while (writes) {
		struct io_data* p_data;

		if ((ret = io_uring_wait_cqe(&ring, &p_cqe))) {
			return ret;
		}
		if (p_cqe->res < 0) {
			return p_cqe->res;
		}
		p_data = io_uring_cqe_get_data(p_cqe);
		free(p_data);
		--writes;
		io_uring_cqe_seen(&ring, p_cqe);
	}

	return 0;
}

