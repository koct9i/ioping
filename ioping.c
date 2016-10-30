/*
 *  ioping  -- simple I/0 latency measuring tool
 *
 *  Copyright (C) 2011-2015 Konstantin Khlebnikov <koct9i@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef VERSION
# define VERSION "0.9"
#endif

#ifndef EXTRA_VERSION
# define EXTRA_VERSION ""
#endif

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#ifdef __linux__
# include <sys/ioctl.h>
# include <sys/mount.h>
# include <sys/sysmacros.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_POSIX_FADVICE
# define HAVE_POSIX_MEMALIGN
# define HAVE_MKOSTEMP
# define HAVE_DIRECT_IO
# define HAVE_LINUX_ASYNC_IO
# define HAVE_ERR_INCLUDE
# define MAX_RW_COUNT		0x7ffff000 /* 2G - 4K */
#endif

#ifdef __gnu_hurd__
# include <sys/ioctl.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_POSIX_MEMALIGN
# define HAVE_MKOSTEMP
# define HAVE_ERR_INCLUDE
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/ioctl.h>
# include <sys/mount.h>
# include <sys/disk.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_MKOSTEMP
# define HAVE_DIRECT_IO
# define HAVE_ERR_INCLUDE
#endif

#ifdef __DragonFly__
# include <sys/diskslice.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_MKOSTEMP
# define HAVE_ERR_INCLUDE
#endif

#ifdef __OpenBSD__
# include <sys/ioctl.h>
# include <sys/disklabel.h>
# include <sys/dkio.h>
# include <sys/param.h>
# include <sys/mount.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_POSIX_MEMALIGN
# define HAVE_MKOSTEMP
# define HAVE_ERR_INCLUDE
#endif

#ifdef __APPLE__ /* OS X */
# include <sys/ioctl.h>
# include <sys/mount.h>
# include <sys/disk.h>
# include <sys/uio.h>
# define HAVE_NOCACHE_IO
# define HAVE_ERR_INCLUDE
#endif

#ifdef __sun__	/* Solaris */
# include <sys/dkio.h>
# include <sys/vtoc.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_DIRECT_IO
# define O_DIRECT	O_DSYNC
# define HAVE_ERR_INCLUDE
#endif

#ifdef __MINGW32__ /* Windows */
# include <io.h>
# include <stdarg.h>
# include <windows.h>
# define HAVE_DIRECT_IO
# define HAVE_MKOSTEMP /* not required */
#endif

#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
# define HAVE_POSIX_FDATASYNC
#endif

#ifdef HAVE_ERR_INCLUDE
# include <err.h>
#else

#define ERR_PREFIX "ioping: "

void err(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, ERR_PREFIX);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ": %s\n", strerror(errno));
	va_end(ap);
	exit(eval);
}

void errx(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, ERR_PREFIX);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(eval);
}

void warnx(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, ERR_PREFIX);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

#endif /* HAVE_ERR_INCLUDE */

#define NSEC_PER_SEC	1000000000ll

#ifdef HAVE_CLOCK_GETTIME

static inline long long now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		err(3, "clock_gettime failed");

	return ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

#else

static inline long long now(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL))
		err(3, "gettimeofday failed");

	return tv.tv_sec * NSEC_PER_SEC + tv.tv_usec * 1000ll;
}

#endif /* HAVE_CLOCK_GETTIME */

#ifndef HAVE_MKOSTEMP
int mkostemp(char *template, int flags)
{
	int fd;

	fd = mkstemp(template);
	if (!flags || fd < 0)
		return fd;
	close(fd);
	return open(template, O_RDWR | flags);
}
#endif

int async = 0;

#ifdef __MINGW32__

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD r;
	OVERLAPPED o;

	memset(&o, 0, sizeof(o));
	o.Offset = offset;
	o.OffsetHigh = offset >> 32;

	if (ReadFile(h, buf, count, &r, &o))
		return r;

	if (async && GetLastError() == ERROR_IO_PENDING &&
			GetOverlappedResult(h, &o, &r, TRUE))
		return r;

	return -1;
}

ssize_t pwrite(int fd, void *buf, size_t count, off_t offset)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD r;
	OVERLAPPED o;

	memset(&o, 0, sizeof(o));
	o.Offset = offset;
	o.OffsetHigh = offset >> 32;

	if (WriteFile(h, buf, count, &r, &o))
		return r;

	if (async && GetLastError() == ERROR_IO_PENDING &&
			GetOverlappedResult(h, &o, &r, TRUE))
		return r;

	return -1;
}

int fsync(int fd)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);

	return FlushFileBuffers(h) ? 0 : -1;
}

void srandom(unsigned int seed)
{
	srand(seed);
}

long int random(void)
{
	return rand() * (RAND_MAX + 1) + rand();
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
	(void)rem;
	Sleep(req->tv_sec * 1000 + req->tv_nsec / 1000000);
	return 0;
}

#endif /* __MINGW32__ */

#ifndef HAVE_POSIX_MEMALIGN
/* don't free it */
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	char *ptr;
	ptr = malloc(size + alignment);
	if (!ptr)
		return -ENOMEM;
	*memptr = ptr + alignment - (size_t)ptr % alignment;
	return 0;
}
#endif

#ifndef HAVE_POSIX_FDATASYNC
int fdatasync(int fd)
{
	return fsync(fd);
}
#endif

void usage(void)
{
	fprintf(stderr,
			" Usage: ioping [-ABCDRLWYykq] [-c count] [-i interval] [-s size] [-S wsize]\n"
			"               [-o offset] [-w deadline] [-pP period] directory|file|device\n"
			"        ioping -h | -v\n"
			"\n"
			"      -c <count>      stop after <count> requests\n"
			"      -i <interval>   interval between requests (1s)\n"
			"      -l <speed>      speed limit in bytes per second\n"
			"      -t <time>       minimal valid request time (0us)\n"
			"      -T <time>       maximum valid request time\n"
			"      -s <size>       request size (4k)\n"
			"      -S <wsize>      working set size (1m)\n"
			"      -o <offset>     working set offset (0)\n"
			"      -w <deadline>   stop after <deadline> time passed\n"
			"      -p <period>     print raw statistics for every <period> requests\n"
			"      -P <period>     print raw statistics for every <period> in time\n"
			"      -A              use asynchronous I/O\n"
			"      -C              use cached I/O (no cache flush/drop)\n"
			"      -B              print final statistics in raw format\n"
			"      -D              use direct I/O (O_DIRECT)\n"
			"      -R              seek rate test\n"
			"      -L              use sequential operations\n"
			"      -W              use write I/O (please read manpage)\n"
			"      -G              read-write ping-pong mode\n"
			"      -Y              use sync I/O (O_SYNC)\n"
			"      -y              use data sync I/O (O_DSYNC)\n"
			"      -k              keep and reuse temporary file (ioping.tmp)\n"
			"      -q              suppress human-readable output\n"
			"      -h              display this message and exit\n"
			"      -v              display version and exit\n"
			"\n"
	       );
}

void version(void)
{
	fprintf(stderr, "ioping %s\n", VERSION EXTRA_VERSION);
}

struct suffix {
	const char	*txt;
	long long	mul;
};

static struct suffix int_suffix[] = {
	{ "T",		1000000000000ll },
	{ "G",		1000000000ll },
	{ "M",		1000000ll },
	{ "k",		1000ll },
	{ "",		1ll },
	{ "da",		10ll },
	{ "P",		1000000000000000ll },
	{ "E",		1000000000000000000ll },
	{ NULL,		0ll },
};

static struct suffix size_suffix[] = {
	/* These are first match for printing */
	{ "PiB",	1ll<<50 },
	{ "TiB",	1ll<<40 },
	{ "GiB",	1ll<<30 },
	{ "MiB",	1ll<<20 },
	{ "KiB",	1ll<<10 },
	{ "B",		1 },
	{ "",		1 },
	/* Should be decimal, keep binary for compatibility */
	{ "k",		1ll<<10 },
	{ "kb",		1ll<<10 },
	{ "m",		1ll<<20 },
	{ "mb",		1ll<<20 },
	{ "g",		1ll<<30 },
	{ "gb",		1ll<<30 },
	{ "t",		1ll<<40 },
	{ "tb",		1ll<<40 },
	{ "pb",		1ll<<50 },
	{ "eb",		1ll<<60 },
	{ "sector",	512 },
	{ "page",	4096 },
	{ NULL,		0ll },
};

static struct suffix time_suffix[] = {
	{ "hour",	NSEC_PER_SEC * 60 * 60 },
	{ "min",	NSEC_PER_SEC * 60 },
	{ "s",		NSEC_PER_SEC },
	{ "ms",		1000000ll },
	{ "us",		1000ll },
	{ "ns",		1ll },
	{ "nsec",	1ll },
	{ "usec",	1000ll },
	{ "msec",	1000000ll },
	{ "",		NSEC_PER_SEC },
	{ "sec",	NSEC_PER_SEC },
	{ "m",		NSEC_PER_SEC * 60 },
	{ "h",		NSEC_PER_SEC * 60 * 60 },
	{ NULL,		0ll },
};

long long parse_suffix(const char *str, struct suffix *sfx,
		       long long min, long long max)
{
	char *end;
	double val, den;

	val = strtod(str, &end);
	if (*end == '/') {
		if (end == str)
			val = 1;
		den = strtod(end + 1, &end);
		if (!den)
			errx(1, "division by zero in parsing argument: %s", str);
		val /= den;
	}
	for ( ; sfx->txt ; sfx++ ) {
		if (strcasecmp(end, sfx->txt))
			continue;
		val *= sfx->mul;
		if (val < min || val > max)
			errx(1, "integer overflow at parsing argument: %s", str);
		return val;
	}
	errx(1, "invalid suffix: \"%s\"", end);
	return 0;
}

int parse_int(const char *str)
{
	return parse_suffix(str, int_suffix, 0, INT_MAX);
}

ssize_t parse_size(const char *str)
{
	return parse_suffix(str, size_suffix, 0, LONG_MAX);
}

off_t parse_offset(const char *str)
{
	return parse_suffix(str, size_suffix, 0, LLONG_MAX);
}

long long parse_time(const char *str)
{
	return parse_suffix(str, time_suffix, 0, LLONG_MAX);
}

void print_suffix(long long val, struct suffix *sfx)
{
	int precision;

	while (val < sfx->mul && sfx->mul > 1)
		sfx++;

	if (val % sfx->mul == 0)
		precision = 0;
	else if (val >= sfx->mul * 10)
		precision = 1;
	else
		precision = 2;

	printf("%.*f", precision, val * 1.0 / sfx->mul);
	if (*sfx->txt)
		printf(" %s", sfx->txt);
}

void print_int(long long val)
{
	print_suffix(val, int_suffix);
}

void print_size(long long val)
{
	print_suffix(val, size_suffix);
}

void print_time(long long val)
{
	print_suffix(val, time_suffix);
}

char *path = NULL;
char *fstype = "";
char *device = "";
off_t device_size = 0;

int fd;
void *buf;

int quiet = 0;
int batch_mode = 0;
int direct = 0;
int cached = 0;
int syncio = 0;
int data_syncio = 0;
int randomize = 1;
int write_test = 0;
int write_read_test = 0;

long long period_request = 0;
long long period_time = 0;

int custom_interval, custom_deadline;
long long interval = NSEC_PER_SEC;
struct timespec interval_ts;
long long deadline = 0;
long long speed_limit = 0;

long long min_valid_time = 0;
long long max_valid_time = LLONG_MAX;

ssize_t default_size = 1<<12;
ssize_t size = 0;
off_t wsize = 0;
off_t temp_wsize = 1<<20;

int keep_file = 0;

off_t offset = 0;
off_t woffset = 0;

long long stop_at_request = 0;

int exiting = 0;

void parse_options(int argc, char **argv)
{
	int opt;

	if (argc < 2) {
		usage();
		exit(1);
	}

	while ((opt = getopt(argc, argv, "hvkALRDCWGYBqyi:t:T:w:s:S:c:o:p:P:l:")) != -1) {
		switch (opt) {
			case 'h':
				usage();
				exit(0);
			case 'v':
				version();
				exit(0);
			case 'L':
				randomize = 0;
				default_size = 1<<18;
				break;
			case 'l':
				speed_limit = parse_size(optarg);
				break;
			case 'R':
				if (!custom_interval)
					interval = 0;
				if (!custom_deadline)
					deadline = 3 * NSEC_PER_SEC;
				temp_wsize = 1<<26;
				quiet = 1;
				break;
			case 'D':
				direct = 1;
				break;
			case 'C':
				cached = 1;
				break;
			case 'A':
				async = 1;
				break;
			case 'W':
				write_test++;
				break;
			case 'G':
				write_test++;
				write_read_test = 1;
				break;
			case 'Y':
				syncio = 1;
				break;
			case 'y':
				data_syncio = 1;
				break;
			case 'i':
				interval = parse_time(optarg);
				custom_interval = 1;
				break;
			case 't':
				min_valid_time = parse_time(optarg);
				break;
			case 'T':
				max_valid_time = parse_time(optarg);
				break;
			case 'w':
				deadline = parse_time(optarg);
				custom_deadline = 1;
				break;
			case 's':
				size = parse_size(optarg);
				break;
			case 'S':
				wsize = parse_offset(optarg);
				break;
			case 'o':
				offset = parse_offset(optarg);
				break;
			case 'p':
				period_request = parse_int(optarg);
				break;
			case 'P':
				period_time = parse_time(optarg);
				break;
			case 'q':
				quiet = 1;
				break;
			case 'B':
				quiet = 1;
				batch_mode = 1;
				break;
			case 'c':
				stop_at_request = parse_int(optarg);
				break;
			case 'k':
				keep_file = 1;
				break;
			case '?':
				usage();
				exit(1);
		}
	}

	if (optind > argc-1)
		errx(1, "no destination specified");
	if (optind < argc-1)
		errx(1, "more than one destination specified");
	path = argv[optind];
}

#ifdef __linux__

void parse_device(dev_t dev)
{
	char *buf = NULL, *ptr;
	unsigned major, minor;
	struct stat st;
	size_t len;
	FILE *file;
	char *real;

	/* since v2.6.26 */
	file = fopen("/proc/self/mountinfo", "r");
	if (!file)
		goto old;
	while (getline(&buf, &len, file) > 0) {
		sscanf(buf, "%*d %*d %u:%u", &major, &minor);
		if (makedev(major, minor) != dev)
			continue;
		ptr = strstr(buf, " - ") + 3;
		fstype = strdup(strsep(&ptr, " "));
		device = strdup(strsep(&ptr, " "));
		goto out;
	}
old:
	/* for older versions */
	file = fopen("/proc/mounts", "r");
	if (!file)
		return;
	while (getline(&buf, &len, file) > 0) {
		ptr = buf;
		strsep(&ptr, " ");
		if (*buf != '/' || stat(buf, &st) || st.st_rdev != dev)
			continue;
		strsep(&ptr, " ");
		fstype = strdup(strsep(&ptr, " "));
		device = strdup(buf);
		goto out;
	}
out:
	free(buf);
	fclose(file);
	real = realpath(device, NULL);
	if (real) {
		free(device);
		device = real;
	}
}

#elif defined(__APPLE__) || defined(__OpenBSD__) \
	|| defined(__FreeBSD__) || defined(__FreeBSD_kernel__)

void parse_device(dev_t dev)
{
	struct statfs fs;
	(void)dev;

	if (statfs(path, &fs))
		return;

	fstype = strdup(fs.f_fstypename);
	device = strdup(fs.f_mntfromname);
}

#else

void parse_device(dev_t dev)
{
	(void)dev;
}

#endif

int get_device_size(int fd, struct stat *st)
{
	unsigned long long blksize = 0;
	int ret = 0;

#if defined(BLKGETSIZE64)
	/* linux */
	ret = ioctl(fd, BLKGETSIZE64, &blksize);
#elif defined(DIOCGMEDIASIZE)
	/* freebsd */
	ret = ioctl(fd, DIOCGMEDIASIZE, &blksize);
#elif defined(DKIOCGETBLOCKCOUNT)
	/* macos */
	ret = ioctl(fd, DKIOCGETBLOCKCOUNT, &blksize);
	blksize <<= 9;
#elif defined(__gnu_hurd__)
	/* hurd */
	blksize = st->st_size;
#elif defined(__MINGW32__)
	blksize = 0;
#elif defined(__DragonFly__)
	struct partinfo pinfo;
	ret = ioctl(fd, DIOCGPART, &pinfo);
	blksize = pinfo.media_size;
#elif defined(__OpenBSD__)
	struct disklabel label;
	struct partition part;

	ret = ioctl(fd, DIOCGDINFO, &label);
	part = label.d_partitions[DISKPART(st->st_rdev)];

	blksize = DL_GETPSIZE(&part) * label.d_secsize;
#elif defined(__sun__)
	struct dk_minfo dkmp;

	ret = ioctl(fd, DKIOCGMEDIAINFO, &dkmp);
	blksize =  dkmp.dki_capacity * dkmp.dki_lbsize;
#else
# warning no get disk size method
	ret = -1;
	errno = ENOSYS;
	blksize = 0;
#endif
	(void)fd;

	st->st_size = blksize;
	return ret;
}

ssize_t do_pwrite(int fd, void *buf, size_t nbytes, off_t offset)
{
	ssize_t ret;

	ret = pwrite(fd, buf, nbytes, offset);
	if (ret < 0)
		return ret;

	if (!cached && fdatasync(fd) < 0)
		err(3, "fdatasync failed, please retry with option -C");

	return ret;
}

ssize_t (*make_pread) (int fd, void *buf, size_t nbytes, off_t offset) = pread;
ssize_t (*make_pwrite) (int fd, void *buf, size_t nbytes, off_t offset) = do_pwrite;
ssize_t (*make_request) (int fd, void *buf, size_t nbytes, off_t offset) = pread;

#ifdef HAVE_LINUX_ASYNC_IO

#include <sys/syscall.h>
#include <linux/aio_abi.h>

static long io_setup(unsigned nr_reqs, aio_context_t *ctx) {
	return syscall(__NR_io_setup, nr_reqs, ctx);
}

static long io_submit(aio_context_t ctx, long n, struct iocb **paiocb) {
	return syscall(__NR_io_submit, ctx, n, paiocb);
}

static long io_getevents(aio_context_t ctx, long min_nr, long nr,
		struct io_event *events, struct timespec *tmo) {
	return syscall(__NR_io_getevents, ctx, min_nr, nr, events, tmo);
}

#if 0
static long io_cancel(aio_context_t ctx, struct iocb *aiocb,
		struct io_event *res) {
	return syscall(__NR_io_cancel, ctx, aiocb, res);
}

static long io_destroy(aio_context_t ctx) {
	return syscall(__NR_io_destroy, ctx);
}
#endif

aio_context_t aio_ctx;
struct iocb aio_cb;
struct iocb *aio_cbp = &aio_cb;
struct io_event aio_ev;

static ssize_t aio_pread(int fd, void *buf, size_t nbytes, off_t offset)
{
	aio_cb.aio_lio_opcode = IOCB_CMD_PREAD;
	aio_cb.aio_fildes = fd;
	aio_cb.aio_buf = (unsigned long) buf;
	aio_cb.aio_nbytes = nbytes;
	aio_cb.aio_offset = offset;

	if (io_submit(aio_ctx, 1, &aio_cbp) != 1)
		err(1, "aio submit failed");

	if (io_getevents(aio_ctx, 1, 1, &aio_ev, NULL) != 1)
		err(1, "aio getevents failed");

	if (aio_ev.res < 0) {
		errno = -aio_ev.res;
		return -1;
	}

	return aio_ev.res;
}

static ssize_t aio_pwrite(int fd, void *buf, size_t nbytes, off_t offset)
{
	aio_cb.aio_lio_opcode = IOCB_CMD_PWRITE;
	aio_cb.aio_fildes = fd;
	aio_cb.aio_buf = (unsigned long) buf;
	aio_cb.aio_nbytes = nbytes;
	aio_cb.aio_offset = offset;

	if (io_submit(aio_ctx, 1, &aio_cbp) != 1)
		err(1, "aio submit failed");

	if (io_getevents(aio_ctx, 1, 1, &aio_ev, NULL) != 1)
		err(1, "aio getevents failed");

	if (aio_ev.res < 0) {
		errno = -aio_ev.res;
		return -1;
	}

	if (!cached && fdatasync(fd) < 0)
		err(3, "fdatasync failed, please retry with option -C");

	return aio_ev.res;

#if 0
	aio_cb.aio_lio_opcode = IOCB_CMD_FDSYNC;
	if (io_submit(aio_ctx, 1, &aio_cbp) != 1)
		err(1, "aio fdsync submit failed");

	if (io_getevents(aio_ctx, 1, 1, &aio_ev, NULL) != 1)
		err(1, "aio getevents failed");

	if (aio_ev.res < 0)
		return aio_ev.res;
#endif
}

static void aio_setup(void)
{
	memset(&aio_ctx, 0, sizeof aio_ctx);
	memset(&aio_cb, 0, sizeof aio_cb);

	if (io_setup(1, &aio_ctx))
		err(2, "aio setup failed");

	make_pread = aio_pread;
	make_pwrite = aio_pwrite;
}

#else

static void aio_setup(void)
{
#ifndef __MINGW32__
	errx(1, "asynchronous I/O not supported by this platform");
#endif
}

#endif

#ifdef __MINGW32__

int open_file(const char *path, const char *temp)
{
	char *file_path = (char *)path;
	DWORD action = OPEN_ALWAYS;
	DWORD attr = 0;
	HANDLE h;

	if (temp) {
		int length = strlen(path) + strlen(temp) + 9;

		file_path = malloc(length);
		if (!file_path)
			err(2, NULL);

		snprintf(file_path, length, "%s\\%s", path, temp);

		if (!keep_file) {
			strcat(file_path, ".XXXXXX");
			mktemp(file_path);
			action = CREATE_NEW;
			attr |= FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE;
		}
	}

	if (direct)
		attr |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
	if (randomize)
		attr |= FILE_FLAG_RANDOM_ACCESS;
	else
		attr |= FILE_FLAG_SEQUENTIAL_SCAN;
	if (async)
		attr |= FILE_FLAG_OVERLAPPED;

	h = CreateFile(file_path, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, action, attr, NULL);

	if (file_path != path)
		free(file_path);

	if (h == INVALID_HANDLE_VALUE)
		return -1;
	return _open_osfhandle((long)h, 0);
}

BOOL WINAPI sig_exit(DWORD type)
{
	switch (type) {
		case CTRL_C_EVENT:
			if (exiting)
				exit(4);
			exiting = 1;
			return TRUE;
		default:
			return FALSE;
	}
}

void set_signal(void)
{
	SetConsoleCtrlHandler(sig_exit, TRUE);
}

#else /* __MINGW32__ */

int open_file(const char *path, const char *temp)
{
	char *file_path = NULL;
	int length, fd;
	int flags = 0;

#ifdef O_SYNC
	if (syncio)
		flags |= O_SYNC;
#endif

#ifdef O_DSYNC
	if (data_syncio)
		flags |= O_DSYNC;
#endif

	if (!temp) {
		fd = open(path, (write_test ? O_RDWR : O_RDONLY) | flags);
		if (fd < 0)
			goto out;
		goto done;
	}

	length = strlen(path) + strlen(temp) + 9;
	file_path = malloc(length);
	if (!file_path)
		err(2, NULL);
	snprintf(file_path, length, "%s/%s", path, temp);

	if (keep_file) {
		fd = open(file_path, O_RDWR | O_CREAT | flags, 0600);
		if (fd < 0)
			goto out;
		goto done;
	}

#ifdef O_TMPFILE
	fd = open(path, O_RDWR | O_TMPFILE | flags, 0600);
	if (fd >= 0)
		goto done;
#endif

	strcat(file_path, ".XXXXXX");
	fd = mkostemp(file_path, flags);
	if (fd < 0)
		goto out;
	if (unlink(file_path))
		err(2, "unlink \"%s\" failed", file_path);

done:
#ifdef HAVE_DIRECT_IO
	if (direct && fcntl(fd, F_SETFL, O_DIRECT))
		err(2, "fcntl(O_DIRECT) failed, "
		       "please retry without option -D");
#endif
out:
	if (file_path != path)
		free(file_path);
	return fd;
}

void sig_exit(int signo)
{
	(void)signo;
	if (exiting)
		exit(4);
	exiting = 1;
}

void set_signal(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_exit;
	sigaction(SIGINT, &sa, NULL);
}

#endif /* __MINGW32__ */

static unsigned long long random_state[2];

/* xorshift128+ */
static inline unsigned long long random64(void)
{
	unsigned long long s1 = random_state[0];
	const unsigned long long s0 = random_state[1];
	random_state[0] = s0;
	s1 ^= s1 << 23; // a
	random_state[1] = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26); // b, c
	return random_state[1] + s0;
}

static void random_init(void)
{
	srandom(now());
	random_state[0] = random();
	random_state[1] = random();
	(void)random64();
	(void)random64();
}

static void random_memory(void *buf, size_t len)
{
	unsigned long long *ptr = buf;
	size_t words = len >> 3;

	while (words--)
		*ptr++ = random64();

	if (len & 7) {
		unsigned long long last = random64();
		memcpy(ptr, &last, len & 7);
	}
}

struct statistics {
	long long start, finish, load_time;
	long long count, valid, too_slow, too_fast;
	long long min, max;
	double sum, sum2, avg, mdev;
	double speed, iops, load_speed, load_iops;
	long long size, load_size;
};

static void start_statistics(struct statistics *s, unsigned long long start) {
	memset(s, 0, sizeof(*s));
	s->min = LLONG_MAX;
	s->max = LLONG_MIN;
	s->start = start;
}

static void add_statistics(struct statistics *s, long long val) {
	s->count++;
	if (val < min_valid_time) {
		s->too_fast++;
	} else if (val > max_valid_time) {
		s->too_slow++;
	} else {
		s->valid++;
		s->sum += val;
		s->sum2 += (double)val * val;
		if (val < s->min)
			s->min = val;
		if (val > s->max)
			s->max = val;
	}
}

static void merge_statistics(struct statistics *s, struct statistics *o) {
	s->count += o->count;
	s->too_fast += o->too_fast;
	s->too_slow += o->too_slow;
	if (o->valid) {
		s->valid += o->valid;
		s->sum += o->sum;
		s->sum2 += o->sum2;
		if (o->min < s->min)
			s->min = o->min;
		if (o->max > s->max)
			s->max = o->max;
	}
}

static void finish_statistics(struct statistics *s, unsigned long long finish) {
	s->finish = finish;
	s->load_time = finish - s->start;

	if (s->valid) {
		s->avg = s->sum / s->valid;
		s->mdev = sqrt(s->sum2 / s->valid - s->avg * s->avg);
	} else {
		s->min = 0;
		s->max = 0;
	}

	if (s->sum)
		s->iops = (double)NSEC_PER_SEC * s->valid / s->sum;

	if (s->load_time)
		s->load_iops = (double)NSEC_PER_SEC * s->count / s->load_time;

	s->speed = s->iops * size;
	s->load_speed = s->load_iops * size;
	s->size = s->valid * size;
	s->load_size = s->count * size;
}

static void dump_statistics(struct statistics *s) {
	printf("%lu %.0f %.0f %.0f %lu %.0f %lu %.0f %lu %lu\n",
			(unsigned long)s->valid, s->sum,
			s->iops, s->speed,
			(unsigned long)s->min, s->avg,
			(unsigned long)s->max, s->mdev,
			(unsigned long)s->count,
			(unsigned long)s->load_time);
}

int main (int argc, char **argv)
{
	ssize_t ret_size;
	struct stat st;
	int ret;

	struct statistics part, total;

	long long request, this_time;
	long long time_now, time_next, period_deadline;

	setvbuf(stdout, NULL, _IOLBF, 0);

	parse_options(argc, argv);

	if (!size)
		size = default_size;

	if (size <= 0)
		errx(1, "request size must be greater than zero");

	if (custom_interval && speed_limit)
		errx(1, "speed limit and interval cannot be set simultaneously");

	if (speed_limit)
		interval = size * NSEC_PER_SEC / speed_limit;

#ifdef MAX_RW_COUNT
	if (size > MAX_RW_COUNT)
		warnx("this platform supports requests %u bytes at most",
				MAX_RW_COUNT);
#endif

	if (wsize)
		temp_wsize = wsize;
	else if (size > temp_wsize)
		temp_wsize = size;

#if !defined(HAVE_POSIX_FADVICE) && !defined(HAVE_NOCACHE_IO)
# if defined(HAVE_DIRECT_IO)
	if (!direct && !cached) {
		warnx("non-cached I/O not supported, will use direct I/O");
		direct = cached = 1;
	}
# else
	if (!cached && !write_test) {
		warnx("non-cached I/O not supported by this platform");
		warnx("you can use write I/O to get reliable results");
		cached = 1;
	}
# endif
#endif

	if (async)
		aio_setup();

	make_request = write_test ? make_pwrite : make_pread;

#ifndef HAVE_DIRECT_IO
	if (direct)
		errx(1, "direct I/O not supported by this platform");
#endif

#ifndef O_SYNC
	if (syncio)
		errx(1, "sync I/O not supported by this platform");
#endif

#ifndef O_DSYNC
	if (data_syncio)
		errx(1, "data sync I/O not supported by this platform");
#endif

	if (stat(path, &st))
		err(2, "stat \"%s\" failed", path);

	if (!S_ISDIR(st.st_mode) && write_test && write_test < 3)
		errx(2, "think twice, then use -WWW to shred this target");

	if (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode)) {
		if (S_ISDIR(st.st_mode))
			st.st_size = offset + temp_wsize;
		parse_device(st.st_dev);
	} else if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
		fd = open_file(path, NULL);
		if (fd < 0)
			err(2, "failed to open \"%s\"", path);

		if (get_device_size(fd, &st)) {
			if (!S_ISCHR(st.st_mode))
				err(2, "block get size ioctl failed");
			st.st_size = offset + temp_wsize;
			fstype = "character";
			device = "device";
		} else {
			device_size = st.st_size;
			fstype = "block";
			device = "device ";
		}
	} else {
		errx(2, "unsupported destination: \"%s\"", path);
	}

	if (wsize > st.st_size || offset > st.st_size - wsize)
		errx(2, "target is too small for this");

	if (!wsize)
		wsize = st.st_size - offset;

	if (size > wsize)
		errx(2, "request size is too big for this target");

	ret = posix_memalign(&buf, 0x1000, size);
	if (ret)
		errx(2, "buffer allocation failed");

	random_init();

	random_memory(buf, size);

	if (S_ISDIR(st.st_mode)) {
		fd = open_file(path, "ioping.tmp");
		if (fd < 0)
			err(2, "failed to create temporary file at \"%s\"", path);
		if (keep_file) {
			if (fstat(fd, &st))
				err(2, "fstat at \"%s\" failed", path);
			if (st.st_size >= offset + wsize)
#ifndef __MINGW32__
			    if (st.st_blocks >= (st.st_size + 511) / 512)
#endif
				goto skip_preparation;
		}
		for (woffset = 0 ; woffset < wsize ; woffset += ret_size) {
			ret_size = size;
			if (woffset + ret_size > wsize)
				ret_size = wsize - woffset;
			if (woffset)
				random_memory(buf, ret_size);
			ret_size = pwrite(fd, buf, ret_size, offset + woffset);
			if (ret_size <= 0)
				err(2, "preparation write failed");
		}
skip_preparation:
		if (fsync(fd))
			err(2, "fsync failed");
	} else if (S_ISREG(st.st_mode)) {
		fd = open_file(path, NULL);
		if (fd < 0)
			err(2, "failed to open \"%s\"", path);
	}

	/* No readahead for non-cached I/O, we'll invalidate it anyway */
	if (randomize || !cached) {
#ifdef HAVE_POSIX_FADVICE
		ret = posix_fadvise(fd, offset, wsize, POSIX_FADV_RANDOM);
		if (ret)
			warn("fadvise(RANDOM) failed, "
			     "operations might perform unneeded readahead");
#endif
	}

	if (!cached) {
#ifdef HAVE_NOCACHE_IO
		ret = fcntl(fd, F_NOCACHE, 1);
		if (ret)
			err(2, "fcntl(F_NOCACHE) failed, "
			       "please retry with option -C");
#endif
	}

	set_signal();

	request = 0;
	woffset = 0;

	time_now = now();

	start_statistics(&part, time_now);
	start_statistics(&total, time_now);

	if (deadline)
		deadline += time_now;

	period_deadline = time_now + period_time;

	time_next = time_now;

	while (!exiting) {
		request++;

		if (randomize)
			woffset = random64() % (wsize / size) * size;

#ifdef HAVE_POSIX_FADVICE
		if (!cached) {
			ret = posix_fadvise(fd, offset + woffset, size,
					    POSIX_FADV_DONTNEED);
			if (ret)
				err(3, "fadvise(DONTNEED) failed, "
				       "please retry with option -C");
		}
#endif

		if (write_read_test) {
			write_test = request & 1;
			make_request = write_test ? make_pwrite : make_pread;
		}

		if (write_test)
			random_memory(buf, size);

		this_time = now();

		ret_size = make_request(fd, buf, size, offset + woffset);

		if (ret_size < 0) {
			if (errno != EINTR)
				err(3, "request failed");
		} else if (ret_size < size)
			warnx("request returned less than expected: %zu", ret_size);
		else if (ret_size > size)
			errx(3, "request returned more than expected: %zu", ret_size);

		time_now = now();

		time_next += interval;
		if ((time_now - time_next) > 0)
			time_next = time_now;

		this_time = time_now - this_time;

		if (request == 1) {
			/* warmup */
			part.count++;
		} else {
			add_statistics(&part, this_time);
		}

		if (!quiet) {
			print_size(ret_size);
			printf(" %s %s (%s %s", write_test ? ">>>" : "<<<",
					path, fstype, device);
			if (device_size)
				print_size(device_size);
			printf("): request=%lu time=", (long unsigned)request);
			print_time(this_time);
			if (request == 1) {
				printf(" (warmup)");
			} else if (this_time < min_valid_time) {
				printf(" (too fast)");
			} else if (this_time > max_valid_time) {
				printf(" (too slow)");
			} else if (part.valid > 5 && part.min < part.max) {
				int percent = (this_time - part.min) * 100 /
						(part.max - part.min);
				if (percent < 5)
					printf(" (fast)");
				else if (percent > 95)
					printf(" (slow)");
			}
			printf("\n");
		}

		if ((period_request && (part.count >= period_request)) ||
		    (period_time && (time_next >= period_deadline))) {
			finish_statistics(&part, time_now);
			dump_statistics(&part);
			merge_statistics(&total, &part);
			start_statistics(&part, time_now);
			period_deadline = time_now + period_time;
		}

		if (!randomize) {
			woffset += size;
			if (woffset + size > wsize)
				woffset = 0;
		}

		if (exiting)
			break;

		if (stop_at_request && request >= stop_at_request)
			break;

		if (deadline && time_next >= deadline)
			break;

		if ((time_next - time_now) > 0) {
			long long delta = time_next - time_now;

			interval_ts.tv_sec = delta / NSEC_PER_SEC;
			interval_ts.tv_nsec = delta % NSEC_PER_SEC;
			nanosleep(&interval_ts, NULL);
		}
	}

	time_now = now();
	finish_statistics(&part, time_now);
	merge_statistics(&total, &part);
	finish_statistics(&total, time_now);

	if (batch_mode) {
		dump_statistics(&total);
		return 0;
	}

	if (quiet && (period_time || period_request))
		return 0;

	printf("\n--- %s (%s %s", path, fstype, device);
	if (device_size)
		print_size(device_size);
	printf(") ioping statistics ---\n");
	print_int(total.valid);
	printf(" requests completed in ");
	print_time(total.sum);
	printf(", ");
	print_size(total.size);
	printf("%s, ", write_read_test ? "" :
			write_test ? " written" : " read");
	print_int(total.iops);
	printf(" iops, ");
	print_size(total.speed);
	printf("/s\n");

	if (total.too_fast) {
		print_int(total.too_fast);
		printf(" too fast, ");
	}
	if (total.too_slow) {
		print_int(total.too_slow);
		printf(" too slow, ");
	}
	printf("generated ");
	print_int(total.count);
	printf(" requests in ");
	print_time(total.load_time);
	printf(", ");
	print_size(total.load_size);
	printf(", ");
	print_int(total.load_iops);
	printf(" iops, ");
	print_size(total.load_speed);
	printf("/s\n");

	printf("min/avg/max/mdev = ");
	print_time(total.min);
	printf(" / ");
	print_time(total.avg);
	printf(" / ");
	print_time(total.max);
	printf(" / ");
	print_time(total.mdev);
	printf("\n");

	return 0;
}
