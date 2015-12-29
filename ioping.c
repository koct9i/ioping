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
# define HAVE_CLOCK_GETTIME
# define HAVE_POSIX_FADVICE
# define HAVE_POSIX_MEMALIGN
# define HAVE_DIRECT_IO
# define HAVE_LINUX_ASYNC_IO
# define HAVE_ERR_INCLUDE
# define MAX_RW_COUNT		0x7ffff000 /* 2G - 4K */
#endif

#ifdef __gnu_hurd__
# include <sys/ioctl.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_POSIX_MEMALIGN
# define HAVE_ERR_INCLUDE
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/ioctl.h>
# include <sys/mount.h>
# include <sys/disk.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_DIRECT_IO
# define HAVE_ERR_INCLUDE
#endif

#ifdef __DragonFly__
# include <sys/diskslice.h>
# define HAVE_CLOCK_GETTIME
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
			" Usage: ioping [-ABCDRLWkq] [-c count] [-i interval] [-s size] [-S wsize]\n"
			"               [-o offset] [-w deadline] [-pP period] directory|file|device\n"
			"        ioping -h | -v\n"
			"\n"
			"      -c <count>      stop after <count> requests\n"
			"      -i <interval>   interval between requests (1s)\n"
			"      -t <time>       minimal valid request time (0us)\n"
			"      -s <size>       request size (4k)\n"
			"      -S <wsize>      working set size (1m)\n"
			"      -o <offset>     working set offset (0)\n"
			"      -w <deadline>   stop after <deadline> time passed\n"
			"      -p <period>     print raw statistics for every <period> requests\n"
			"      -P <period>     print raw statistics for every <period> in time\n"
			"      -A              use asynchronous I/O\n"
			"      -C              use cached I/O\n"
			"      -B              print final statistics in raw format\n"
			"      -D              use direct I/O\n"
			"      -R              seek rate test\n"
			"      -L              use sequential operations\n"
			"      -W              use write I/O (please read manpage)\n"
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
	double val;

	val = strtod(str, &end);
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
int randomize = 1;
int write_test = 0;

ssize_t (*make_request) (int fd, void *buf, size_t nbytes, off_t offset) = pread;

long long period_request = 0;
long long period_time = 0;

int custom_interval, custom_deadline;
long long interval = NSEC_PER_SEC;
struct timespec interval_ts;
long long deadline = 0;

long long min_valid_time = 0;

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

	while ((opt = getopt(argc, argv, "hvkALRDCWBqi:t:w:s:S:c:o:p:P:")) != -1) {
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
			case 'i':
				interval = parse_time(optarg);
				custom_interval = 1;
				break;
			case 't':
				min_valid_time = parse_time(optarg);
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
		return -1;
	return ret;
}

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
		return -1;

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

	make_request = write_test ? aio_pwrite : aio_pread;
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

	if (!temp) {
		fd = open(path, write_test ? O_RDWR : O_RDONLY);
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
		fd = open(file_path, O_RDWR|O_CREAT, 0600);
		if (fd < 0)
			goto out;
		goto done;
	}

#ifdef O_TMPFILE
	fd = open(path, O_RDWR|O_TMPFILE, 0600);
	if (fd >= 0)
		goto done;
#endif

	strcat(file_path, ".XXXXXX");
	fd = mkstemp(file_path);
	if (fd < 0)
		goto out;
	if (unlink(file_path))
		err(2, "unlink \"%s\" failed", file_path);

done:
#ifdef HAVE_DIRECT_IO
	if (direct && fcntl(fd, F_SETFL, O_DIRECT))
		errx(2, "fcntl failed, please retry without -D");
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

int main (int argc, char **argv)
{
	ssize_t ret_size;
	struct stat st;
	int ret;

	long long request, part_request;
	long long total_valid, part_valid;
	long long this_time;
	double part_min, part_max, time_min, time_max;
	double time_sum, time_sum2, time_mdev, time_avg;
	double part_sum, part_sum2, part_mdev, part_avg;
	long long time_now, time_next, period_deadline;

	setvbuf(stdout, NULL, _IOLBF, 0);

	parse_options(argc, argv);

	interval_ts.tv_sec = interval / NSEC_PER_SEC;
	interval_ts.tv_nsec = interval % NSEC_PER_SEC;

	if (!size)
		size = default_size;

	if (size <= 0)
		errx(1, "request size must be greather than zero");

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
	if (!direct && !cached)
		direct = cached = 1;
# else
	if (!cached && !write_test) {
		warnx("non-cached read I/O not supported by this platform");
		warnx("you can use write I/O to get reliable results");
		cached = 1;
	}
# endif
#endif

	if (write_test)
		make_request = do_pwrite;

	if (async)
		aio_setup();

#ifndef HAVE_DIRECT_IO
	if (direct)
		errx(1, "direct I/O not supported by this platform");
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

		if (!cached && write_test && fdatasync(fd)) {
			warnx("fdatasync not supported by \"%s\", "
			      "enable cached requests", path);
			cached = 1;
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

	if (!cached) {
#ifdef HAVE_POSIX_FADVICE
		ret = posix_fadvise(fd, offset, wsize, POSIX_FADV_RANDOM);
		if (ret)
			err(2, "fadvise failed");
#endif
#ifdef HAVE_NOCACHE_IO
		ret = fcntl(fd, F_NOCACHE, 1);
		if (ret)
			err(2, "fcntl nocache failed");
#endif
	}

	if (deadline)
		deadline += now();

	set_signal();

	request = 0;
	total_valid = 0;
	woffset = 0;

	part_request = 0;
	part_valid = 0;

	part_min = time_min = LLONG_MAX;
	part_max = time_max = LLONG_MIN;
	part_sum = time_sum = 0;
	part_sum2 = time_sum2 = 0;

	time_now = now();
	period_deadline = time_now + period_time;

	while (!exiting) {
		request++;
		part_request++;

		if (randomize)
			woffset = random64() % (wsize / size) * size;

#ifdef HAVE_POSIX_FADVICE
		if (!cached) {
			ret = posix_fadvise(fd, offset + woffset, size,
					POSIX_FADV_DONTNEED);
			if (ret)
				err(3, "fadvise failed");
		}
#endif

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
		this_time = time_now - this_time;
		time_next = time_now + interval;

		if (this_time >= min_valid_time) {
			part_valid++;
			part_sum += this_time;
			part_sum2 += this_time * this_time;
			if (this_time < part_min)
				part_min = this_time;
			if (this_time > part_max)
				part_max = this_time;
		}

		if (!quiet) {
			print_size(ret_size);
			printf(" %s %s (%s %s", write_test ? "to" : "from",
					path, fstype, device);
			if (device_size)
				print_size(device_size);
			printf("): request=%lu time=", (long unsigned)request);
			print_time(this_time);
			if (this_time < min_valid_time)
				printf(" (cache hit)");
			printf("\n");
		}

		if ((period_request && (part_request >= period_request)) ||
		    (period_time && (time_next >= period_deadline))) {

			time_sum += part_sum;
			time_sum2 += part_sum2;
			if (part_min < time_min)
				time_min = part_min;
			if (part_max > time_max)
				time_max = part_max;

			if (part_valid) {
				part_avg = part_sum / part_valid;
				part_mdev = sqrt(part_sum2 / part_valid -
						 part_avg * part_avg);
			} else {
				part_min = 0;
				part_avg = 0;
				part_max = 0;
				part_mdev = 0;
				part_sum = 0.1;
			}

			printf("%lu %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n",
					(unsigned long)part_valid, part_sum,
					1. * NSEC_PER_SEC *
						part_valid / part_sum,
					1. * NSEC_PER_SEC *
						part_valid * size / part_sum,
					part_min, part_avg,
					part_max, part_mdev);

			part_min = LLONG_MAX;
			part_max = LLONG_MIN;
			part_sum = part_sum2 = 0;
			part_request = 0;
			total_valid += part_valid;
			part_valid = 0;

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

		if (interval)
			nanosleep(&interval_ts, NULL);
	}

	time_sum += part_sum;
	time_sum2 += part_sum2;
	if (part_min < time_min)
		time_min = part_min;
	if (part_max > time_max)
		time_max = part_max;
	total_valid += part_valid;

	if (total_valid) {
		time_avg = time_sum / total_valid;
		time_mdev = sqrt(time_sum2 / total_valid - time_avg * time_avg);
	} else {
		time_min = 0;
		time_avg = 0;
		time_max = 0;
		time_mdev = 0;
		time_sum = 0.1;
	}

	if (batch_mode) {
		printf("%lu %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n",
				(unsigned long)total_valid, time_sum,
				1. * NSEC_PER_SEC *
					total_valid / time_sum,
				1. * NSEC_PER_SEC *
					total_valid * size / time_sum,
				time_min, time_avg,
				time_max, time_mdev);
	} else if (!quiet || (!period_time && !period_request)) {
		printf("\n--- %s (%s %s", path, fstype, device);
		if (device_size)
			print_size(device_size);
		printf(") ioping statistics ---\n");
		print_int(total_valid);
		printf(" requests completed in ");
		print_time(time_sum);
		printf(", ");
		if (total_valid < request) {
			print_int(request - total_valid);
			printf(" cache hits, ");
		}
		print_size((long long)total_valid * size);
		printf(" %s, ", write_test ? "written" : "read");
		print_int(1. * NSEC_PER_SEC * total_valid / time_sum);
		printf(" iops, ");
		print_size(1. * NSEC_PER_SEC * total_valid * size / time_sum);
		printf("/s\n");
		printf("min/avg/max/mdev = ");
		print_time(time_min);
		printf(" / ");
		print_time(time_avg);
		printf(" / ");
		print_time(time_max);
		printf(" / ");
		print_time(time_mdev);
		printf("\n");
	}

	return 0;
}
