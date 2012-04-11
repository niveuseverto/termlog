/*-
 * Copyright (c) 2002-2005 Christian S.J. Peron
 * Copyright (c) 2002-2005 Seccuris Labs, Inc
 * All rights reserved.
 *
 * This software was developed for Seccuris Labs by Christian S.J. Peron.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/snoop.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/filio.h>
#include <sys/mman.h>

#include <utmp.h>
#include <signal.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>
#include <assert.h>
#include <syslog.h>

#include "termlog.h"
#include "fileops.h"
#include "rdwrlock.h"

struct rdwrlock q_lock;
TAILQ_HEAD(tailhead, snp_d) head = TAILQ_HEAD_INITIALIZER(head);

static char *ttylist[MAXTTYS];	/* user defined tty name lists */
static char *userlist[MAXUSERS];/* user defined user lists */
static char *thistty;		/* controlling tty */
static char *oflag;		/* plugin specific options */
static char *rootfs = "/";
				/* devfs mount point */
static int vflag;		/* verbose level */
static int nflag = 20;		/* maximum number of snp devices we will use */
static int iflag = 500000;	/* stat(2) interval of utmp in micro-secs */
static int q_serialno;		/* serial number for queue */
int isdaemon = 0;

#define	USRHDR	"USER"
#define	TTYHDR	"TTY"
#define	SNPHDR	"FD"
#define BYTHDR	"BYTES"
#define	HDRSIZE(x) (sizeof(x) - 1)

extern int maxfsize;
extern int appendonly;
static int usrwidth = HDRSIZE(USRHDR);
static int ttywidth = UT_LINESIZE;
static int fflag;

int
dolog(char const *const fmt, ...)
{
	va_list ap;

	if (!isdaemon) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		fputc('\n', stderr);
		va_end(ap);
	}
	va_start(ap, fmt);
	vsyslog(LOG_AUTH | LOG_NOTICE, fmt, ap);
	va_end(ap);
	return (0);
}

static int
build_snp_device(int minor)
{
	char devpath[MAXPATHLEN];
	dev_t dev;

	snprintf(devpath, sizeof(devpath) - 1,
	    "%s/snp%d", _PATH_DEV, minor);
	dev = makedev(_SNP_MAJOR, minor);
	if (mknod(devpath, S_IFCHR | 0600, dev) < 0)
		err(1, "mknod %s failed", devpath);
	DEBUG(vflag, "created device snp%d", minor);
	return (0);
}

static void
dumpstats(FILE *fp)
{
	struct snp_d *snp;

	assert(fp != NULL);
	fprintf(fp, "Current snoop sessions:\n"
	    "%-*.*s %-*.*s %s\n",
	    usrwidth, usrwidth, USRHDR,
	    ttywidth, ttywidth, TTYHDR, BYTHDR);
	rd_lock(&q_lock);
	TAILQ_FOREACH(snp, &head, glue)
		fprintf(fp, "%-*.*s %-*.*s %lu\n",
		    usrwidth, usrwidth, snp->s_username,
		    ttywidth, ttywidth, snp->s_line,
		    snp->s_bytes);
	rdwr_unlock(&q_lock);
	fclose(fp);
}

static void
catchusr(int sig __unused)
{
	char buf[MAXPATHLEN];
	FILE *fp;

	snprintf(buf, sizeof(buf) - 1,
	    "%s.%d", _PATH_TERMLOG_STATS, time(0));
	fp = fopen(buf, "w");
	if (fp == NULL) {
		warn("fopen failed");
		return;
	}
	dumpstats(fp);
}

int
skipcrtltty(struct utmp *utmp)
{
	if (vflag == 0)
		return (0);
	assert(utmp != NULL);
	if (strcmp(thistty, utmp->ut_line) == 0)
		return (1);
	return (0);
}

int
handlesnpio(struct snp_d *s)
{
	static int nalloc = 512;
	int error, nbytes;
	static char *ptr;

	assert(s != NULL);
	WRLOCK_ASSERT(M_OWNED, &q_lock);
	if (ioctl(s->s_fd, FIONREAD, &nbytes) < 0)
		err(1, "ioctl FIONREAD failed");
	switch (nbytes) {
	case SNP_OFLOW:
		DEBUG(vflag, "overflow on %s reconnecting line",
		    s->s_line);
		s->snp_overflow(s->s_meta);
		close(s->s_fd);
		s->s_fd = snpattach(s->s_line);
		if (s->s_fd > 0) {
			q_serialno++;
			break;
		}
		/* FALL THROUGH */
	case SNP_DETACH:
	case SNP_TTYCLOSE:
		DEBUG(vflag, "user %s disconnected line %s",
		    s->s_username, s->s_line);
		q_serialno++;
		TAILQ_REMOVE(&head, s, glue);
		close(s->s_fd);
		s->snp_close(s->s_meta);
		free(s);
		break;
	default:
		if (ptr == NULL) {
			DEBUG(vflag, "doing initial allocation of %d bytes",
			    nalloc);
			ptr = malloc(nalloc);
		} else if (nalloc < nbytes) {
			DEBUG(vflag, "adjusting input buffer to %d bytes",
			    nbytes);
			ptr = realloc(ptr, nbytes);
			nalloc = nbytes;
		}
		if (ptr == NULL)
			return (1);
		error = read(s->s_fd, ptr, nbytes);
		if (error < 0) {
			warn("read failed");
			return (1);
		}
		error = s->snp_write(s->s_meta, ptr, error);
		if (error)
			warn("write failed");
		s->s_bytes += nbytes;
	}
	return (0);
}

int
buildfdlist(fd_set *fdset)
{
	struct snp_d *snp;
	int maxfd;

	assert(fdset != NULL);
	DEBUG(vflag, "refreshing fd set\n");
	FD_ZERO(fdset);
	maxfd = 0;
	rd_lock(&q_lock);
	TAILQ_FOREACH(snp, &head, glue) {
		if (snp->s_fd > maxfd)
			maxfd = snp->s_fd;
		FD_SET(snp->s_fd, fdset);
	}
	rdwr_unlock(&q_lock);
	maxfd++;
	return (maxfd);
}

void *
eventloop(void *arg __unused)
{
	struct timeval tv = { 1, 0 };
	int serialno, error, maxfd;
	struct snp_d *snp, *snp2;
	fd_set a_fds, r_fds;

	serialno = maxfd = 0;
	for (;;) {
		if (serialno != q_serialno) {
			maxfd = buildfdlist(&a_fds);
			serialno = q_serialno;
		}
		r_fds = a_fds;
		error = select(maxfd, &r_fds, NULL, NULL, &tv);
		if (error < 0 && errno == EINTR) {
			continue;
		} else if (error < 0)
			err(1, "select failed");
		else if (error == 0) {
			continue;
		}
		/*
		 * Since its possible that handlesnpio will remove
		 * elements from the TAILQ, we should use TAILQ_FOREACH_SAFE
		 * instead of it's unsafe counter-part.
		 */
		wr_lock(&q_lock);
		TAILQ_FOREACH_SAFE(snp, &head, glue, snp2) 
			if (FD_ISSET(snp->s_fd, &r_fds))
				handlesnpio(snp);
		rdwr_unlock(&q_lock);
	}
}
				
void *
watchutmp(void *arg __unused)
{
	char path[MAXPATHLEN];
	struct stat sb;
	int fd;
	int mtime;

	snprintf(path, sizeof(path) - 1,
	    "%s%s", rootfs, _PATH_UTMP);
	mtime = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(1, "open utmp failed");
	for (;;) {
		if (fstat(fd, &sb) < 0)
			err(1, "fstat failed");
		if (sb.st_mtime != mtime) {
			processutmp(&sb);
			mtime = sb.st_mtime;
		}
		usleep(iflag);
	}
}

int
getsnpfd(char **snp, size_t len)
{
	struct stat sb;
	char *snppath;
	int unit, fd;

	assert(*snp != NULL || len != 0);
	snppath = *snp;
	for (unit = 0; unit < nflag; unit++) {
		snprintf(snppath, len - 1, "%ssnp%d",
		    _PATH_DEV, unit);
		if (fflag)
			if (stat(snppath, &sb) < 0 &&
			    errno == ENOENT)
				build_snp_device(unit);
		fd = open(snppath, O_RDONLY);
		if (fd < 0 && errno != EBUSY) {
			err(1, "open %s failed", snppath);
		} else if (fd < 0) {
			continue;
		} else
			return (fd);
	}
	return (-1);
}

int
snpattach(char *tty_line)
{
	char *ptr, snpdev[MAXPATHLEN], line[MAXPATHLEN];
	struct stat sb;
#if __FreeBSD_version > 600000
	int sdev;
#else
	dev_t sdev;
#endif
	int fd;

	assert(tty_line != NULL);
	ptr = &snpdev[0];
	snprintf(line, sizeof(line) - 1, "%s%s%s", rootfs, _PATH_DEV,
	    tty_line);
	if (stat(line, &sb) < 0) {
		warn("stat %s failed", tty_line);
		return (-1);
	}
	if ((sb.st_mode & S_IFMT) != S_IFCHR) {
		warn("%s not a device", tty_line);
		return (-1);
	}
	if ((fd = getsnpfd(&ptr, MAXPATHLEN)) < 0) {
		DEBUG(vflag,
		    "snp open failed: retrying on next login/logout event\n");
		return (-1);
	}
#if __FreeBSD_version > 600000
	sdev = open(line, O_RDONLY | O_NONBLOCK);
	if (sdev < 0) {
		warn("open failed");
		return (-1);
	}
#else
	sdev = sb.st_rdev;
#endif
	if (ioctl(fd, SNPSTTY, &sdev) < 0) {
		warn("ioctl SNPSTTY failed");
		close(fd);
		return (-1);
	}
#if __FreeBSD_version > 600000
	close(sdev);
#endif
	DEBUG(vflag, "%s fd %d %s attached to tty %s",
	    snpdev, fd, ptr, line);
	return (fd);
}

int
ttyislinked(struct utmp *utmp)
{
	struct snp_d *s;

	assert(utmp != NULL);
	rd_lock(&q_lock);
	TAILQ_FOREACH(s, &head, glue) {
		assert(s != NULL);
		if (strcmp(s->s_line, utmp->ut_line) == 0) {
			rdwr_unlock(&q_lock);
			return (1);
		}
	}
	rdwr_unlock(&q_lock);
	return (0);
}

int
linktty(struct utmp *utmp)
{
	struct snp_d *s;
	int len, fd;

	assert(utmp != NULL);
	fd = snpattach(utmp->ut_line);
	if (fd < 0)
		return (1);
	s = malloc(sizeof(struct snp_d));
	if (s == NULL)
		goto error;
	DEBUG(vflag, "building snoop session for user %s",
	    utmp->ut_name);
	if ((len = strlcpy(s->s_username, utmp->ut_name,
	    UT_NAMESIZE)) > usrwidth)
		usrwidth = len;
	strlcpy(s->s_line, utmp->ut_line, UT_LINESIZE);
	s->snp_write = snp_write_log;
	s->snp_setup = snp_setup;
	s->snp_close = snp_remove;
	s->snp_overflow = snp_overflow;
	s->s_fd = fd;
	s->s_meta = s->snp_setup(s, oflag);
	s->s_bytes = 0;
	wr_lock(&q_lock);
	TAILQ_INSERT_HEAD(&head, s, glue);
	q_serialno++;
	rdwr_unlock(&q_lock);
	return (0);
error:
	if (s != NULL)
		free(s);
	return (1);
}

int
ttystat(char *line, int size)
{
	struct stat sb;
	char ttybuf[MAXPATHLEN];

	assert(line != NULL || size != 0);
	snprintf(ttybuf, sizeof(ttybuf), "%s%s%.*s", rootfs,
	    _PATH_DEV, size, line);
	if (stat(ttybuf, &sb) == 0)
		return (1);
	else
		return (0);
}

int
checkuserlist(struct utmp *utmp)
{
	char *user;
	char **ulist;

	assert(utmp != NULL);
	ulist = userlist;
	if (*ulist == NULL)
		return (1);
	while ((user = *ulist++))
		if (strcmp(utmp->ut_name, user) == 0)
			return (1);
	return (0);
}

int
checkttylist(struct utmp *utmp)
{
	char *tty;
	char **tlist;

	assert(utmp != NULL);
	tlist = ttylist;
	if (*tlist == NULL)
		return (1);
	while ((tty = *tlist++))
		if (strcmp(utmp->ut_line, tty) == 0)
			return (1);
	return (0);
}

/*
 * Rather than calling fread() on every element of the utmp array
 * stored in a file, it makes more sense to mmap(2) the file
 * and use the mmaping instead. In benchmarks I have done, it
 * was concluded that the array element access through the mmap
 * was close to twice as fast.
 */
int
processutmp(struct stat *sb)
{
	static int fd, n, usize;
	static caddr_t memmap;
	struct utmp *utmp, *up;
	int i;
	char path[MAXPATHLEN];

	if (fd == 0) {
		snprintf(path, sizeof(path) - 1,
		    "%s%s", rootfs, _PATH_UTMP);
		if ((fd = open(path, O_RDONLY)) < 0)
			err(1, "%s", path);
	}
	if (usize != sb->st_size) {
		DEBUG(vflag,
		    "creating memory mmap of utmp file %qu bytes %d bytes per record",
		    sb->st_size, sizeof(struct utmp));
		if ((sb->st_size % sizeof(struct utmp)) != 0)
			errx(1, "mutilated record in utmp database");
		if (memmap != NULL)
			if (munmap(memmap, usize) < 0)
				err(1, "munmap failed");
		if ((memmap = mmap(0, sb->st_size, PROT_READ, MAP_FILE |
		    MAP_SHARED, fd, 0)) < 0)
			err(1, "mmap failed");
		usize = sb->st_size;
		n = sb->st_size / sizeof(struct utmp);
	}
	utmp = (struct utmp *)memmap;
	for (i = 0; i < n; i++) {
		up = &utmp[i];
		if (*up->ut_name == '\0' || skipcrtltty(up) ||
		    !ttystat(up->ut_line, UT_LINESIZE) ||
		    !checkttylist(up) || !checkuserlist(up) || ttyislinked(up))
			continue;
		if (linktty(up))
			DEBUG(vflag, "unable to link %s", up->ut_line);
	}
	return (0);
}

static int
strtoval(char *string, int flag)
{
	char *endp;
	int val;

	/* XXX range checking ? */
	val = strtoul(string, &endp, 0);
	if (*endp != '\0')
		if (!flag)
			err(1, "%s: invalid number", string);
	return (val);
}

int
main(int argc, char *argv [])
{
	int ch;
	char **tlist, **ulist;
	pthread_t thr[2];

	tlist = ttylist;
	ulist = userlist;
	while ((ch = getopt(argc, argv, "aC:c:d:Dfi:o:n:t:u:v")) != -1)
		switch (ch) {
		case 'a':
			appendonly++;
			break;
		case 'C':
			if (chdir(optarg) < 0)
				err(1, "chdir failed");
			break;
		case 'c':
			maxfsize = strtoval(optarg, 0);
			break;
		case 'd':
			rootfs = optarg;
			break;
		case 'D':
			isdaemon++;
			break;
		case 'f':
			fflag++;
			break;
		case 'i':
			iflag = strtoval(optarg, 0);
			break;
		case 'o':
			oflag = optarg;
			break;
		case 'n':
			nflag = strtoval(optarg, 0);
			break;
		case 't':
			if (tlist == &ttylist[MAXTTYS]) {
				warnx("ignoring tty %s: max tty list exceeded",
				    optarg);
				break;
			}
			*tlist++ = optarg;
			break;
		case 'u':
			if (ulist == &userlist[MAXUSERS]) {
				warnx("ignoring user %s: max userlist exceeded",
				    optarg);
				break;
			}
			*ulist++ = optarg;
			break;
		case 'v':
			vflag++;
			DEBUG(vflag, "termlog %s",
			     TERMLOG_VERSION);
			break;
		case '?':
		default:
			usage(argv[0]);
		}
	if (modfind("snp") == -1)
		if (kldload("snp") == -1 || modfind("snp") == -1)
			err(1, "snp module not available");
	thistty = ttyname(0);
	if (thistty) {
		thistty = rindex(thistty, '/');
		if (thistty)
			thistty++;
	}
	openlog("termlog", LOG_PID | LOG_NDELAY, LOG_AUTH);
#ifdef DEBUGGING
	fprintf(stderr, "NOTE: debugging and assertions are enabled\n");
#endif
	signal(SIGUSR1, catchusr);
	rdwr_lock_init(&q_lock);
	TAILQ_INIT(&head);
	if (pthread_create(&thr[0], NULL, watchutmp, NULL))
		err(1, "pthread_create failed");
	if (pthread_create(&thr[1], NULL, eventloop, NULL))
		err(1, "pthread_create failed");
	for (;;)
		pause();
}

void
usage(char *execname)
{
	fprintf(stderr,
	    "usage: %s [-fv] [-C dir] [-c count] [-i interval] [-n max devs]\n"
	    "               [-u username] [-t tty]\n",
	    execname);
	exit(1);
}
