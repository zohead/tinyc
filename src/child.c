#include "./child.h"

int
tc_child_main(void* arg)
{
	tc_proc_t* proc = arg;

#ifdef RAND_HOSTNAME
	_TC_MUST_P_GO(!sethostname(proc->hostname, strlen(proc->hostname)),
	              "sethostname",
	              abort,
	              "couldn't set hostname to %s",
	              proc->hostname);
#endif

	if (proc->rootfs && strlen(proc->rootfs) > 0) {
		_TC_MUST_GO(
		  !tc_child_mounts(proc), abort, "couldn't set child mounts");

		if (proc->disable_proc == false) {
			_TC_MUST_GO(
			  !tc_child_mount_procfs(), abort, "couldn't mount procfs");
		}
	}

	if (proc->disable_userns_remap == false) {
		_TC_MUST_GO(
		  !tc_child_set_userns(proc), abort, "couldn't set userns");
	}

	if (proc->disable_capabilities == false) {
		_TC_MUST_GO(
		  !tc_child_capabilities(), abort, "couldn't set capabilities");
	}

#ifdef SUPPORT_SECCOMP
	if (proc->disable_seccomp == false) {
		_TC_MUST_GO(!tc_child_block_syscalls(),
		            abort,
		            "couldn't block syscalls via seccomp");
	}
#endif

	if (proc->parent_ipc_socket >= 0) {
		_TC_MUST_P_GO(!close(proc->parent_ipc_socket),
			      "close",
			      abort,
			      "couldn't close parent fd %d",
			      proc->parent_ipc_socket);
	}

	_TC_DEBUG("[child] starting execution of process %s", proc->argv[0]);

	_TC_MUST_P_GO(!execve(proc->argv[0], proc->argv, proc->envp),
	              "execve",
	              abort,
	              "couldn't execute process %s",
	              proc->argv[0]);

	return 0;

abort:
	_TC_INFO("[child] failed to execute child");
	tc_proc_cleanup(proc);
	return 1;
}

#ifdef SUPPORT_SECCOMP
int
tc_child_block_syscalls()
{
	scmp_filter_ctx ctx = NULL;
	tc_child_seccomp_mask const* mask;

	_TC_DEBUG("[child] starting filtering of syscalls");

	_TC_MUST_P_GO((ctx = seccomp_init(SCMP_ACT_ALLOW)),
	              "seccomp_init",
	              abort,
	              "couldn't initialize seccomp context");

	for (size_t i = 0; i < tc_child_seccomp_masks_len; i++) {
		mask = &tc_child_seccomp_masks[i];
		_TC_MUST_P_GO(
		  !seccomp_rule_add(
		    ctx, mask->action, mask->syscall, mask->arg_cnt, mask->cmp),
		  "seccomp_rule_add",
		  abort,
		  "couldn't add seccomp rule");
	}

	_TC_MUST_P_GO(!seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 0),
	              "seccomp_attr_set",
	              abort,
	              "couldn't set seccomp attr");
	_TC_MUST_P_GO(!seccomp_load(ctx),
	              "seccomp_load",
	              abort,
	              "couldn't load seccomp rule");

	return 0;

abort:
	_TC_INFO("[child] failed to block syscalls via seccomp");
	if (ctx) {
		seccomp_release(ctx);
	}

	return 1;
}
#endif

int
tc_child_mount_procfs()
{
	_TC_MUST_P_GO(!mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL),
	              "mount",
	              abort,
	              "failed to mount 'none' in '/'");
	_TC_MUST_P_GO(
	  !mount(
	    "proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL),
	  "mount",
	  abort,
	  "failed to mount 'proc' in '/proc'");

	return 0;

abort:
	_TC_INFO("[child] failed to mount procfs");
	return 1;
}

int
tc_child_capabilities()
{
	cap_t caps;

	_TC_DEBUG("[child] dropping capabilities");

	for (size_t i = 0; i < tc_child_dropped_capabilities_len; i++) {
		_TC_MUST_P_GO(
		  prctl(PR_CAPBSET_DROP,
		        tc_child_dropped_capabilities[i],
		        0,
		        0,
		        0) != -1,
		  "prctl",
		  abort,
		  "Couldn't drop capability %d for the current proc",
		  tc_child_dropped_capabilities[i]);
	}

	_TC_DEBUG("[child] Setting inheritable capabilities");

	_TC_MUST_P_GO((caps = cap_get_proc()) != NULL,
	              "cap_get_proc",
	              abort,
	              "couldn't allocate proc capability state");

	_TC_MUST_P_GO(cap_set_flag(caps,
	                           CAP_INHERITABLE,
	                           tc_child_dropped_capabilities_len,
	                           tc_child_dropped_capabilities,
	                           CAP_CLEAR) != -1,
	              "cap_set_flag",
	              abort,
	              "couldn't set flag of desired"
	              " capabilities to inheritable");

	_TC_MUST_P_GO(cap_set_proc(caps) != -1,
	              "cap_set_proc",
	              abort,
	              "couldn't set process capabilities from cap state");

	_TC_MUST_P_GO(!cap_free(caps),
	              "cap_free",
	              abort,
	              "couldn't release memory allocated for capabilities");

	_TC_DEBUG("[child] capabilities dropped");
	return 0;

abort:
	_TC_INFO("[child] failed to set child capabilities");
	return 1;
}

int
tc_child_mounts(tc_proc_t* proc)
{
	char mount_dir[] = "/tmp/tmp.XXXXXX";
	char inner_mount_dir[] = "/tmp/tmp.XXXXXX/oldroot.XXXXXX";

	_TC_DEBUG("[child] remounting '/' with MS_PRIVATE");

	_TC_MUST_P_GO(!mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL),
	              "mount",
	              abort,
	              "couldn't remount '/' with MS_PRIVATE");
	_TC_MUST_P_GO(mkdtemp(mount_dir) != NULL,
	              "mkdtemp",
	              abort,
	              "couldn't create temporary directory '%s'",
	              mount_dir);

	_TC_DEBUG(
	  "[child] mounting rootfs to temporary dir (rootfs=%s,mount_dir=%s)",
	  proc->rootfs,
	  mount_dir);

	_TC_MUST_P_GO(
	  !mount(proc->rootfs, mount_dir, NULL, MS_BIND | MS_PRIVATE, NULL),
	  "mount",
	  abort,
	  "failed to mount (src=%s,dst=%s)",
	  proc->rootfs,
	  mount_dir);

	memcpy(inner_mount_dir, mount_dir, sizeof(mount_dir) - 1);
	_TC_MUST_P_GO(mkdtemp(inner_mount_dir),
	              "mkdtemp",
	              abort,
	              "failed creating temporary inner dir %s",
	              inner_mount_dir);

	_TC_DEBUG("[child] pivoting root (new=%s,put_old=%s)",
	          mount_dir,
	          inner_mount_dir);

	_TC_MUST_P_GO(!tc_syscall_pivot_root(mount_dir, inner_mount_dir),
	              "pivot_root",
	              abort,
	              "couldn't pivot root (new=%s,put_old=%s)",
	              mount_dir,
	              inner_mount_dir);

	char *old_root = inner_mount_dir + sizeof(mount_dir) - 1;

	_TC_DEBUG("[child] unmounting old root (old_root=%s)", old_root);

	if (chdir("/")) {
		fprintf(stderr, "chdir failed! %m\n");
		return -1;
	}

	char tmp_dir[sizeof(inner_mount_dir)];
	snprintf(tmp_dir, sizeof(tmp_dir), "%s%s", old_root, mount_dir);
	rmdir(tmp_dir);

	if (proc->old_rootfs && proc->old_rootfs[0])
		mount(old_root, proc->old_rootfs, NULL, MS_BIND | MS_PRIVATE, NULL);

	if (umount2(old_root, MNT_DETACH)) {
		fprintf(stderr, "umount failed! %m\n");
		return -1;
	}

	if (rmdir(old_root)) {
		fprintf(stderr, "rmdir failed! %m\n");
		return -1;
	}

	if (proc->readonly)
		mount(NULL, "/", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL);

	if (proc->enable_debug)
		fprintf(stderr, "done.\n");
	return 0;

abort:
	_TC_INFO("[child] failed to set child mounts");
	return 1;
}

// TODO improve socket communication with enum
//      to better handle information sharing.
int
tc_child_set_userns(tc_proc_t* proc)
{
	int result = 0;
	int has_userns = !unshare(CLONE_NEWUSER);
	gid_t gid = (gid_t)proc->uid;

	_TC_DEBUG("[child] writing userns to parent");

	_TC_MUST_P_GO(write(proc->parent_ipc_socket,
	                    &has_userns,
	                    sizeof(has_userns)) == sizeof(has_userns),
	              "write",
	              abort,
	              "failed to write to parent");

	_TC_DEBUG("[child] waiting parent");

	_TC_MUST_P_GO(read(proc->parent_ipc_socket, &result, sizeof(result)) ==
	                sizeof(result),
	              "read",
	              abort,
	              "failed to read from parent");

	_TC_DEBUG("[child] got response from parent - %d", result);

	if (result != 0) {
		return -1;
	}

	_TC_DEBUG(
	  "[child] switching to uid %d / gid %d...", proc->uid, proc->uid);

	_TC_MUST_P_GO((!setgroups(1, &gid)),
	              "setgroups",
	              abort,
	              "failed to set process user group");
	_TC_MUST_P_GO((!setresgid(proc->uid, proc->uid, proc->uid)),
	              "setresgid",
	              abort,
	              "failed to set real gid");
	_TC_MUST_P_GO(!(setresuid(proc->uid, proc->uid, proc->uid)),
	              "setresuid",
	              abort,
	              "failed to set real uid");

	return 0;
abort:
	_TC_INFO("[child] failed to set child userns");
	return 1;
}
