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

#include <cap-ng.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <proc/readproc.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define KSM_RUN		"/sys/kernel/mm/ksm/run"
#define KSMD_CMD		"ksmd"
#define OBSERVE_WINDOW_SECS	30
#define IDLE_SLEEP_SECS	60

#define __SYSFS_pmadv_ksm	"/sys/kernel/pmadv/ksm"

static int __NR_pmadv_ksm = -1;

static int ksm_ctl(bool _enable)
{
	int ret = 0;

	int fd = open(KSM_RUN, O_WRONLY);
	if (fd == -1)
	{
		ret = errno;
		goto out;
	}

	if (write(fd, _enable ? "1" : "2", 1) == -1)
	{
		ret = errno;
		goto close_fd;
	}

close_fd:
	close(fd);

out:
	return ret;
}

static int pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(__NR_pidfd_open, pid, flags);
}

static int pmadv_ksm(int pidfd, int behaviour, unsigned int flags)
{
	return syscall(__NR_pmadv_ksm, pidfd, behaviour, flags);
}

static int ksm_advise(pid_t pid, bool _merge)
{
	int ret;
	int pidfd;

	pidfd = pidfd_open(pid, 0);
	if (pidfd == -1)
	{
		ret = errno;
		goto out;
	}

	ret = pmadv_ksm(pidfd, _merge ? MADV_MERGEABLE : MADV_UNMERGEABLE, 0);
	if (ret == -1)
	{
		ret = errno;
		goto close_pidfd;
	}

close_pidfd:
	ret = close(pidfd);
	if (ret == -1)
	{
		ret = errno;
		goto out;
	}

out:
	return ret;
}

static int kthread_niceness(const char* _name)
{
	int ret = -1;
	proc_t proc_info = { 0, };

	PROCTAB* proc = openproc(PROC_FILLSTAT);
	while (readproc(proc, &proc_info) != NULL)
	{
		/* skip uthreads */
		if (proc_info.vm_size)
			continue;

		if (!strcmp(_name, proc_info.cmd))
		{
			ret = proc_info.nice;
			break;
		}
	}
	closeproc(proc);

	return ret;
}

static int setup_nr_pmadv_ksm(void)
{
	int ret = 0;
	char buf[4] = { 0, };
	ssize_t read_len;
	long nr;

	int fd = open(__SYSFS_pmadv_ksm, O_RDONLY);
	if (fd == -1)
	{
		ret = errno;
		goto out;
	}

	read_len = read(fd, buf, sizeof buf);
	if (read_len == -1)
	{
		ret = errno;
		goto close_fd;
	}

	nr = strtol(buf, NULL, 10);
	if (nr == LONG_MIN || nr == LONG_MAX)
	{
		ret = errno;
		goto close_fd;
	}

	__NR_pmadv_ksm = nr;

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
	int ksmd_niceness;
	pid_t self;
	long ctps;
	sigset_t sigmask;
	sigset_t sigorigmask;
	proc_t proc_info;
	struct timespec now;
	struct timespec time_to_sleep;
	siginfo_t siginfo;

	if (capng_get_caps_process() == -1)
	{
		ret = ENODATA;
		fprintf(stderr, "Unable to get capabilities\n");
		goto out;
	}

	if (!capng_have_capability(CAPNG_EFFECTIVE, CAP_SYS_PTRACE))
	{
		ret = EACCES;
		fprintf(stderr, "capabilities: CAP_SYS_PTRACE required\n");
		goto out;
	}

	if (!capng_have_capability(CAPNG_EFFECTIVE, CAP_DAC_OVERRIDE))
	{
		ret = EACCES;
		fprintf(stderr, "capabilities: CAP_DAC_OVERRIDE required\n");
		goto out;
	}

	if (!capng_have_capability(CAPNG_EFFECTIVE, CAP_SYS_NICE))
	{
		ret = EACCES;
		fprintf(stderr, "capabilities: CAP_SYS_NICE required\n");
		goto out;
	}

	if (setup_nr_pmadv_ksm())
	{
		ret = ENODATA;
		fprintf(stderr, "Unable to get pmadv_ksm syscall number\n");
		goto out;
	}

	ksmd_niceness = kthread_niceness("ksmd");
	if (ksmd_niceness == -1)
	{
		ret = ESRCH;
		fprintf(stderr, "Unable to get ksmd niceness\n");
		goto out;
	}

	ret = setpriority(PRIO_PROCESS, 0, ksmd_niceness);
	if (ret == -1 && errno)
	{
		ret = errno;
		fprintf(stderr, "setpriority: %s\n", strerror(ret));
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
					if (!siginfo.si_signo || siginfo.si_signo == SIGQUIT)
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

