/*
 *  ioping  -- simple I/0 latency measuring tool
 *
 *  Copyright (C) 2011-2013 Konstantin Khlebnikov <koct9i@gmail.com>
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

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
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
# define HAVE_POSIX_FADVICE
# define HAVE_POSIX_MEMALIGN
# define HAVE_DIRECT_IO
# define HAVE_LINUX_ASYNC_IO
# define HAVE_ERR_INCLUDE
#endif

#ifdef __gnu_hurd__
# include <sys/ioctl.h>
# define HAVE_POSIX_MEMALIGN
# define HAVE_ERR_INCLUDE
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/ioctl.h>
# include <sys/mount.h>
# include <sys/disk.h>
# define HAVE_DIRECT_IO
# define HAVE_ERR_INCLUDE
#endif

#ifdef __DragonFly__
# include <sys/diskslice.h>
# define HAVE_ERR_INCLUDE
#endif

#ifdef __OpenBSD__
# include <sys/ioctl.h>
# include <sys/disklabel.h>
# include <sys/dkio.h>
# include <sys/param.h>
# include <sys/mount.h>
# define HAVE_POSIX_MEMALIGN
# define HAVE_ERR_INCLUDE
#endif

#ifdef __APPLE__
# include <sys/ioctl.h>
# include <sys/mount.h>
# include <sys/disk.h>
# include <sys/uio.h>
# define HAVE_NOCACHE_IO
# define HAVE_ERR_INCLUDE
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

#ifdef __MINGW32__
#include <io.h>
#include <stdarg.h>
#include <windows.h>

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
	return rand();
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
			" Usage: ioping [-LABCDWRq] [-c count] [-w deadline] [-pP period] [-i interval]\n"
			"               [-s size] [-S wsize] [-o offset] directory|file|device\n"
			"        ioping -h | -v\n"
			"\n"
			"      -c <count>      stop after <count> requests\n"
			"      -w <deadline>   stop after <deadline>\n"
			"      -p <period>     print raw statistics for every <period> requests\n"
			"      -P <period>     print raw statistics for every <period> in time\n"
			"      -i <interval>   interval between requests (1s)\n"
			"      -s <size>       request size (4k)\n"
			"      -S <wsize>      working set size (1m)\n"
			"      -o <offset>     working set offset (0)\n"
			"      -L              use sequential operations (includes -s 256k)\n"
			"      -A              use asynchronous I/O\n"
			"      -C              use cached I/O\n"
			"      -D              use direct I/O\n"
			"      -W              use write I/O *DANGEROUS*\n"
			"      -R              seek rate test (same as -q -i 0 -w 3 -S 64m)\n"
			"      -B              print final statistics in raw format\n"
			"      -q              suppress human-readable output\n"
			"      -h              display this message and exit\n"
			"      -v              display version and exit\n"
			"\n"
	       );
}

#ifndef VERSION
# warning ioping version undefined
# define VERSION ""
#endif

void version(void)
{
	fprintf(stderr, "ioping %s\n", VERSION);
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
	{ "day",	1000000ll * 60 * 60 * 24 },
	{ "hour",	1000000ll * 60 * 60 },
	{ "min",	1000000ll * 60 },
	{ "s",		1000000ll },
	{ "ms",		1000ll },
	{ "us",		1ll },
	{ "usec",	1ll },
	{ "msec",	1000ll },
	{ "",		1000000ll },
	{ "sec",	1000000ll },
	{ "m",		1000000ll * 60 },
	{ "h",		1000000ll * 60 * 60 },
	{ "week",	1000000ll * 60 * 60 * 24 * 7 },
	{ "month",	1000000ll * 60 * 60 * 24 * 7 * 30 },
	{ "year",	1000000ll * 60 * 60 * 24 * 7 * 365 },
	{ "century",	1000000ll * 60 * 60 * 24 * 7 * 365 * 100 },
	{ "millenium",	1000000ll * 60 * 60 * 24 * 7 * 365 * 1000 },
	{ NULL,		0ll },
};

long long parse_suffix(const char *str, struct suffix *sfx)
{
	char *end;
	double val;

	val = strtod(str, &end);
	for ( ; sfx->txt ; sfx++ ) {
		if (!strcasecmp(end, sfx->txt))
			return val * sfx->mul;
	}
	errx(1, "invalid suffix: \"%s\"", end);
	return 0;
}

long long parse_int(const char *str)
{
	return parse_suffix(str, int_suffix);
}

long long parse_size(const char *str)
{
	return parse_suffix(str, size_suffix);
}

long long parse_time(const char *str)
{
	return parse_suffix(str, time_suffix);
}

void print_suffix(int64_t val, struct suffix *sfx)
{
	while (val < sfx->mul && sfx->mul > 1)
		sfx++;
	if (sfx->mul == 1)
		printf("%" PRId64, val);
	else
		printf("%.1f", val * 1.0 / sfx->mul);
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

long long now(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL))
		err(3, "gettimeofday failed");

	return tv.tv_sec * 1000000ll + tv.tv_usec;
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
int async = 0;
int cached = 0;
int randomize = 1;
int write_test = 0;

ssize_t (*make_request) (int fd, void *buf, size_t nbytes, off_t offset) = pread;

int period_request = 0;
long long period_time = 0;

long long interval = 1000000;
struct timespec interval_ts;
long long deadline = 0;

ssize_t size = 1<<12;
off_t wsize = 0;
off_t temp_wsize = 1<<20;

off_t offset = 0;
off_t woffset = 0;

int request;
int count = 0;

int exiting = 0;

void parse_options(int argc, char **argv)
{
	int opt;

	if (argc < 2) {
		usage();
		exit(1);
	}

	while ((opt = getopt(argc, argv, "hvALRDCWBqi:w:s:S:c:o:p:P:")) != -1) {
		switch (opt) {
			case 'h':
				usage();
				exit(0);
			case 'v':
				version();
				exit(0);
			case 'L':
				randomize = 0;
				size = 1<<18;
				break;
			case 'R':
				interval = 0;
				deadline = 3000000;
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
				break;
			case 'w':
				deadline = parse_time(optarg);
				break;
			case 's':
				size = parse_size(optarg);
				break;
			case 'S':
				wsize = parse_size(optarg);
				break;
			case 'o':
				offset = parse_size(optarg);
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
				count = parse_int(optarg);
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

off_t get_device_size(int fd, struct stat *st)
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
#else
# error no get disk size method
#endif
	(void)fd;
	(void)st;

	if (ret)
		err(2, "block get size ioctl failed");

	return blksize;
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
	errx(1, "asynchronous I/O not supportted by this platform");
}

#endif

#ifdef __MINGW32__

int create_temp(char *path, char *template)
{
	int length = strlen(path) + strlen(template) + 2;
	char *temp = malloc(length);
	HANDLE h;
	DWORD attr;

	if (!temp)
		err(2, NULL);
	snprintf(temp, length, "%s\\%s", path, template);
	mktemp(temp);

	attr = FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE;
	if (!cached)
		attr |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
	if (randomize)
		attr |= FILE_FLAG_RANDOM_ACCESS;
	else
		attr |= FILE_FLAG_SEQUENTIAL_SCAN;

	h = CreateFile(temp, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, CREATE_ALWAYS, attr, NULL);

	free(temp);
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

int create_temp(char *path, char *template)
{
	int length = strlen(path) + strlen(template) + 2;
	char *temp = malloc(length);
	int fd;

	if (!temp)
		err(2, NULL);
	snprintf(temp, length, "%s/%s", path, template);

	fd = mkstemp(temp);
	if (fd < 0)
		err(2, "failed to create temporary file at \"%s\"", path);
	if (unlink(temp))
		err(2, "unlink \"%s\" failed", temp);
	free(temp);
# ifdef HAVE_DIRECT_IO
	if (direct && fcntl(fd, F_SETFL, O_DIRECT))
		errx(2, "fcntl failed, please retry without -D");
# endif
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

int main (int argc, char **argv)
{
	long ret_size;
	struct stat st;
	int ret, flags;

	int part_request;
	long long this_time, time_total;
	double part_min, part_max, time_min, time_max;
	double time_sum, time_sum2, time_mdev, time_avg;
	double part_sum, part_sum2, part_mdev, part_avg;
	long long time_now, time_next, period_deadline;

	parse_options(argc, argv);

	interval_ts.tv_sec = interval / 1000000;
	interval_ts.tv_nsec = (interval % 1000000) * 1000;

	if (wsize)
		temp_wsize = wsize;
	else if (size > temp_wsize)
		temp_wsize = size;

	if (size <= 0)
		errx(1, "request size must be greather than zero");

	flags = O_RDONLY;

#if !defined(HAVE_POSIX_FADVICE) && !defined(HAVE_NOCACHE_IO)
# if defined(HAVE_DIRECT_IO)
	direct |= !cached;
# else
	if (!cached)
		warnx("non-cached I/O not supportted by this platform,"
				" results will be unreliable.");
	cached = 1;
# endif
#endif

	if (write_test) {
		flags = O_RDWR;
		make_request = do_pwrite;
	}

	if (async)
		aio_setup();

	if (direct)
#ifdef HAVE_DIRECT_IO
		flags |= O_DIRECT;
#else
		errx(1, "direct I/O not supportted by this platform");
#endif

#ifdef __MINGW32__
	flags |= O_BINARY;
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
		fd = open(path, flags);
		if (fd < 0)
			err(2, "failed to open \"%s\"", path);

		device_size = get_device_size(fd, &st);
		st.st_size = device_size;
		fstype = "device";
	} else {
		errx(2, "unsupported destination: \"%s\"", path);
	}

	if (offset + wsize > st.st_size)
		errx(2, "target is too small for this");

	if (!wsize)
		wsize = st.st_size - offset;

	if (size > wsize)
		errx(2, "request size is too big for this target");

	ret = posix_memalign(&buf, 0x1000, size);
	if (ret)
		errx(2, "buffer allocation failed");
	memset(buf, '*', size);

	if (S_ISDIR(st.st_mode)) {
		fd = create_temp(path, "ioping.XXXXXX");
		for (woffset = 0 ; woffset + size <= wsize ; woffset += size) {
			if (pwrite(fd, buf, size, offset + woffset) != size)
				err(2, "write failed");
		}
		if (fsync(fd))
			err(2, "fsync failed");
	} else if (S_ISREG(st.st_mode)) {
		fd = open(path, flags);
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

	srandom(now());

	if (deadline)
		deadline += now();

	set_signal();

	request = 0;
	woffset = 0;

	part_request = 0;
	part_min = time_min = LLONG_MAX;
	part_max = time_max = LLONG_MIN;
	part_sum = time_sum = 0;
	part_sum2 = time_sum2 = 0;

	time_now = now();
	time_total = time_now;
	period_deadline = time_now + period_time;

	while (!exiting) {
		request++;
		part_request++;

		if (randomize)
			woffset = random() % (wsize / size) * size;

#ifdef HAVE_POSIX_FADVICE
		if (!cached) {
			ret = posix_fadvise(fd, offset + woffset, size,
					POSIX_FADV_DONTNEED);
			if (ret)
				err(3, "fadvise failed");
		}
#endif

		this_time = now();

		ret_size = make_request(fd, buf, size, offset + woffset);
		if (ret_size < 0 && errno != EINTR)
			err(3, "request failed");

		time_now = now();
		this_time = time_now - this_time;
		time_next = time_now + interval;

		part_sum += this_time;
		part_sum2 += this_time * this_time;
		if (this_time < part_min)
			part_min = this_time;
		if (this_time > part_max)
			part_max = this_time;

		if (!quiet) {
			print_size(ret_size);
			printf(" from %s (%s %s", path, fstype, device);
			if (device_size)
				print_size(device_size);
			printf("): request=%d time=", request);
			print_time(this_time);
			printf("\n");
		}

		if ((period_request && (part_request >= period_request)) ||
		    (period_time && (time_next >= period_deadline))) {
			part_avg = part_sum / part_request;
			part_mdev = sqrt(part_sum2 / part_request - part_avg * part_avg);

			printf("%d %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n",
					part_request, part_sum,
					1000000. * part_request / part_sum,
					1000000. * part_request * size / part_sum,
					part_min, part_avg,
					part_max, part_mdev);

			time_sum += part_sum;
			time_sum2 += part_sum2;
			if (part_min < time_min)
				time_min = part_min;
			if (part_max > time_max)
				time_max = part_max;
			part_min = LLONG_MAX;
			part_max = LLONG_MIN;
			part_sum = part_sum2 = 0;
			part_request = 0;

			period_deadline = time_now + period_time;
		}

		if (!randomize) {
			woffset += size;
			if (woffset + size > wsize)
				woffset = 0;
		}

		if (exiting)
			break;

		if (count && request >= count)
			break;

		if (deadline && time_next >= deadline)
			break;

		if (interval)
			nanosleep(&interval_ts, NULL);
	}

	time_total = now() - time_total;

	time_sum += part_sum;
	time_sum2 += part_sum2;
	if (part_min < time_min)
		time_min = part_min;
	if (part_max > time_max)
		time_max = part_max;

	time_avg = time_sum / request;
	time_mdev = sqrt(time_sum2 / request - time_avg * time_avg);

	if (batch_mode) {
		printf("%d %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n",
				request, time_sum,
				1000000. * request / time_sum,
				1000000. * request * size / time_sum,
				time_min, time_avg,
				time_max, time_mdev);
	} else if (!quiet || (!period_time && !period_request)) {
		printf("\n--- %s (%s %s", path, fstype, device);
		if (device_size)
			print_size(device_size);
		printf(") ioping statistics ---\n");
		print_int(request);
		printf(" requests completed in ");
		print_time(time_total);
		printf(", ");
		print_int(request * 1000000. / time_sum);
		printf(" iops, ");
		print_size(request * size * 1000000. / time_sum);
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
