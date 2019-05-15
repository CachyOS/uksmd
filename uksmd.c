#include <proc/readproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

int main(int _argc, char** _argv)
{
	(void)_argc;
	(void)_argv;
	proc_t proc_info;

	memset(&proc_info, 0, sizeof(proc_info));

	PROCTAB* proc = openproc(PROC_FILLSTATUS | PROC_FILLSTAT);
	while (readproc(proc, &proc_info) != NULL)
	{
		if (!proc_info.vm_size)
			continue;
		printf("%u %s %llu\n", proc_info.tid, proc_info.cmd, proc_info.start_time / sysconf(_SC_CLK_TCK));
	}
	closeproc(proc);

	exit(EX_OK);
}

