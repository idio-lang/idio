/*
 * This file contains nominal applications of each standard library
 * API we want to be able to use in Idio in order that we can
 * post-process the .o and determine the set of typedefs and structs
 * we need to support through libc.
 *
 * That immediately raises the question as to what *is* the set of
 * APIs that we want to have in libc?
 *
 * There are various considerations about POSIX, SUS (Single Unix
 * Specification) and the ANSI C library definitions of the set of
 * APIs.  We can also look to which APIs similar languages support --
 * the reasoning being that they've had a few decades to figure out
 * which APIs are required: Perl's POSIX module, Python's posix module
 * (though you are directed to the os module in preference), etc..
 *
 * Then you get to worry about whether or not any particular platform
 * supports whatever set of APIs you do want to use.
 *
 * There is an edge towards a generally portable set of APIs -- noting
 * some more elderly platforms in the mix -- but also that you want to
 * be supporting a modern set of APIs which clearly excludes those
 * more long in the tooth.
 *
 * All of this requires some conditional testing for which we have the
 * autoconf-lite, utils/bin/gen-idio-config, which creates
 * src/idio-config.h.
 *
 *
 * The Minimal Viable Product is that which is required for Idio to
 * run.  Using "to run the test suite" has a rolling scope as each
 * interface over and above the MVP will have at least as many "error"
 * tests as there are arguments to the interface (for validating
 * types) and possibly more validation tests for values.
 *
 * Furthermore, the corollary is that, in all probability, once an
 * interface (over and above the MVP) has been added then it is at
 * risk of being used in the core Idio tree and thus becomes part of
 * the MVP.
 *
 *
 * Each nominal snippet wants to use portable typedefs and where a
 * struct is involved to use portable typedef'd access to elements of
 * the struct.
 *
 * If you don't use a dev_t, say, to access the st_dev member of a
 * struct stat then the typedef will not appear in the .o and Idio
 * won't get a definition for it.
 *
 *
 * Not all platforms produce the same DWARF output.  This is generally
 * run on a Fedora system which produces almost everything we need.
 * It doesn't produce API argument names, which is slightly annoying
 * but not surprising as this is the DWARF output for this file, not
 * the DWARF output for the standard library.  It does produce the
 * interfaces (subprograms in DWARF-speak) in a reversed order.  That
 * might be important (to DWARF) or just an artifact.  Whatever the
 * cause, subprograms (and structs) are sorted by name and generated
 * lexicographically.
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "idio-config.h"

/*
 * Solaris and CentOS 6  don't typedef suseconds_t
 *
 * re-typedef'ing something to the same thing shouldn't be a problem
 */
#ifdef __suseconds_t_defined
typedef __suseconds_t seconds_t;
#else
#if defined (__APPLE__) && defined (__MACH__) && defined (_SUSECONDS_T)
/*
 * Darwin 9.8.0 /usr/include/sys/types.h:250
 * typedef __darwin_suseconds_t    suseconds_t;
 *
 * if we re-typedef it here then gcc gets in a mess and doesn't
 * generate the suseconds_t -> __darwin_suseconds_t typedef in the
 * DWARF output
 *
 * Darwin 19.6.0 (Mac OS X 10.15) didn't mind.  So why did I
 * re-typedef it?
 */
#elif defined (_SUSECONDS_T_DECLARED)
typedef        __suseconds_t   suseconds_t;
#elif defined (__NetBSD__)
/* NetBSD does typedef suseconds_t but does not #define __suseconds_t_defined */
#else

/*
 * OpenBSD doesn't flag the definition in any way
 */
#if ! defined (__OpenBSD__)
typedef long suseconds_t;
#endif

#endif
#endif

#if defined (__NetBSD__)
/*
 * In both sys/types.h and sys/statvfs.h, NetBSD 9 says:

#ifndef	fsblkcnt_t
typedef	__fsblkcnt_t	fsblkcnt_t;
#define	fsblkcnt_t	__fsblkcnt_t
#endif

#ifndef	fsfilcnt_t
typedef	__fsfilcnt_t	fsfilcnt_t;
#define	fsfilcnt_t	__fsfilcnt_t
#endif

 * which 1) creates the typedef we want but 2) creates a pre-processor
 * macro which will replace, say, fsblkcnt_t with __fsblkcnt_t.
 *
 * If you were just compiling on NetBSD you wouldn't see much effect
 * but what cpp does is replace, say:

fsblkcnt_t blocks     = buf.f_blocks;

 * with:

__fsblkcnt_t blocks     = buf.f_blocks;

 * and we don't get to see the fsblkcnt_t typedef being used and
 * therefore we won't generate the various accessors and predicates.
 *
 * This means that when src/libc-api.c tries to use the expected
 * portable accessor, say, idio_libc_fsblkcnt_t (statvfsp->f_blocks),
 * then on NetBSD it complains that there is no such function.
 *
 * So, and I don't like this but, we #undef the macro.
 *
 * Other systems evidently have a similar problem and, for example,
 * Fedora uses a __fsblkcnt_t_defined macro rather than the typedef
 * name.
 */
#ifdef	fsblkcnt_t
#undef	fsblkcnt_t
#endif
#ifdef	fsfilcnt_t
#undef	fsfilcnt_t
#endif
#endif

int main (int argc, char **argv)
{
    /* access(2) */
    {
	char *pathname = ".";
	int mode = R_OK;
	int access_r = access (pathname, mode);
    }

    /* asctime(3) */
    {
	time_t t = time (NULL);
	struct tm *tmp = localtime (&t);
	char *asctime_r = asctime (tmp);
    }

    /* chdir(2) */
    {
	char *path = ".";
	int chdir_r = chdir (path);
    }

    /* chmod(2) */
    {
	char *pathname = ".";
	mode_t mode = S_IRWXU;
	int chmod_r = chmod (pathname, mode);
    }

    /* chown(2) */
    {
	char *pathname = ".";
	uid_t owner = 0;
	gid_t group = 0;
	int chown_r = chown (pathname, owner, group);
    }

    /* chroot(2) */
    {
	char *path = ".";
	int chroot_r = chroot (path);
    }

    /* close(2) */
    {
	int fd = STDIN_FILENO;
	int close_r = close (fd);
    }

    /* ctermid(3) */
    {
	/*
	 * "The symbolic constant L_ctermid is the maximum number of
	 * characters in the returned pathname."
	 *
	 * So, including NUL or not?
	 */
	char s[L_ctermid + 1];
	char *ctermid_r = ctermid (s);
    }

    /* ctime(3) */
    {
	time_t t = time (NULL);
	char *ctime_r = ctime (&t);
    }

    /* dup(2) */
    {
	int oldfd = STDIN_FILENO;
	int dup_r = dup (oldfd);
    }

    /* dup2(2) */
    {
	int oldfd = STDIN_FILENO;
	int newfd = 10;
	int dup2_r = dup2 (oldfd, newfd);
    }

    /* exit(3) */
    {
	int status = EXIT_SUCCESS;
	/*
	 * XXX The presence of exit() stops the debuggger generating
	 * subprogram tags...
	 
	  exit (status);

	*/
    }

    /* fchdir(2) */
    {
	int fd = STDIN_FILENO;
	int fchdir_r = fchdir (fd);
    }

    /* fchmod(2) */
    {
	int fd = STDIN_FILENO;
	mode_t mode = S_IRWXU;
	int fchmod_r = fchmod (fd, mode);
    }

    /* fchown(2) */
    {
	int fd = STDIN_FILENO;
	uid_t owner = 0;
	gid_t group = 0;
	int fchown_r = fchown (fd, owner, group);
    }

    /* fcntl(2) */
    {
	int fd = STDIN_FILENO;
	int fcntl_r = fcntl (fd, F_GETFD);
    }

    /* fork(2) */
    {
	pid_t fork_r = fork ();
    }

    /* fstat(2) */
    {
	struct stat statbuf;
	int stat_r = fstat (0, &statbuf);

	/*
	 * struct stat is expanded in stat(2)
	 */
    }

    /* fstatvfs(3) */
    {
	struct statvfs buf;
	int stat_r = fstatvfs (0, &buf);

	/*
	 * struct statvfs is expanded in statvfs(3)
	 */
    }

    /* fsync(2) */
    {
	int fd = STDIN_FILENO;
	int fsync_r = fsync (fd);
    }

    /* ftruncate(2) */
    {
	int fd = STDIN_FILENO;
	off_t length = 0;
	int ftruncate_r = ftruncate (fd, length);
    }

    /* getcwd(3) */
    {
	char buf[PATH_MAX];
	char *getcwd_r = getcwd (buf, PATH_MAX);
    }

    /* getegid(2) */
    {
	gid_t getegid_r = getegid ();
    }

    /* geteuid(2) */
    {
	uid_t geteuid_r = geteuid ();
    }

    /* getgid(2) */
    {
	gid_t getgid_r = getgid ();
    }

    /* getgrnam(3) */
    {
	struct group *getgrnam_r = getgrnam ("root");
    }

    /* getgrgid(3) */
    {
	struct group *getgrgid_r = getgrgid (0);
    }

    /* getlogin(3) */
    {
	char *getlogin_r = getlogin ();
    }

    /* getpgid(2) */
    {
	pid_t pgid = getpgid (0);
    }

    /* getpgrp(2) */
    {
	pid_t getpgrp_r = getpgrp ();
    }

    /* getpid(2) */
    {
	pid_t getpid_r = getpid ();
    }

    /* getppid(2) */
    {
	pid_t getppid_r = getppid ();
    }

    /* getpriority(2) */
    {
	int which = PRIO_PROCESS;
	id_t who = 1;
	int getpriority_r = getpriority (which, who);
    }

    /* getpwnam(3) */
    {
	struct passwd *getpwnam_r = getpwnam ("root");
    }

    /* getpwuid(3) */
    {
	struct passwd *getpwuid_r = getpwuid (0);
    }

    /* getrlimit(2) */
    {
	struct rlimit rlim;
	int getrlimit_r = getrlimit (RLIMIT_NOFILE, &rlim);
    }

    /* getrusage(2) */
    {
	struct rusage usage;
	int getrusage_r = getrusage (RUSAGE_SELF, &usage);
    }

    /* getsid(2) */
    {
	pid_t pid = getpid ();
	pid_t getsid_r = getsid (pid);
    }

    /* gettimeofday(2) */
    {
	struct timeval tv;
	int gettimeofday_r = gettimeofday (&tv, NULL);
	time_t sec = tv.tv_sec;
	suseconds_t usec = tv.tv_usec;
    }

    /* getuid(2) */
    {
	uid_t getuid_r = getuid ();
    }

    /* gmtime(3) */
    {
	time_t t = time (NULL);
	struct tm *gmtime_r = gmtime (&t);
    }

    /* grantpt(3) */
    {
	int grantpt_r = grantpt (0);
    }

    /* ioctl(2) */
    {
	/*
	 * Arguments to ioctl(2) are platform-specific so we can't be
	 * very portable, here.  However, we're not trying to run
	 * anything but merely trying to generate a .o we can trawl
	 * for APIs.
	 */
	int ioctl_r = ioctl (0, 0);
    }

    /* isatty(3) */
    {
	int fd = STDIN_FILENO;
	int isatty_r = isatty (fd);
    }

    /* kill(2) */
    {
	pid_t pid = getpid ();
	int sig = SIGINT;
	int kill_r = kill (pid, sig);
    }

    /* killpg(2) */
    {
	pid_t pgrp = getpgid (0);
	int sig = SIGINT;
	int killpg_r = killpg ((int) pgrp, sig);
    }

    /* link(2) */
    {
	char *oldpath = "old";
	char *newpath = "new";
	int link_r = link (oldpath, newpath);
    }

    /* localtime(3) */
    {
	time_t t = time (NULL);
	struct tm *tmp = localtime (&t);
    }

    /* lockf(3) */
    {
	int fd = STDIN_FILENO;
	int cmd = F_LOCK;
	off_t len = 0;
	int lockf_r = lockf (fd, cmd, len);
    }

    /* lstat(2) */
    {
	char *pathname = ".";
	struct stat statbuf;
	int stat_r = lstat (pathname, &statbuf);

	/*
	 * struct stat is expanded in stat(2)
	 */
    }

    /* mkdir(2) */
    {
	char *pathname = ".";
	mode_t mode = S_IRWXU;
	int mkdir_r = mkdir (pathname, mode);
    }

    /* mkdtemp(3) */
    {
	char *template = "XXXXXX";
	char *mkdtemp_r = mkdtemp (template);
    }

    /* mkfifo(3) */
    {
	char *path = "idio-np";
	mode_t mode = S_IRWXU;
	int mkfifo_r = mkfifo (path, mode);
    }

    /* mkstemp(3) */
    {
	char *template = "XXXXXX";
	int mkstemp_r = mkstemp (template);
    }

    /* mktime(3) */
    {
	time_t t = time (NULL);
	struct tm *tmp = localtime (&t);
	time_t mktime_r = mktime (tmp);
    }

    /* nanosleep(2) */
    {
	struct timespec *req = NULL;
	struct timespec *rem = NULL;
	int nanosleep_r = nanosleep (req, rem);
    }

    /* open(2) */
    {
	int open_r = open ("/dev/tty", O_RDONLY);
    }

    /* pipe(2) */
    {
	int pipefd[2];
	int pipe_r = pipe (pipefd);
    }

    /* poll(2) */
    {
	nfds_t nfds = 2;
	struct pollfd fds[nfds];
    }

    /* posix_openpt(3) */
    {
	int posix_openpt_r = posix_openpt (O_RDWR);
    }

    /* pread(2) */
    {
	int fd = STDIN_FILENO;
	char buf[BUFSIZ];
	size_t count = BUFSIZ;
	off_t offset = 0;
	ssize_t pread_r = pread (fd, buf, count, offset);
    }

    /* ptsname(3) */
    {
	char *ptsname_r = ptsname (0);
    }

    /* pwrite(2) */
    {
	int fd = STDIN_FILENO;
	char buf[BUFSIZ];
	size_t count = BUFSIZ;
	off_t offset = 0;
	ssize_t pwrite_r = pwrite (fd, buf, count, offset);
    }

    /* read(2) */
    {
	int fd = STDIN_FILENO;
	char buf[BUFSIZ];
	size_t count = BUFSIZ;
	ssize_t read_r = read (fd, buf, count);
    }

    /* readlink(2) */
    {
	char *pathname = ".";
	char buf[BUFSIZ];
	size_t bufsiz = BUFSIZ;
	ssize_t readlink_r = readlink (pathname, buf, bufsiz);
    }

    /* rename(2) */
    {
	char *oldpath = "old";
	char *newpath = "new";
	int rename_r = rename (oldpath, newpath);
    }

    /* rmdir(2) */
    {
	char *pathname = ".";
	int rmdir_r = rmdir (pathname);
    }

    /* setegid(2) */
    {
	gid_t egid = 0;
	int setegid_r = setegid (egid);
    }

    /* seteuid(2) */
    {
	uid_t euid = 0;
	int seteuid_r = seteuid (euid);
    }

    /* setgid(2) */
    {
	gid_t gid = 0;
	int setgid_r = setgid (gid);
    }

    /* setpgid(2) */
    {
	pid_t pid = getpid ();
	pid_t pgid = getpgid (pid);
	int setpgid_r = setpgid (pid, pgid);
    }

    /* setrlimit(2) */
    {
	struct rlimit rlim;
	int setrlimit_r = setrlimit (RLIMIT_NOFILE, &rlim);
    }

    /* setpriority(2) */
    {
	int which = PRIO_PROCESS;
	id_t who = 1;
	int prio = 0;
	int setpriority_r = setpriority (which, who, prio);
    }

    /* setregid(2) */
    {
	gid_t rgid = 0;
	gid_t egid = 0;
	int setregid_r = setregid (rgid, egid);
    }

#ifdef IDIO_HAVE_SET_SAVED_IDS
    /* setresgid(2) */
    {
	gid_t rgid = 0;
	gid_t egid = 0;
	gid_t sgid = 0;
	int setresgid_r = setresgid (rgid, egid, sgid);
    }

    /* setresuid(2) */
    {
	uid_t ruid = 0;
	uid_t euid = 0;
	uid_t suid = 0;
	int setresuid_r = setresuid (ruid, euid, suid);
    }
#endif

    /* setreuid(2) */
    {
	uid_t ruid = 0;
	uid_t euid = 0;
	int setreuid_r = setreuid (ruid, euid);
    }

    /* setsid(2) */
    {
	pid_t setsid_r = setsid ();
    }

    /* setuid(2) */
    {
	uid_t uid = 0;
	int setuid_r = setuid (uid);
    }

    /* signal(2) */
    {
	/*
	 * Linux (pre-)defines sighandler_t whereas FreeBSD
	 * (pre-)defines sig_t.
	 *
	 * In either case the nominal type is:
	 */
	void (*signal_r) (int) = signal (SIGINT, SIG_IGN);
    }

    /* sigaction(2) */
    {
	struct sigaction act;
	struct sigaction oldact;
	int sigaction_r = sigaction (SIGINT, &act, &oldact);
    }

    /* sleep(3) */
    {
	unsigned int seconds = 1;
	unsigned int sleep_r = sleep (seconds);
    }

    /* stat(2) */
    {
	char *pathname = ".";
	struct stat statbuf;
	int stat_r = stat (pathname, &statbuf);

	/*
	 * Fedora uses __dev_t in struct stat requiring us to provoke
	 * the use of dev_t, etc..
	 */
	dev_t dev		= statbuf.st_dev;
	ino_t ino		= statbuf.st_ino;
	nlink_t nlink		= statbuf.st_nlink;
	mode_t mode		= statbuf.st_mode;
	uid_t uid		= statbuf.st_uid;
	gid_t gid		= statbuf.st_gid;
	off_t off		= statbuf.st_size;
	blksize_t blksize	= statbuf.st_blksize;
	blkcnt_t blkcnt		= statbuf.st_blocks;

	/* time_t */
	time_t t = statbuf.st_mtime;
    }

    /* statvfs(3) */
    {
	char *pathname = ".";
	struct statvfs buf;
	int statvfs_r = statvfs (pathname, &buf);

	unsigned long bsize   = buf.f_bsize;
	unsigned long frsize  = buf.f_frsize;
	fsblkcnt_t blocks     = buf.f_blocks;
	fsblkcnt_t bfree      = buf.f_bfree;
	fsblkcnt_t bavail     = buf.f_bavail;
	fsfilcnt_t files      = buf.f_files;
	fsfilcnt_t ffree      = buf.f_ffree;
	fsfilcnt_t favail     = buf.f_favail;
	unsigned long fsid    = buf.f_fsid;
	unsigned long flag    = buf.f_flag;
	unsigned long namemax = buf.f_namemax;
    }

    /* strerror(3) */
    {
	char *strerror_r = strerror (EBADF);
    }

    /* strftime(3) */
    {
	char s[BUFSIZ];
	char *format = "%c";
	time_t t = time (NULL);
	struct tm *tmp = localtime (&t);
	size_t strftime_r = strftime (s, BUFSIZ, format, tmp);
    }

    /* strptime(3) */
    {
	char *s = "1999";
	char *format = "%Y";
	struct tm tm;
	char *strptime_r = strptime (s, format, &tm);
    }

    /* strsignal(3) */
    {
	char *strsignal_r = strsignal (SIGINT);
    }

    /* symlink(2) */
    {
	char *target = "target";
	char *linkpath = "link";
	int symlink_r = symlink (target, linkpath);
    }

    /* sync(2) */
    {
	sync ();
    }

    /* tcgetattr(3) */
    {
	int fd = STDIN_FILENO;
	struct termios t;
	int tcgetattr_r = tcgetattr (fd, &t);

	/*
	 * OpenBSD 6.9:
	 *
	 *   typedef unsigned int speed_t;
	 *
	 *   struct termios {
	 *     ...
	 *     int c_ispeed;
	 *     int c_ospeed;
	 *   };
	 *
	 *
	 * which doesn't add up but also doesn't force the use of
	 * speed_t which we use with idio_libc_speed_t in
	 * src/libc-api.c.
	 */
#if defined (IDIO_HAVE_TERMIOS_SPEEDS)
	speed_t ospeed = (speed_t) t.c_ospeed;
#endif
    }

    /* tcgetpgrp(3) */
    {
	int fd = STDIN_FILENO;
	pid_t tcgetpgrp_r = tcgetpgrp (fd);
    }

    /* tcsetattr(3) */
    {
	int fd = STDIN_FILENO;
	struct termios t;
	int tcsetattr_r = tcsetattr (fd, TCSADRAIN, &t);
    }

    /* tcsetpgrp(3) */
    {
	int fd = STDIN_FILENO;
	pid_t pgrp = getpgrp ();
	pid_t tcsetpgrp_r = tcsetpgrp (fd, pgrp);
    }

    /* time(2) */
    {
	time_t t = time (NULL);
    }

    /* times(3) */
    {
	struct tms buffer;
	clock_t times_r = times (&buffer);
    }

    /* truncate(2) */
    {
	char *path = ".";
	off_t length = 0;
	int truncate_r = truncate (path, length);
    }

    /* uname(3) */
    {
	struct utsname name;
	int uname_r = uname (&name);
    }

    /* unlink(2) */
    {
	char *pathname = ".";
	int unlink_r = unlink (pathname);
    }

    /* unlockpt(3) */
    {
	int unlockpt_r = unlockpt (0);
    }

    /* waitpid(2) */
    {
	pid_t pid = getpid ();
	int wstatus;
	pid_t waitpid_r = waitpid (pid, &wstatus, WNOHANG);
    }

    /* write(2) */
    {
	int fd = STDIN_FILENO;
	char buf[BUFSIZ];
	size_t count = BUFSIZ;
	ssize_t write_r = write (fd, buf, count);
    }

    /* number types */
    intmax_t intmax = INTMAX_MAX;
    uintmax_t uintmax = UINTMAX_MAX;

    intptr_t intptr = INTPTR_MAX;
}
