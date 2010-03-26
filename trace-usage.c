#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "trace-local.h"
#include "version.h"

struct usage_help {
	char *name;
	char *short_help;
	char *long_help;
};

static struct usage_help usage_help[] = {
	{
		"record",
		"record a trace into a trace.dat file",
		" %s record [-v][-e event [-f filter]][-p plugin][-F][-d][-o file] \\\n"
		"           [-s usecs][-O option ][-l func][-g func][-n func] \\\n"
		"           [-P pid][-N host:port][-t][-b size][command ...]\n"
		"          -e run command with event enabled\n"
		"          -f filter for previous -e event\n"
		"          -p run command with plugin enabled\n"
		"          -F filter only on the given process\n"
		"          -P trace the given pid like -F for the command\n"
		"          -l filter function name\n"
		"          -g set graph function\n"
		"          -n do not trace function\n"
		"          -v will negate all -e after it (disable those events)\n"
		"          -d disable function tracer when running\n"
		"          -o data output file [default trace.dat]\n"
		"          -O option to enable (or disable)\n"
		"          -s sleep interval between recording (in usecs) [default: 1000]\n"
		"          -N host:port to connect to (see listen)\n"
		"          -t used with -N, forces use of tcp in live trace\n"
		"          -b change kernel buffersize (in kilobytes per CPU)\n"
	},
	{
		"start",
		"start tracing without recording into a file",
		" %s start [-e event][-p plugin][-d][-O option ][-P pid]\n"
		"          Uses same options as record, but does not run a command.\n"
		"          It only enables the tracing and exits\n"
	},
	{
		"extract",
		"extract a trace from the kernel",
		" %s extract [-p plugin][-O option][-o file]\n"
		"          Uses same options as record, but only reads an existing trace.\n"
	},
	{
		"stop",
		"stop the kernel from recording trace data",
		" %s stop\n"
		"          Stops the tracer from recording more data.\n"
		"          Used in conjunction with start\n"
	},
	{
		"reset",
		"disable all kernel tracing and clear the trace buffers",
		" %s reset [-b size]\n"
		"          Disables the tracer (may reset trace file)\n"
		"          Used in conjunction with start\n"
		"          -b change the kernel buffer size (in kilobytes per CPU)\n"
	},
	{
		"report",
		"read out the trace stored in a trace.dat file",
		" %s report [-i file] [--cpu cpu] [-e][-f][-l][-P][-L][-N][-E][-F filter][-v][-V]\n"
		"          -i input file [default trace.dat]\n"
		"          -e show file endianess\n"
		"          -f show function list\n"
		"          -P show printk list\n"
		"          -L load only local (~/.trace-cmd/plugins) plugins\n"
		"          -N do not load any plugins\n"
		"          -E show event files stored\n"
		"          -F filter to filter output on\n"
		"          -v will negate all -F after it (Not show matches)\n"
		"          -V verbose (shows plugins being loaded)\n"
		"          -w show wakeup latencies\n"
		"          -l show latency format (default with latency tracers)\n"
	},
	{
		"split",
		"parse a trace.dat file into smaller file(s)",
		" %s split [options] -o file [start [end]]\n"
		"          -o output file to write to (file.1, file.2, etc)\n"
		"          -s n  split file up by n seconds\n"
		"          -m n  split file up by n milliseconds\n"
		"          -u n  split file up by n microseconds\n"
		"          -e n  split file up by n events\n"
		"          -p n  split file up by n pages\n"
		"          -r    repeat from start to end\n"
		"          -c    per cpu, that is -p 2 will be 2 pages for each CPU\n"
		"          if option is specified, it will split the file\n"
		"           up starting at start, and ending at end\n"
		"          start - decimal start time in seconds (ex: 75678.923853)\n"
		"                  if left out, will start at beginning of file\n"
		"          end   - decimal end time in seconds\n"
	},
	{
		"listen",
		"listen on a network socket for trace clients",
		" %s listen -p port[-D][-o file][-d dir]\n"
		"          Creates a socket to listen for clients.\n"
		"          -D create it in daemon mode.\n"
		"          -o file name to use for clients.\n"
		"          -d diretory to store client files.\n"
	},
	{
		"list",
		"list the available events, plugins or options",
		" %s list [-e][-p]\n"
		"          -e list available events\n"
		"          -p list available plugins\n"
		"          -o list available options\n"
	},
	{
		NULL, NULL, NULL
	}
};

static struct usage_help *find_help(char *cmd)
{
	struct usage_help *help;

	help = usage_help;
	while (help->name) {
		if (strcmp(cmd, help->name) == 0)
			return help;
		help++;
	}
	return NULL;
}

void usage(char **argv)
{
	struct usage_help *help = NULL;
	char *arg = argv[0];
	char *p;

	p = basename(arg);

	printf("\n"
	       "%s version %s\n\n"
	       "usage:\n", p, VERSION_STRING);

	if (argv[1])
		help = find_help(argv[1]);

	if (help) {
		printf(help->long_help, p);
		goto out;
	}

	printf("  %s [COMMAND] ...\n\n"
	       "  commands:\n", p);

	help = usage_help;
	while (help->name) {
		printf("     %s - %s\n", help->name, help->short_help);
		help++;
	}
 out:
	printf("\n");
	exit(-1);
}