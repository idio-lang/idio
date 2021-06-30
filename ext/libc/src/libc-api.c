#define _GNU_SOURCE

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ffi.h>
#include <glob.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <strings.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>

/*
 * Solaris and CentOS 6  don't typedef suseconds_t
 *
 * re-typedef'ing something to the same thing shouldn't be a problem
 */
#ifdef __suseconds_t_defined
typedef __suseconds_t seconds_t;
#else
#if defined (__APPLE__) && defined (__MACH__) && defined (_SUSECONDS_T)
typedef __darwin_suseconds_t    suseconds_t;
#elif defined (_SUSECONDS_T_DECLARED)
typedef        __suseconds_t   suseconds_t;
#else
typedef long suseconds_t;
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

    /* chdir(2) */
    {
	char *path = ".";
	int chdir_r = chdir (path);
    }

    /* close(2) */
    {
	int fd = STDIN_FILENO;
	int close_r = close (fd);
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
	 * XXX The presence of exit() stops the debuggger generating subprogram tags...
	 
	  exit (status);

	*/
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

    /* getcwd(3) */
    {
	char *getcwd_r = getcwd (NULL, PATH_MAX);
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

    /* pipe(2) */
    {
	int pipefd[2];
	int pipe_r = pipe (pipefd);
    }

    /* read(2) */
    {
	int fd = STDIN_FILENO;
	char buf[BUFSIZ];
	size_t count = BUFSIZ;
	ssize_t read_r = read (fd, buf, count);
    }

    /* rmdir(2) */
    {
	char *pathname = ".";
	int rmdir_r = rmdir (pathname);
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
	 * the use of dev_t.
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

    /* strerror(3) */
    {
	char *strerror_r = strerror (EBADF);
    }

    /* strsignal(3) */
    {
	char *strsignal_r = strsignal (SIGINT);
    }

    /* tcgetattr(3) */
    {
	int fd = STDIN_FILENO;
	struct termios termios_p;
	int tcgetattr_r = tcgetattr (fd, &termios_p);
    }

    /* tcgetpgrp(3) */
    {
	int fd = STDIN_FILENO;
	pid_t tcgetpgrp_r = tcgetpgrp (fd);
    }

    /* tcsetattr(3) */
    {
	int fd = STDIN_FILENO;
	struct termios termios_p;
	int tcsetattr_r = tcsetattr (fd, TCSADRAIN, &termios_p);
    }

    /* tcsetpgrp(3) */
    {
	int fd = STDIN_FILENO;
	pid_t pgrp = getpgrp ();
	pid_t tcsetpgrp_r = tcsetpgrp (fd, pgrp);
    }

    /* times(3) */
    {
	struct tms buffer;
	clock_t times_r = times (&buffer);
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
