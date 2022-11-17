#define _GNU_SOURCE
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#if defined (__sun) && defined (__SVR4)
#include <stropts.h>
#endif


int main (int argc, char **argv)
{
    int mfd = posix_openpt (O_RDWR | O_NOCTTY);

    if (-1 == mfd) {
	perror ("posix_openpt");
	exit (1);
    }

    if (grantpt (mfd) == -1) {
	perror ("grantpt");
	exit (1);
    }

    if (unlockpt (mfd) == -1) {
	perror ("unlockpt");
	exit (1);
    }

    char *sn = ptsname (mfd);

    if (NULL == sn) {
	perror ("ptsname");
	exit (1);
    }

    int sfd = open (sn, O_RDWR | O_NOCTTY);

    if (-1 == sfd) {
	perror ("open");
	exit (1);
    }

#if defined (__sun) && defined (__SVR4)
    if (ioctl (sfd, I_PUSH, "ptem") == -1) {
	perror ("ioctl (I_PUSH, ptem)");
	exit (1);
    }

    if (ioctl (sfd, I_PUSH, "ldterm") == -1) {
	perror ("ioctl (I_PUSH, ldterm)");
	exit (1);
    }
#endif

    pid_t pid = fork ();

    if (-1 == pid) {
	perror ("fork");
	exit (1);
    }

    if (0 == pid) {
	close (mfd);

	setsid ();

	dup2 (sfd, STDIN_FILENO);
	dup2 (sfd, STDOUT_FILENO);
	dup2 (sfd, STDERR_FILENO);

	if (sfd > STDERR_FILENO) {
	    close (sfd);
	}

	/* tty -> controlling tty */
	int ctty = open (sn, O_RDWR);

	if (-1 == ctty) {
	    /* "open (sn, O_RDWR)" */
	    size_t emlen = 6 + strlen (sn) + 9 + 1;
	    char em[emlen];
	    snprintf (em, emlen, "open (%s, O_RDWR)", sn);
	    perror (em);
	    exit (1);
	}
	close (ctty);

	char cmdstr[BUFSIZ];
	snprintf (cmdstr, BUFSIZ, "hello from %" PRIdMAX, (intmax_t) getpid ());

	execl ("/bin/echo", "/bin/echo", cmdstr, (char *) NULL);

	perror ("execl /bin/echo");
	exit (1);
    } else {
	close (sfd);

	/*
	 * exit (0) and only exit (0) is satisfactory
	 *
	 * XXX OpenBSD/NetBSD both get a 5s timeout here.
	 */
	int wstatus;
	pid_t waitpid_r = waitpid (pid, &wstatus, 0);

	if (-1 == waitpid_r) {
	    perror ("waitpid");
	    exit (1);
	}

	if (! WIFEXITED (wstatus)) {
	    fprintf (stderr, "/bin/echo: did not exit (killed/signalled/stopped)\n");
	    exit (1);
	}

	if (0 != WEXITSTATUS (wstatus)) {
	    fprintf (stderr, "/bin/echo: exit (%d)\n", WEXITSTATUS (wstatus));
	    exit (1);
	}

	/*
	 * Finally, we get to see if a process that output something
	 * (POLLIN) and has exited (POLLHUP) generates POLLIN|POLLHUP
	 * or just POLLHUP.
	 */
	struct pollfd fds[1];
	fds[0].fd = mfd;
	fds[0].events = POLLIN;

	/*
	 * 1ms timeout as the POLLIN|POLLHUP should be there as the
	 * process has been reaped.
	 */
	int poll_r = poll (fds, 1, 1);

	if (-1 == poll_r) {
	    perror ("poll mfd");
	    exit (1);
	}

	if (0 == poll_r) {
	    fprintf (stderr, "poll mfd: timeout\n");
	    exit (1);
	}

	if (fds[0].revents & POLLHUP) {
	    if (0 == (fds[0].revents & POLLIN)) {
		exit (0);
	    }
	}
    }

    close (sfd);
    close (mfd);

    return 1;
}
