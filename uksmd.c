#include <errno.h>
#include <proc/readproc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#define OBSERVE_WINDOW_SECS	10
#define IDLE_SLEEP_SECS		5

int main(int _argc, char** _argv)
{
	(void)_argc;
	(void)_argv;
	pid_t self = getpid();
	long ctps = sysconf(_SC_CLK_TCK);
	proc_t proc_info;
	struct timespec now;
	unsigned int time_to_sleep;

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

			printf("%u\t%s\n", proc_info.tid, proc_info.cmd);
		}
		closeproc(proc);

		time_to_sleep = IDLE_SLEEP_SECS;
		while ((time_to_sleep = sleep(time_to_sleep)) && errno == EINTR)
			continue;
	}

	exit(EX_OK);
}

