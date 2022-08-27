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
# define VERSION "1.3"
#endif

#ifndef EXTRA_VERSION
# define EXTRA_VERSION ""
#endif

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#ifdef __MINGW32__
# define _WIN32_WINNT 0x0600
# define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
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

#define HAVE_GETOPT_LONG_ONLY

#ifdef __linux__
# include <sys/ioctl.h>
# include <sys/sysmacros.h>
# include <sys/syscall.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_POSIX_FADVICE
# define HAVE_POSIX_MEMALIGN
# define HAVE_MKOSTEMP
# define HAVE_DIRECT_IO
# define HAVE_LINUX_ASYNC_IO
# define HAVE_ERR_INCLUDE
# define HAVE_STATVFS
# define MAX_RW_COUNT		0x7ffff000 /* 2G - 4K */

# undef RWF_NOWAIT
# include <linux/fs.h>
# include <linux/aio_abi.h>
# ifndef RWF_NOWAIT
#  define aio_rw_flags aio_reserved1
# endif

# undef RWF_NOWAIT
# include <sys/uio.h>
# ifdef RWF_NOWAIT
#  define HAVE_LINUX_PREADV2
# endif

# ifndef RWF_NOWAIT
#  define RWF_NOWAIT	0x00000008
# endif

# ifndef RWF_HIPRI
#  define RWF_HIPRI	0x00000001
# endif

#else /* __linux__ */

# ifndef RWF_NOWAIT
#  define RWF_NOWAIT	0
# endif

# ifndef RWF_HIPRI
#  define RWF_HIPRI	0
# endif

#endif /* __linux__ */

#ifdef __gnu_hurd__
# include <sys/ioctl.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_POSIX_MEMALIGN
# define HAVE_MKOSTEMP
# define HAVE_ERR_INCLUDE
# define HAVE_STATVFS
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/ioctl.h>
# include <sys/mount.h>
# include <sys/disk.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_MKOSTEMP
# define HAVE_DIRECT_IO
# define HAVE_ERR_INCLUDE
# define HAVE_STATVFS
#endif

#ifdef __DragonFly__
# include <sys/diskslice.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_MKOSTEMP
# define HAVE_ERR_INCLUDE
# define HAVE_STATVFS
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
# define HAVE_STATVFS
#endif

#ifdef __APPLE__ /* OS X */
# include <sys/ioctl.h>
# include <sys/mount.h>
# include <sys/disk.h>
# include <sys/uio.h>
# define HAVE_FULLFSYNC
# define HAVE_NOCACHE_IO
# define HAVE_ERR_INCLUDE
# define HAVE_STATVFS
#endif

#ifdef __sun	/* Solaris */
# include <sys/dkio.h>
# include <sys/vtoc.h>
# define HAVE_CLOCK_GETTIME
# define HAVE_POSIX_FADVICE
# define HAVE_ERR_INCLUDE
# define HAVE_STATVFS
#endif

#ifdef __MINGW32__ /* Windows */
# include <io.h>
# include <stdarg.h>
# include <windows.h>
# define HAVE_DIRECT_IO
# define HAVE_SYNC_IO
# define HAVE_MKOSTEMP /* not required */
#endif

#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
# define HAVE_POSIX_FDATASYNC
#endif

#ifdef O_SYNC
# define HAVE_SYNC_IO
#endif

#ifdef O_DSYNC
# define HAVE_DATA_SYNC_IO
#endif

#ifdef HAVE_STATVFS
# include <sys/statvfs.h>
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
#define USEC_PER_SEC	1000000L

int timestamp_uptodate;
char timestamp_str[64];
char localtime_str[64];
const char *localtime_fmt = "%b %d %T";

#ifdef HAVE_CLOCK_GETTIME

static inline long long now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		err(3, "clock_gettime failed");

	return ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

static inline void update_timestamp(void)
{
	struct timespec ts;
	struct tm tm;

	if (timestamp_uptodate)
		return;

	timestamp_uptodate = 1;

	if (clock_gettime(CLOCK_REALTIME, &ts))
		err(3, "clock_gettime failed");

	snprintf(timestamp_str, sizeof(timestamp_str), "%f",
		 ts.tv_sec + (double)ts.tv_nsec / NSEC_PER_SEC);

	localtime_r(&ts.tv_sec, &tm);
	strftime(localtime_str, sizeof(localtime_str), localtime_fmt, &tm);
}

#else

static inline long long now(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL))
		err(3, "gettimeofday failed");

	return tv.tv_sec * NSEC_PER_SEC + tv.tv_usec * 1000ll;
}

static inline void update_timestamp(void)
{
	struct timeval tv;
	struct tm tm;

	if (timestamp_uptodate)
		return;

	timestamp_uptodate = 1;

	if (gettimeofday(&tv, NULL))
		err(3, "gettimeofday failed");

	snprintf(timestamp_str, sizeof(timestamp_str), "%f",
		 tv.tv_sec + (double)tv.tv_usec / USEC_PER_SEC);

	time_t t = tv.tv_sec; /* windows bug */
	localtime_r(&t, &tm);
	strftime(localtime_str, sizeof(localtime_str), localtime_fmt, &tm);
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

void version(void)
{
	fprintf(stdout, "ioping %s\n", VERSION EXTRA_VERSION);
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

double parse_suffix(const char *str, struct suffix *sfx,
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
long long device_size = 0;

int target_fd = -1;
void *buf;

const char *notice = NULL;

int quiet = 0;
int time_info = 0;
int batch_mode = 0;
int direct = 0;
int cached = 0;
int rw_flags = 0;
int syncio = 0;
int data_syncio = 0;
int randomize = 1;
int write_test = 0;
int write_read_test = 0;

unsigned long long random_entropy = 0;

long long period_request = 0;
long long period_time = 0;

int custom_interval, custom_deadline;
long long interval = NSEC_PER_SEC;
struct timespec interval_ts;
long long deadline = 0;
long long speed_limit = 0;
double rate_limit = 0;

long long min_valid_time = 0;
long long max_valid_time = LLONG_MAX;

ssize_t default_size = 1<<12;
ssize_t size = 0;
off_t wsize = 0;
off_t temp_wsize = 1<<20;

int keep_file = 0;

off_t offset = 0;
off_t woffset = 0;

long long request = 0;
long long warmup_request = 1;
long long burst = 0;
long long burst_request = 0;
long long stop_at_request = 0;

int json = 0;
int json_line = 0;

int exiting = 0;

const char *options = "hvkALRDNHCWGYBqyi:t:T:w:s:S:c:o:p:P:l:r:a:I::Je:b:";

#ifdef HAVE_GETOPT_LONG_ONLY

static struct option long_options[] = {
	{"help",	no_argument,		NULL,	'h'},
	{"version",	no_argument,		NULL,	'v'},

	{"keep",	no_argument,		NULL,	'k'},

	{"quiet",	no_argument,		NULL,	'q'},
	{"batch",	no_argument,		NULL,	'B'},
	{"time",	optional_argument,	NULL,   'I'},
	{"json",	no_argument,		NULL,	'J'},

	{"rapid",	no_argument,		NULL,	'R'},
	{"linear",	no_argument,		NULL,	'L'},
	{"direct",	no_argument,		NULL,	'D'},
	{"cached",	no_argument,		NULL,	'C'},
	{"nowait",	no_argument,		NULL,   'N'},
	{"hipri",	no_argument,		NULL,   'H'},
	{"sync",	no_argument,		NULL,	'Y'},
	{"dsync",	no_argument,		NULL,	'y'},
	{"async",	no_argument,		NULL,	'A'},
	{"write",	no_argument,		NULL,	'W'},
	{"read-write",	no_argument,		NULL,	'G'},

	{"size",	required_argument,	NULL,	's'},
	{"work-size",	required_argument,	NULL,	'S'},
	{"work-offset",	required_argument,	NULL,	'o'},

	{"count",	required_argument,	NULL,	'c'},
	{"work-time",	required_argument,	NULL,	'w'},

	{"interval",	required_argument,	NULL,	'i'},
	{"burst",	required_argument,	NULL,	'b'},
	{"speed-limit",	required_argument,	NULL,	'l'},
	{"rate-limit",  required_argument,	NULL,	'r'},

	{"warmup",	required_argument,	NULL,	'a'},
	{"min-time",	required_argument,	NULL,	't'},
	{"max-time",	required_argument,	NULL,	'T'},

	{"print-count", required_argument,	NULL,	'p'},
	{"print-interval", required_argument,	NULL,	'P'},

	{"entropy",	required_argument,	NULL,	'e'},

	{0,		0,			NULL,	0},
};

#endif /* HAVE_GETOPT_LONG */

void usage(FILE *output)
{
	fprintf(output,
			" Usage: ioping [options...] directory|file|device\n"
			"        ioping -h | -v\n"
			"\n"
			" options:\n"
			"      -A, -async                 use asynchronous I/O\n"
			"      -C, -cached                use cached I/O (no cache flush/drop)\n"
			"      -D, -direct                use direct I/O (O_DIRECT)\n"
			"      -G, -read-write            read-write ping-pong mode\n"
			"      -L, -linear                use sequential operations\n"
			"      -N, -nowait                use nowait I/O (RWF_NOWAIT)\n"
			"      -H, -hipri                 use high priority I/O (RWF_HIPRI)\n"
			"      -W, -write                 use write I/O (please read manpage)\n"
			"      -Y, -sync                  use sync I/O (O_SYNC)\n"
			"      -y, -dsync                 use data sync I/O (O_DSYNC)\n"
			"      -R, -rapid                 test with rapid I/O during 3s (-q -i 0 -w 3)\n"
			"      -k, -keep                  keep and reuse temporary file (ioping.tmp)\n"
			"\n"
			" parameters:\n"
			"      -a, -warmup <count>        ignore <count> first requests (1)\n"
			"      -b, -burst <count>         make <count> requsts without delay (0)\n"
			"      -c, -count <count>         stop after <count> requests\n"
			"      -e, -entropy <seed>        seed for random number generator (0)\n"
			"      -i, -interval <time>       interval between requests (1s)\n"
			"      -s, -size <size>           request size (4k)\n"
			"      -S, -work-size <size>      working set size (1m)\n"
			"      -o, -work-offset <size>    working set offset (0)\n"
			"      -w, -work-time <time>      stop after <time> passed\n"
			"      -l, -speed-limit <size>    limit speed with <size> per second\n"
			"      -r, -rate-limit <count>    limit rate with <count> per second\n"
			"      -t, -min-time <time>       minimal valid request time (0us)\n"
			"      -T, -max-time <time>       maximum valid request time\n"
			"\n"
			" output:\n"
			"      -B, -batch                 print final statistics in raw format\n"
			"      -I, -time [format]         print current time for every request\n"
			"      -J, -json                  print output in JSON format\n"
			"      -p, -print-count <count>   print statistics for every <count> requests\n"
			"      -P, -print-interval <time> print statistics for every <time>\n"
			"      -q, -quiet                 suppress human-readable output\n"
			"      -h, -help                  display this message and exit\n"
			"      -v, -version               display version and exit\n"
			"\n"
	       );
}

void parse_options(int argc, char **argv)
{
	int opt;

	if (argc < 2) {
		usage(stdout);
		exit(1);
	}

	while ((opt =
#ifdef HAVE_GETOPT_LONG_ONLY
		getopt_long_only(argc, argv, options, long_options, NULL)
#else
		getopt(argc, argv, options)
#endif
	) != -1) {

		switch (opt) {
			case 'h':
				usage(stdout);
				exit(0);
			case 'v':
				version();
				exit(0);
			case 'L':
				randomize = 0;
				default_size = 1<<18;
				break;
			case 'e':
				random_entropy = strtoull(optarg, NULL, 0);
				break;
			case 'l':
				if (!custom_interval)
					interval = 0;
				speed_limit = parse_size(optarg);
				break;
			case 'r':
				if (!custom_interval)
					interval = 0;
				rate_limit = parse_suffix(optarg, int_suffix, 0, NSEC_PER_SEC);
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
			case 'N':
				rw_flags |= RWF_NOWAIT;
				break;
			case 'H':
				rw_flags |= RWF_HIPRI;
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
			case 'I':
				time_info = 1;
				if (optarg)
					localtime_fmt = optarg;
				break;
			case 'J':
				json = 1;
				localtime_fmt = "%FT%T%z";
				break;
			case 'c':
				stop_at_request = parse_int(optarg);
				break;
			case 'a':
				warmup_request = parse_int(optarg);
				break;
			case 'b':
				burst = parse_int(optarg);
				break;
			case 'k':
				keep_file = 1;
				break;
			case '?':
				fprintf(stderr, "\n");
				usage(stderr);
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
	struct statvfs vfs;
	size_t len;
	FILE *file;
	char *real;

	if (!fstatvfs(target_fd, &vfs))
		device_size = (long long)vfs.f_frsize * vfs.f_blocks;

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
		struct stat st;

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
	device_size = (long long)fs.f_bsize * fs.f_blocks;
}

#elif defined(__MINGW32__)

void parse_device(dev_t dev)
{
	HANDLE h = (HANDLE)_get_osfhandle(target_fd);
	ULARGE_INTEGER total;
	DWORD flags;
	wchar_t wname[MAX_PATH + 1];
	wchar_t wtype[MAX_PATH + 1];

	(void)dev;

	if (GetDiskFreeSpaceExA(path, NULL ,&total, NULL))
		device_size = total.QuadPart;

	if (GetVolumeInformationByHandleW(h, wname, MAX_PATH,
					  NULL, NULL, &flags,
					  wtype, MAX_PATH)) {
		size_t len;

		len = wcstombs(NULL, wname, 0) + 1;
		device = malloc(len);
		wcstombs(device, wname, len);

		len = wcstombs(NULL, wtype, 0) + 1;
		fstype = malloc(len);
		wcstombs(fstype, wtype, len);
	}
}

#else

void parse_device(dev_t dev)
{
# warning no method to get filesystem name, device and size
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
#elif defined(__sun)
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
       return pwrite(fd, buf, nbytes, offset);
}

void sync_file(int fd)
{
#ifdef HAVE_FULLFSYNC
	static bool have_fullfsync = true;

	if (have_fullfsync) {
		if (fcntl(fd, F_FULLFSYNC, 0) < 0) {
			warn("fcntl(F_FULLFSYNC) failed, fallback to fsync");
			have_fullfsync = false;
		} else
			return;
	}
#endif

#ifdef HAVE_POSIX_FDATASYNC
	if (fdatasync(fd) < 0)
		err(3, "fdatasync failed, please retry with option -C");
#else
	if (fsync(fd) < 0)
		err(3, "fsync failed, please retry with option -C");
#endif
}

ssize_t (*make_pread) (int fd, void *buf, size_t nbytes, off_t offset) = pread;
ssize_t (*make_pwrite) (int fd, void *buf, size_t nbytes, off_t offset) = do_pwrite;
ssize_t (*make_request) (int fd, void *buf, size_t nbytes, off_t offset) = pread;

#ifdef HAVE_LINUX_PREADV2

ssize_t do_preadv2(int fd, void *buf, size_t nbytes, off_t offset)
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = nbytes,
	};

	return preadv2(fd, &iov, 1, offset, rw_flags);
}

ssize_t do_pwritev2(int fd, void *buf, size_t nbytes, off_t offset)
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = nbytes,
	};

	return pwritev2(fd, &iov, 1, offset, rw_flags);
}

#endif /* HAVE_LINUX_PREADV2 */

#ifdef HAVE_LINUX_ASYNC_IO

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
	aio_cb.aio_buf = (intptr_t)buf;
	aio_cb.aio_nbytes = nbytes;
	aio_cb.aio_offset = offset;
	aio_cb.aio_rw_flags = rw_flags;

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
	aio_cb.aio_buf = (intptr_t)buf;
	aio_cb.aio_nbytes = nbytes;
	aio_cb.aio_offset = offset;
	aio_cb.aio_rw_flags = rw_flags;

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

static void aio_setup(void)
{
	memset(&aio_ctx, 0, sizeof aio_ctx);
	memset(&aio_cb, 0, sizeof aio_cb);

	if (io_setup(1, &aio_ctx))
		err(2, "aio setup failed");

	make_pread = aio_pread;
	make_pwrite = aio_pwrite;
}

#else /* HAVE_LINUX_ASYNC_IO */

static void aio_setup(void)
{
#ifndef __MINGW32__
	errx(1, "asynchronous I/O not supported by this platform");
#endif
}

#endif /* HAVE_LINUX_ASYNC_IO */

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
		attr |= FILE_FLAG_NO_BUFFERING;
	if (syncio)
		attr |= FILE_FLAG_WRITE_THROUGH;
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

	return _open_osfhandle((intptr_t)h, 0);
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

/* splitmix64 */
static unsigned long long random64_seed(void)
{
	unsigned long long result = random_entropy;

	random_entropy = result + 0x9E3779B97f4A7C15;
	result = (result ^ (result >> 30)) * 0xBF58476D1CE4E5B9;
	result = (result ^ (result >> 27)) * 0x94D049BB133111EB;
	return result ^ (result >> 31);
}

static void random_init(void)
{
	if (!random_entropy)
		random_entropy = now();
	random_state[0] = random64_seed();
	random_state[1] = random64_seed();
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

static int add_statistics(struct statistics *s, long long val) {
	s->count++;
	if (request <= warmup_request) {
		notice = "warmup";
	} else if (val < min_valid_time) {
		notice = "too fast";
		s->too_fast++;
	} else if (val > max_valid_time) {
		notice = "too slow";
		s->too_slow++;
	} else {
		s->valid++;
		s->sum += val;
		s->sum2 += (double)val * val;
		if (val < s->min)
			s->min = val;
		if (val > s->max)
			s->max = val;

		notice = NULL;
		if (s->valid > 5) {
			long long avg = s->sum / s->valid;
			if (val * 2 < avg)
				notice = "fast";
			else if (val > avg * 2)
				notice = "slow";
		}

		return 1;
	}

	return 0;
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
	printf("%llu %.0f %.0f %.0f %llu %.0f %llu %.0f %llu %llu\n",
	       s->valid, s->sum, s->iops, s->speed,
	       s->min, s->avg, s->max, s->mdev,
	       s->count, s->load_time);
}

static void json_request(long long io_size, long long io_time, int valid)
{
	update_timestamp();

	printf("%s{\n"
	       "  \"timestamp\": %s,\n"
	       "  \"localtime\": \"%s\",\n"
	       "  \"target\": {\n"
	       "    \"path\": \"%s\",\n"
	       "    \"fstype\": \"%s\",\n"
	       "    \"device\": \"%s\",\n"
	       "    \"device_size\": %lld\n"
	       "  },\n"
	       "  \"io\": {\n"
	       "    \"request\": %lld,\n"
	       "    \"operation\": \"%s\",\n"
	       "    \"offset\": %lld,\n"
	       "    \"size\": %lld,\n"
	       "    \"time\": %llu,\n"
	       "    \"ignored\": %s\n"
	       "  }\n"
	       "}",
	       json_line++ ? "," : "",
	       timestamp_str,
	       localtime_str,
	       path,
	       fstype,
	       device,
	       device_size,
	       request,
	       write_test ? "write" : "read",
	       (long long)offset + woffset,
	       io_size,
	       io_time,
	       valid ? "false" : "true");
}

static void json_statistics(struct statistics *s)
{
	update_timestamp();

	printf("%s{\n"
	       "  \"timestamp\": %s,\n"
	       "  \"localtime\": \"%s\",\n"
	       "  \"target\": {\n"
	       "    \"path\": \"%s\",\n"
	       "    \"fstype\": \"%s\",\n"
	       "    \"device\": \"%s\",\n"
	       "    \"device_size\": %lld\n"
	       "  },\n"
	       "  \"stat\": {\n"
	       "    \"count\": %llu,\n"
	       "    \"size\": %llu,\n"
	       "    \"time\": %.0f,\n"
	       "    \"iops\": %f,\n"
	       "    \"bps\": %.0f,\n"
	       "    \"min\": %llu,\n"
	       "    \"avg\": %.0f,\n"
	       "    \"max\": %llu,\n"
	       "    \"mdev\": %.0f\n"
	       "  },\n"
	       "  \"load\": {\n"
	       "    \"count\": %llu,\n"
	       "    \"size\": %llu,\n"
	       "    \"time\": %llu,\n"
	       "    \"iops\": %f,\n"
	       "    \"bps\": %.0f\n"
	       "  }\n"
	       "}",
	       json_line++ ? "," : "",
	       timestamp_str,
	       localtime_str,
	       path,
	       fstype,
	       device,
	       device_size,
	       s->valid,
	       s->size,
	       s->sum,
	       s->iops,
	       s->speed,
	       s->min,
	       s->avg,
	       s->max,
	       s->mdev,
	       s->count,
	       s->load_size,
	       s->load_time,
	       s->load_iops,
	       s->load_speed);
}

int main (int argc, char **argv)
{
	ssize_t ret_size;
	struct stat st;
	int valid;
	int ret;

	struct statistics part, total;

	long long this_time;
	long long time_now, time_next, period_deadline;

	parse_options(argc, argv);

	setvbuf(stdout, NULL, _IOFBF, BUFSIZ);

	if (!size)
		size = default_size;

	if (size <= 0)
		errx(1, "request size must be greater than zero");

	if (speed_limit) {
		long long i = size * NSEC_PER_SEC / speed_limit;

		if (burst)
			i *= burst;
		if (i > interval)
			interval = i;
	}

	if (rate_limit) {
		long long i = NSEC_PER_SEC / rate_limit;

		if (burst)
			i *= burst;
		if (i > interval)
			interval = i;
	}

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
	if (!cached && !write_test) {
		cached = 1;
# if defined(HAVE_DIRECT_IO)
		if (!direct) {
			warnx("non-cached I/O not supported, will use direct I/O");
			direct = 1;
		}
# else
		warnx("non-cached I/O not supported by this platform");
		warnx("you can use write I/O to get reliable results");
# endif
	}
#endif

	if (async) {
		aio_setup();
	} else if (rw_flags) {
#ifdef HAVE_LINUX_PREADV2
		make_pread = do_preadv2;
		make_pwrite = do_pwritev2;
#else
		warnx("nowait/hipri I/O is not supported");
#endif
	}

	if ((rw_flags & RWF_NOWAIT) && !cached && !direct)
		warnx("nowait without cached or direct I/O is supposed to fail");

	make_request = write_test ? make_pwrite : make_pread;

#ifndef HAVE_DIRECT_IO
	if (direct)
		errx(1, "direct I/O not supported by this platform");
#endif

#ifndef HAVE_SYNC_IO
	if (syncio)
		errx(1, "sync I/O not supported by this platform");
#endif

#ifndef HAVE_DATA_SYNC_IO
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
	} else if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
		target_fd = open_file(path, NULL);
		if (target_fd < 0)
			err(2, "failed to open \"%s\"", path);

		if (get_device_size(target_fd, &st)) {
			if (!S_ISCHR(st.st_mode))
				err(2, "block get size ioctl failed");
			st.st_size = offset + temp_wsize;
			fstype = "character";
			device = "device";
		} else {
			device_size = st.st_size;
			fstype = "block";
			device = "device";
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
		target_fd = open_file(path, "ioping.tmp");
		if (target_fd < 0)
			err(2, "failed to create temporary file at \"%s\"", path);
		if (keep_file) {
			if (fstat(target_fd, &st))
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
			ret_size = pwrite(target_fd, buf, ret_size, offset + woffset);
			if (ret_size <= 0)
				err(2, "preparation write failed");
		}
skip_preparation:
		if (fsync(target_fd))
			err(2, "fsync failed");
	} else if (S_ISREG(st.st_mode)) {
		target_fd = open_file(path, NULL);
		if (target_fd < 0)
			err(2, "failed to open \"%s\"", path);
	}

	if (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))
		parse_device(st.st_dev);

	/* No readahead for non-cached I/O, we'll invalidate it anyway */
	if (randomize || !cached) {
#ifdef HAVE_POSIX_FADVICE
		ret = posix_fadvise(target_fd, offset, wsize, POSIX_FADV_RANDOM);
		if (ret)
			warn("fadvise(RANDOM) failed, "
			     "operations might perform unneeded readahead");
#endif
	}

	if (!cached) {
#ifdef HAVE_NOCACHE_IO
		ret = fcntl(target_fd, F_NOCACHE, 1);
		if (ret)
			err(2, "fcntl(F_NOCACHE) failed, "
			       "please retry with option -C");
#endif
	}

	set_signal();

	woffset = 0;

	time_now = now();

	start_statistics(&part, time_now);
	start_statistics(&total, time_now);

	if (json)
		printf("[");

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
			ret = posix_fadvise(target_fd, offset + woffset, size,
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

		ret_size = make_request(target_fd, buf, size, offset + woffset);

		if (ret_size < 0) {
			if ((rw_flags & RWF_NOWAIT) && errno == EAGAIN) {
				notice = "EAGAIN";
				ret_size = 0;
			} else if (errno != EINTR)
				err(3, "request failed");
		} else {
			if (ret_size < size)
				warnx("request returned less than expected: %zu", ret_size);
			else if (ret_size > size)
				errx(3, "request returned more than expected: %zu", ret_size);

			if (write_test && !cached)
				sync_file(target_fd);
		}

		time_now = now();

		if (!burst || ++burst_request == burst) {
		    burst_request = 0;
		    time_next += interval;
		}

		if ((time_now - time_next) > 0)
			time_next = time_now;

		this_time = time_now - this_time;

		timestamp_uptodate = 0;

		valid = ret_size ? add_statistics(&part, this_time) : false;

		if (quiet) {
			/* silence */
		} else if (json) {
			json_request(ret_size, this_time, valid);
		} else {
			if (time_info) {
				update_timestamp();
				printf("%s ", localtime_str);
			}
			print_size(ret_size);
			printf(" %s %s (%s %s ", write_test ? ">>>" : "<<<",
					path, fstype, device);
			print_size(device_size);
			printf("): request=%llu time=", request);
			print_time(this_time);
			if (notice)
			    printf(" (%s)", notice);
			if (burst && !burst_request)
			    printf("\n");
			printf("\n");
		}

		if ((period_request && (part.valid >= period_request)) ||
		    (period_time && (time_next >= period_deadline))) {
			finish_statistics(&part, time_now);
			if (json)
				json_statistics(&part);
			else
				dump_statistics(&part);
			fflush(stdout);
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

			if (!quiet)
			    fflush(stdout);

			nanosleep(&interval_ts, NULL);
		}
	}

	time_now = now();
	finish_statistics(&part, time_now);
	merge_statistics(&total, &part);
	finish_statistics(&total, time_now);

	if (json) {
		json_statistics(&total);
		printf("]\n");
		return 0;
	}

	if (batch_mode) {
		dump_statistics(&total);
		return 0;
	}

	if (quiet && (period_time || period_request))
		return 0;

	printf("\n--- %s (%s %s ", path, fstype, device);
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
