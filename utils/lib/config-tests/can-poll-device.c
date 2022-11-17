#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

    struct pollfd fds[1];

    fds[0].fd = sfd;
    fds[0].events = POLLIN;

    int poll_r = poll (fds, 1, 1);

    if (fds[0].revents & POLLNVAL) {
	exit (1);
    }

    close (sfd);
    close (mfd);

    return 0;
}
