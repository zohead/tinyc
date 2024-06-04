#include <stdlib.h>
#include <sys/time.h>

#include "./child.h"
#include "./cli.h"
#include "./common.h"
#ifdef RAND_HOSTNAME
#include "./names.h"
#endif
#include "./proc.h"

int
main(int argc, char** argv)
{
#ifdef RAND_HOSTNAME
	struct timeval time;
#endif
	tc_cli_t cli = { 0 };
	tc_proc_t proc = { 0 };
	int err = 0;

	// CC:  parses the command line arguments
	if (tc_cli_parse(&cli, argc, argv)) {
		tc_cli_help();
		fprintf(stderr,
		        "ERROR: Couldn't properly parse CLI arguments.\n"
		        "Make sure you're passing the right arguments.\n"
		        "Aborting.");
		exit(1);
	}

	if (cli.help == true) {
		tc_cli_help();
		exit(0);
	}

#ifdef RAND_HOSTNAME
	// CC:  initialize the seed for our random choice of
	//      container name
	gettimeofday(&time, NULL);
	srand((time.tv_sec * 1000) + (time.tv_usec / 1000));
#endif

	proc.envpc = cli.envc;
	proc.envp = cli.envp;
	proc.parent_ipc_socket = proc.child_ipc_socket = -1;
	proc.argv = cli.argv;
	proc.argc = cli.argc;
	proc.rootfs = cli.rootfs;
	proc.enable_debug = cli.debug;
	proc.disable_cgroups = cli.disable_cgroups;
	proc.disable_mount = cli.disable_mount;
	proc.disable_pid = cli.disable_pid;
	proc.disable_ipc = cli.disable_ipc;
	proc.disable_net = cli.disable_net;
	proc.disable_uts = cli.disable_uts;
	proc.disable_proc = cli.disable_proc;
	proc.disable_seccomp = cli.privileged;
	proc.disable_capabilities = cli.privileged;
	proc.disable_userns_remap = cli.userns_remap == false;

#ifdef RAND_HOSTNAME
	// CC:  fill the hostname of the process that we'll
	//      start.
	tc_names_fill(proc.hostname, 255);
#endif
	if (proc.enable_debug)
		tc_proc_show(&proc);

	err = tc_proc_init(&proc);
	if (err) {
		fprintf(stderr,
		        "ERROR: Couldn't properly run the application.\n"
		        "Aborting.");
		goto abort;
	}

	err = tc_proc_run(&proc, tc_child_main);
	if (err) {
		fprintf(stderr,
		        "ERROR: Couldn't properly run the application.\n"
		        "Aborting.");
		goto abort;
	}

	tc_cli_cleanup(&cli);
	tc_proc_cleanup(&proc);

	return 0;

abort:
	tc_cli_cleanup(&cli);
	tc_proc_cleanup(&proc);
	return 1;
}
