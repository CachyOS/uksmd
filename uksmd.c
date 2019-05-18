#include <errno.h>
#include <fcntl.h>
#include <proc/readproc.h>
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
	proc_t proc_info;
	struct timespec now;
	unsigned int time_to_sleep;

	if (getuid())
	{
		ret = EACCES;
		fprintf(stderr, "%s\n", strerror(ret));
		goto out;
	}

	self = getpid();
	ctps = sysconf(_SC_CLK_TCK);

	ret = ksm_ctl(true);
	if (ret)
	{
		fprintf(stderr, "%s\n", strerror(ret));
		goto out;
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

		time_to_sleep = IDLE_SLEEP_SECS;
		while ((time_to_sleep = sleep(time_to_sleep)) && errno == EINTR)
			continue;
	}

	ret = ksm_ctl(false);
	if (ret)
	{
		fprintf(stderr, "%s\n", strerror(ret));
		goto out;
	}

out:
	exit(ret);
}

