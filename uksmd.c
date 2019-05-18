/*
 * uksmd - userspace KSM helper daemon
 * Copyright (C) 2019 Oleksandr Natalenko <oleksandr@natalenko.name>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <fcntl.h>
#include <proc/readproc.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KSM_RUN		"/sys/kernel/mm/ksm/run"
#define KSM_ADVISE		"/proc/%d/ksm"
#define OBSERVE_WINDOW_SECS	10
#define IDLE_SLEEP_SECS	5

static int ksm_ctl(bool _enable)
{
	int ret = 0;

	int fd = open(KSM_RUN, O_WRONLY);
	if (fd == -1)
	{
		ret = errno;
		goto out;
	}

	if (write(fd, _enable ? "1" : "0", 1) == -1)
	{
		ret = errno;
		goto close_fd;
	}

close_fd:
	close(fd);

out:
	return ret;
}

static int ksm_advise(pid_t pid, bool _merge)
{
	int ret = 0;
	char path[PATH_MAX];
	int fd;

	ret = snprintf(path, sizeof(path), KSM_ADVISE, pid);
	if (ret < 0)
	{
		ret = EINVAL;
		goto out;
	}

	fd = open(path, O_WRONLY);
	if (fd == -1)
	{
		ret = errno;
		goto out;
	}

	ret = write(fd, _merge ? "merge" : "unmerge", _merge ? 5 : 7);
	if (ret == -1)
	{
		ret = errno;
		goto close_fd;
	}

close_fd:
	close(fd);

out:
	return ret;
}

int main(int _argc, char** _argv)
{
	(void)_argc;
	(void)_argv;
	int ret;
	pid_t self;
	long ctps;
	sigset_t sigmask;
	sigset_t sigorigmask;
	proc_t proc_info;
	struct timespec now;
	struct timespec time_to_sleep;
	siginfo_t siginfo;

	if (getuid())
	{
		ret = EACCES;
		fprintf(stderr, "getuid: %s (root privileges required)\n", strerror(ret));
		goto out;
	}

	ret = daemon(0, 0);
	if (ret == -1)
	{
		ret = errno;
		fprintf(stderr, "daemon: %s\n", strerror(ret));
		goto out;
	}

	self = getpid();
	ctps = sysconf(_SC_CLK_TCK);

	ret = ksm_ctl(true);
	if (ret)
	{
		fprintf(stderr, "ksm_ctl: %s\n", strerror(ret));
		goto out;
	}

	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);
	ret = sigprocmask(SIG_BLOCK, &sigmask, &sigorigmask);
	if (ret == -1)
	{
		ret = errno;
		fprintf(stderr, "sigprocmask: %s\n", strerror(ret));
		goto ksm_ctl_false;
	}

	while (true)
	{
		clock_gettime(CLOCK_BOOTTIME, &now);

		memset(&proc_info, 0, sizeof(proc_info));

		PROCTAB* proc = openproc(PROC_FILLSTATUS | PROC_FILLSTAT);
		while (readproc(proc, &proc_info) != NULL)
		{
			/* skip kthreads */
			if (!proc_info.vm_size)
				continue;

			/* skip ourselves */
			if (proc_info.tid == self)
				continue;

			/* skip short-living tasks */
			if (now.tv_sec - proc_info.start_time / ctps < OBSERVE_WINDOW_SECS)
				continue;

			if (ksm_advise(proc_info.tid, true))
				continue;
		}
		closeproc(proc);

		time_to_sleep.tv_sec = IDLE_SLEEP_SECS;
		time_to_sleep.tv_nsec = 0;
		ret = sigtimedwait(&sigmask, &siginfo, &time_to_sleep);
		if (ret == SIGINT || ret == SIGTERM)
		{
			printf("Caught signal %d, shutting down gracefully...\n", ret);
			goto unblock_signals;
		} else if (ret == -1)
		{
			switch (errno)
			{
				case EINVAL:
					ret = errno;
					fprintf(stderr, "sigtimedwait: %s\n", strerror(ret));
					goto unblock_signals;
				case EINTR:
					if (!siginfo.si_signo)
					{
						printf("Are we being traced?\n");
						continue;
					} else
					{
						ret = errno;
						fprintf(stderr, "sigtimedwait: an unblocked signal %d has been caught\n", siginfo.si_signo);
						goto unblock_signals;
					}
					break;
				case EAGAIN:
					/* timeout, just continuing */
					continue;
			}
		}
	}

unblock_signals:
	ret = sigprocmask(SIG_SETMASK, &sigorigmask, NULL);
	if (ret == -1)
	{
		ret = errno;
		fprintf(stderr, "sigprocmask: %s\n", strerror(ret));
		goto ksm_ctl_false;
	}

ksm_ctl_false:
	ret = ksm_ctl(false);
	if (ret)
	{
		fprintf(stderr, "ksm_ctl: %s\n", strerror(ret));
		goto out;
	}

out:
	exit(ret);
}

