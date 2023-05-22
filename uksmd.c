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
#include <sys/pidfd.h>
#include <sys/resource.h>
#if defined HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif /* HAVE_SYSTEMD */
#include <time.h>
#include <unistd.h>

#define KSM_RUN				"/sys/kernel/mm/ksm/run"
#define KSM_FULL_SCANS		"/sys/kernel/mm/ksm/full_scans"
#define KSM_PAGES_VOLATILE	"/sys/kernel/mm/ksm/pages_volatile"
#if defined HAVE_SYSTEMD
#define KSM_PROFIT			"/sys/kernel/mm/ksm/general_profit"
#endif /* HAVE_SYSTEMD */
#define KSMD_CMD			"ksmd"
#define OBSERVE_WINDOW_SECS	30
#define IDLE_SLEEP_SECS		15

#define __SYSFS_process_ksm_enable	"/sys/kernel/process_ksm/process_ksm_enable"
#define __SYSFS_process_ksm_disable	"/sys/kernel/process_ksm/process_ksm_disable"
#define __SYSFS_process_ksm_status	"/sys/kernel/process_ksm/process_ksm_status"

enum pksm_action
{
	PKSM_ENABLE = 0,
	PKSM_DISABLE,
	PKSM_STATUS,
};

static long __NR_process_ksm_enable = -1;
static long __NR_process_ksm_disable = -1;
static long __NR_process_ksm_status = -1;

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

static long process_ksm_enable(int pidfd, unsigned int flags)
{
	return syscall(__NR_process_ksm_enable, pidfd, flags);
}

static long process_ksm_disable(int pidfd, unsigned int flags)
{
	return syscall(__NR_process_ksm_disable, pidfd, flags);
}

static long process_ksm_status(int pidfd, unsigned int flags)
{
	return syscall(__NR_process_ksm_status, pidfd, flags);
}

static long process_ksm(pid_t pid, enum pksm_action _action)
{
	long ret;
	int pidfd;

	pidfd = pidfd_open(pid, 0);
	if (pidfd == -1)
	{
		ret = errno;
		goto out;
	}

	switch (_action)
	{
		case PKSM_ENABLE:
			ret = process_ksm_enable(pidfd, 0);
			break;
		case PKSM_DISABLE:
			ret = process_ksm_disable(pidfd, 0);
			break;
		case PKSM_STATUS:
			ret = process_ksm_status(pidfd, 0);
			break;
	}

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

static int do_setup_process_ksm(const char* _path, long* _nr)
{
	int ret = 0;
	char buf[4] = { 0, };
	ssize_t read_len;
	long nr;

	int fd = open(_path, O_RDONLY);
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

	*_nr = nr;

close_fd:
	close(fd);

out:
	return ret;
}

static int setup_nr_process_ksm(void)
{
	int ret = 0;

	ret = do_setup_process_ksm(__SYSFS_process_ksm_enable, &__NR_process_ksm_enable);
	if (ret)
		goto out;

	ret = do_setup_process_ksm(__SYSFS_process_ksm_disable, &__NR_process_ksm_disable);
	if (ret)
		goto out;

	ret = do_setup_process_ksm(__SYSFS_process_ksm_status, &__NR_process_ksm_status);

out:
	return ret;
}

static int get_ksm_gauge(const char *_name, long *_value)
{
	int ret = 0;
	char buf[21] = { 0, };
	ssize_t read_len;
	unsigned long value;

	int fd = open(_name, O_RDONLY);
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

	value = strtol(buf, NULL, 10);
	if (value == LONG_MIN || value == LONG_MAX)
	{
		ret = errno;
		goto close_fd;
	}

	*_value = value;

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
	long full_scans;
	long prev_full_scans;
	bool first_run;
	long pages_volatile;
#if defined HAVE_SYSTEMD
	long profit;
#endif /* HAVE_SYSTEMD */

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

	if (setup_nr_process_ksm())
	{
		ret = ENODATA;
		fprintf(stderr, "Unable to get process_ksm syscall numbers\n");
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

#if !defined HAVE_SYSTEMD
	ret = daemon(0, 0);
	if (ret == -1)
	{
		ret = errno;
		fprintf(stderr, "daemon: %s\n", strerror(ret));
		goto out;
	}
#endif /* HAVE_SYSTEMD */

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

#if defined HAVE_SYSTEMD
	sd_notify(0, "READY=1");
#endif /* HAVE_SYSTEMD */

	first_run = true;
	full_scans = prev_full_scans = 0;
	while (true)
	{
#if defined HAVE_SYSTEMD
		sd_notify(0, "WATCHDOG=1");
		ret = get_ksm_gauge(KSM_PROFIT, &profit);
		if (ret)
		{
			fprintf(stderr, "get KSM_PROFIT: %s\n", strerror(ret));
			goto unblock_signals;
		}
		sd_notifyf(0, "STATUS=Profit: %ld MiB", profit / (1L << 20));
#endif /* HAVE_SYSTEMD */

		ret = get_ksm_gauge(KSM_FULL_SCANS, &full_scans);
		if (ret)
		{
			fprintf(stderr, "get KSM_FULL_SCANS: %s\n", strerror(ret));
			goto unblock_signals;
		}

		ret = get_ksm_gauge(KSM_PAGES_VOLATILE, &pages_volatile);
		if (ret)
		{
			fprintf(stderr, "get KSM_PAGES_VOLATILE: %s\n", strerror(ret));
			goto unblock_signals;
		}

		if (first_run || full_scans != prev_full_scans || !pages_volatile)
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

				/* skip already processed tasks */
				if (process_ksm(proc_info.tid, PKSM_STATUS))
					continue;

				if (process_ksm(proc_info.tid, PKSM_ENABLE))
					continue;
			}
			closeproc(proc);

			if (first_run)
				first_run = false;

			prev_full_scans = full_scans;
		}

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
						fprintf(stderr, "sigtimedwait: EINTR came with si_signo = %d\n", siginfo.si_signo);
						continue;
					}
					break;
				case EAGAIN:
					/* timeout, just continuing */
					continue;
			}
		}
	}

unblock_signals:
#if defined HAVE_SYSTEMD
	sd_notify(0, "STOPPING=1");
#endif /* HAVE_SYSTEMD */

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
#if defined HAVE_SYSTEMD
	sd_notifyf(0, "ERRNO=%d", ret);
#endif /* HAVE_SYSTEMD */

	exit(ret);
}

