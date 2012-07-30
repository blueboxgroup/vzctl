#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "types.h"
#include "vzerror.h"
#include "cgroup.h"

static int copy_string_from_parent(struct cgroup_controller *controller,
				   struct cgroup_controller *pcont, const char *file)
{
	char *ptr = NULL;
	int ret;

	ret = cgroup_get_value_string(pcont, file, &ptr);
	if (ret)
		goto out;
	ret = cgroup_set_value_string(controller, file, ptr);
out:
	free(ptr);
	return ret;
}

static int controller_apply_config(struct cgroup *ct, struct cgroup *parent,
				   struct cgroup_controller *controller,
				   const char *name)
{
	int ret;
	if (!strcmp("cpuset", name)) {
		struct cgroup_controller *pcont = cgroup_get_controller(parent, name);
		if (!pcont)
			return 0;

		if ((ret = copy_string_from_parent(controller, pcont, "cpuset.cpus")))
			return ret;

		if ((ret = copy_string_from_parent(controller, pcont, "cpuset.mems")))
			return ret;
	}
	return 0;
}

static int do_create_container(struct cgroup *ct, struct cgroup *parent)
{
	struct cgroup_mount_point mnt;
	struct cgroup_controller *controller;
	void *handle;
	int ret;

	ret = cgroup_get_controller_begin(&handle, &mnt);

	cgroup_get_cgroup(parent);

	do {
		controller = cgroup_add_controller(ct, mnt.name);
		ret = controller_apply_config(ct, parent, controller, mnt.name);
		if (!ret)
			ret = cgroup_get_controller_next(&handle, &mnt);
	} while (!ret);

	cgroup_get_controller_end(&handle);

	if (ret == ECGEOF)
		ret = cgroup_create_cgroup(ct, 0);

	return ret;

}

int create_container(envid_t veid)
{
	char cgrp[CT_MAX_STR_SIZE];
	struct cgroup *ct, *parent;
	int ret;

	veid_to_name(cgrp, veid);
	ct = cgroup_new_cgroup(cgrp);
	parent = cgroup_new_cgroup(CT_BASE_STRING);

	ret = do_create_container(ct, parent);
	cgroup_free(&ct);
	cgroup_free(&parent);

	return ret;
}

/* libcgroup is lame. This should be done with the cgroup structure, not the
 * cgroup name
 */
static int controller_has_tasks(const char *cgrp, const char *name)
{
	int ret;
	pid_t pid;
	void *handle;

	ret = cgroup_get_task_begin(cgrp, name, &handle, &pid);
	ret = (ret != ECGEOF);
	cgroup_get_task_end(&handle);
	return ret;
}

int container_add_task(envid_t veid)
{
	char cgrp[CT_MAX_STR_SIZE];
	struct cgroup *ct;

	veid_to_name(cgrp, veid);
	ct = cgroup_new_cgroup(cgrp);
	if (cgroup_get_cgroup(ct))
		return -1;

	cgroup_attach_task_pid(ct, getpid());
	cgroup_free(&ct);
	return 0;
}

int destroy_container(envid_t veid)
{
	struct cgroup *ct;
	char cgrp[CT_MAX_STR_SIZE];
	int ret;

	veid_to_name(cgrp, veid);
	ct = cgroup_new_cgroup(cgrp);
	ret = cgroup_delete_cgroup_ext(ct, 0);
	cgroup_free(&ct);
	return ret;
}

int container_is_running(envid_t veid)
{
	int ret = 0;
	void *handle;
	struct cgroup_mount_point mnt;
	struct cgroup *ct;
	char cgrp[CT_MAX_STR_SIZE];

	veid_to_name(cgrp, veid);

	ct = cgroup_new_cgroup(cgrp);
	ret = cgroup_get_cgroup(ct);
	if (ret == ECGROUPNOTEXIST) {
		ret = 0;
		goto out_free;
	}

	ret = cgroup_get_controller_begin(&handle, &mnt);
	do {
		if ((ret = controller_has_tasks(cgrp, mnt.name)) != 0)
			goto out;
	} while ((ret = cgroup_get_controller_next(&handle, &mnt)) == 0);

	if (ret != ECGEOF)
		ret = -ret;
	else
		ret = 0;
out:
	cgroup_get_controller_end(&handle);
out_free:
	cgroup_free(&ct);
	return ret;
}

/*
 * This function assumes that all pids inside a cgroup
 * belong to the same namespace, that is the container namespace.
 * Therefore, from the host box, any of them will do.
 */
pid_t get_pid_from_container(envid_t veid)
{
	char cgrp[CT_MAX_STR_SIZE];
	struct cgroup *ct;
	void *task_handle;
	void *cont_handle;
	struct cgroup_mount_point mnt;
	pid_t pid = -1;
	int ret;

	veid_to_name(cgrp, veid);
	ct = cgroup_new_cgroup(cgrp);
	ret = cgroup_get_cgroup(ct);
	if (ret == ECGROUPNOTEXIST)
		goto out_free;

	ret = cgroup_get_controller_begin(&cont_handle, &mnt);
	if (ret != 0) /* no controllers, something is wrong */
		goto out_free;

	ret = cgroup_get_task_begin(cgrp, mnt.name, &task_handle, &pid);
	if (ret != 0) /* no tasks, something is also wrong */
		goto out_end_cont;
	cgroup_get_task_end(&task_handle);

out_end_cont:
	cgroup_get_controller_end(&cont_handle);
out_free:
	cgroup_free(&ct);
	return pid;
}
int container_init(void)
{
	int ret;
	struct cgroup *ct, *parent;
	struct cgroup_controller *mem;

	cgroup_init();
	ct  = cgroup_new_cgroup(CT_BASE_STRING);
	parent  = cgroup_new_cgroup("/");
	ret = do_create_container(ct, parent);

	/*
	 * We do it here, because writes to memory.use_hierarchy from a kid
	 * whose parent have hierarchy set, will fail
	 */
	mem = cgroup_add_controller(ct, "memory");
	cgroup_set_value_string(mem, "memory.use_hierarchy", "1");
	cgroup_free(&ct);
	cgroup_free(&parent);
	return ret;
}
