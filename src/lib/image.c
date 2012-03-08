/*
 *  Copyright (C) 2011-2012, Parallels, Inc. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "image.h"
#include "logger.h"
#include "list.h"
#include "util.h"
#include "vzerror.h"
#include "vzctl.h"
#include "env.h"
#include "destroy.h"

#define DEFAULT_FSTYPE		"ext4"
#define VZCTL_VE_ROOTHDD_DIR	"root.hdd"

int get_ploop_type(const char *type)
{
	if (type == NULL)
		return -1;
	if (!strcmp(type, "expanded"))
		return PLOOP_EXPANDED_MODE;
	else if (!strcmp(type, "plain"))
		return PLOOP_EXPANDED_PREALLOCATED_MODE;
	else if (!strcmp(type, "raw"))
		return PLOOP_RAW_MODE;

	return -1;
}

int vzctl_mount_image(const char *ve_private, struct vzctl_mount_param *param)
{
	int ret;
	struct ploop_mount_param mount_param = {};
	char fname[MAXPATHLEN];
	struct ploop_disk_images_data *di;

	di = ploop_alloc_diskdescriptor();
	if (di == NULL)
		return VZ_RESOURCE_ERROR;
	GET_DISK_DESCRIPTOR(fname, ve_private)
	ret = ploop_read_diskdescriptor(fname, di);
	if (ret) {
		logger(-1, 0, "Failed to read %s", fname);
		ploop_free_diskdescriptor(di);
		return VZCTL_E_MOUNT_IMAGE;
	}

	mount_param.fstype = DEFAULT_FSTYPE;
	mount_param.target = param->target;
	mount_param.quota = param->quota;
	mount_param.mount_data = param->mount_data;

	ret = ploop_mount_image(di, &mount_param);
	if (ret) {
		logger(-1, 0, "Failed to mount image: %s [%d]",
				ploop_get_last_error(), ret);
		ret = VZCTL_E_MOUNT_IMAGE;
	}
	ploop_free_diskdescriptor(di);
	return ret;
}

int vzctl_umount_image(const char *ve_private, const char *target)
{
	int ret;
	char fname[MAXPATHLEN];
	struct ploop_disk_images_data *di;

	di = ploop_alloc_diskdescriptor();
	if (di == NULL)
		return VZ_RESOURCE_ERROR;

	GET_DISK_DESCRIPTOR(fname, ve_private)
	ret = ploop_read_diskdescriptor(fname, di);
	if (ret) {
		logger(-1, 0, "Failed to read %s", fname);
		ploop_free_diskdescriptor(di);
		return VZCTL_E_UMOUNT_IMAGE;
	}

	ret = ploop_umount_image(di);
	if (ret) {
		logger(-1, 0, "Failed to umount image: %s [%d]",
				ploop_get_last_error(), ret);
		ret = VZCTL_E_UMOUNT_IMAGE;
	}
	ploop_free_diskdescriptor(di);
	return ret;
}

int vzctl_create_image(const char *ve_private,
		struct vzctl_create_image_param *param)
{
	int ret = 0;
	struct ploop_create_param create_param = {};
	char image[MAXPATHLEN];

	snprintf(image, sizeof(image), "%s/" VZCTL_VE_ROOTHDD_DIR, ve_private);
	if (mkdir(image, 0700) && errno != EEXIST) {
		logger(-1, errno, "Failed to create image %s", image);
		return VZCTL_E_CREATE_IMAGE;
	}

	snprintf(image, sizeof(image), "%s/%s/root.hdd",
			ve_private, VZCTL_VE_ROOTHDD_DIR);

	logger(0, 0, "Creating image: %s size=%luK", image, param->size);
	create_param.mode = param->mode;
	create_param.fstype = DEFAULT_FSTYPE;
	create_param.size = param->size * 2; /* Kb to 512b sectors */
	create_param.image = image;
	ret = ploop_create_image(&create_param);
	if (ret) {
		logger(-1, 0, "Failed to create image: %s [%d]",
				ploop_get_last_error(), ret);
		return VZCTL_E_CREATE_IMAGE;
	}
	return 0;
}

int vzctl_convert_image(const char *ve_private, int mode)
{
	int ret;
	char fname[MAXPATHLEN];
	struct ploop_disk_images_data *di;

	di = ploop_alloc_diskdescriptor();
	if (di == NULL)
		return VZ_RESOURCE_ERROR;
	GET_DISK_DESCRIPTOR(fname, ve_private)
	ret = ploop_read_diskdescriptor(fname, di);
	if (ret) {
		logger(-1, 0, "Failed to read %s", fname);
		ploop_free_diskdescriptor(di);
		return VZCTL_E_CONVERT_IMAGE;
	}
	ret = ploop_convert_image(di, mode, 0);
	if (ret) {
		logger(-1, 0, "Failed to convert image: %s [%d]",
				ploop_get_last_error(), ret);
		ret = VZCTL_E_CONVERT_IMAGE;
	}
	ploop_free_diskdescriptor(di);
	return ret;
}

int vzctl_resize_image(const char *ve_private, unsigned long long newsize)
{
	int ret;
	struct ploop_disk_images_data *di;
	struct ploop_resize_param param;
	char fname[MAXPATHLEN];

	if (ve_private == NULL) {
		logger(-1, 0, "Failed to resize image: "
				"CT private is not specified");
		return VZCTL_E_RESIZE_IMAGE;
	}

	di = ploop_alloc_diskdescriptor();
	if (di == NULL)
		return VZ_RESOURCE_ERROR;
	GET_DISK_DESCRIPTOR(fname, ve_private)
	ret = ploop_read_diskdescriptor(fname, di);
	if (ret) {
		logger(-1, 0, "Failed to read %s", fname);
		ploop_free_diskdescriptor(di);
		return VZCTL_E_RESIZE_IMAGE;
	}
	param.size = newsize * 2; /* Kb to 512b sectors */
	ret = ploop_resize_image(di, &param);
	if (ret) {
		logger(-1, 0, "Failed to resize image: %s [%d]",
				ploop_get_last_error(), ret);
		ret = VZCTL_E_RESIZE_IMAGE;
	}
	ploop_free_diskdescriptor(di);
	return ret;
}

int vzctl_get_ploop_dev(const char *mnt, char *out, int len)
{
	return ploop_get_partition_by_mnt(mnt, out, len);
}

int vzctl_create_snapshot(const char *ve_private, const char *guid)
{
	char fname[MAXPATHLEN];
	struct ploop_disk_images_data *di;
	int ret;
	struct ploop_snapshot_param param = {};

	if (ve_private == NULL) {
		logger(-1, 0, "Failed to create snapshot: "
				"CT private is not specified");
		return VZ_VE_PRIVATE_NOTSET;
	}
	di = ploop_alloc_diskdescriptor();
	if (di == NULL)
		return VZ_RESOURCE_ERROR;
	GET_DISK_DESCRIPTOR(fname, ve_private)
	if (ploop_read_diskdescriptor(fname, di)) {
		logger(-1, 0, "Failed to read %s", fname);
		ploop_free_diskdescriptor(di);
		return VZCTL_E_CREATE_SNAPSHOT;
	}
	param.guid = (char *)guid;
	ret = ploop_create_snapshot(di, &param);
	if (ret) {
		logger(-1, 0, "Failed to create snapshot: %s [%d]",
				ploop_get_last_error(), ret);
		ret = VZCTL_E_CREATE_SNAPSHOT;
	}
	ploop_free_diskdescriptor(di);

	return ret;
}

int vzctl_delete_snapshot(const char *ve_private, const char *guid)
{
	char fname[MAXPATHLEN];
	struct ploop_disk_images_data *di;
	int ret;

	if (ve_private == NULL) {
		logger(-1, 0, "Failed to delete snapshot: "
				"CT private is not specified");
		return VZ_VE_PRIVATE_NOTSET;
	}
	di = ploop_alloc_diskdescriptor();
	if (di == NULL)
		return VZ_RESOURCE_ERROR;
	GET_DISK_DESCRIPTOR(fname, ve_private)
	if (ploop_read_diskdescriptor(fname, di)) {
		logger(-1, 0, "Failed to read %s", fname);
		ploop_free_diskdescriptor(di);
		return VZCTL_E_CREATE_SNAPSHOT;
	}
	ret = ploop_delete_snapshot(di, guid);
	if (ret) {
		logger(-1, 0, "Failed to delete snapshot: %s [%d]",
				ploop_get_last_error(), ret);
		ret = VZCTL_E_DELETE_SNAPSHOT;
	}
	ploop_free_diskdescriptor(di);

	return ret;
}

int vzctl_merge_snapshot(const char *ve_private, const char *guid)
{
	char fname[MAXPATHLEN];
	struct ploop_disk_images_data *di;
	int ret;
	struct ploop_merge_param param = {};

	if (guid == NULL)
		return VZCTL_E_MERGE_SNAPSHOT;
	if (ve_private == NULL) {
		logger(-1, 0, "Failed to merge snapshot: "
				"CT private is not specified");
		return VZ_VE_PRIVATE_NOTSET;
	}
	di = ploop_alloc_diskdescriptor();
	if (di == NULL)
		return VZ_RESOURCE_ERROR;
	GET_DISK_DESCRIPTOR(fname, ve_private)
	ret = VZCTL_E_MERGE_SNAPSHOT;
	if (ploop_read_diskdescriptor(fname, di)) {
		logger(-1, 0, "Failed to read %s", fname);
		goto err;
	}
	param.guid = (char *)guid;
	ret = ploop_merge_snapshot(di, &param);
	if (ret) {
		logger(-1, 0, "Failed to merge snapshot %s: %s [%d]",
				guid, ploop_get_last_error(), ret);
		goto err;
	}
err:
	ploop_free_diskdescriptor(di);

	return (ret = 0) ? 0: VZCTL_E_MERGE_SNAPSHOT;
}

int ve_private_is_ploop(const char *private)
{
	char image[MAXPATHLEN];

	snprintf(image, sizeof(image), "%s/%s/root.hdd",
			private, VZCTL_VE_ROOTHDD_DIR);
	return stat_file(image);
}

/* Convert a CT to ploop layout
 * 1) mount CT
 * 2) create & mount image
 * 3) cp VE_ROOT -> image
 * 4) update ve_layout
 */
int vzctl_env_convert_ploop(vps_handler *h, envid_t veid,
		fs_param *fs, dq_param *dq)
{
	struct vzctl_create_image_param param = {};
	struct vzctl_mount_param mount_param = {};
	int ploop_mounted = 0;
	int ret;
	char cmd[STR_SIZE];
	char new_private[STR_SIZE];

	if (ve_private_is_ploop(fs->private)) {
		logger(0, 0, "CT is already on ploop");
		return 0;
	}
	if (vps_is_run(h, veid)) {
		logger(-1, 0, "CT is running (stop it first)");
		return VZ_VE_RUNNING;
	}
	if (vps_is_mounted(fs->root)) {
		logger(-1, 0, "CT is mounted (umount it first)");
		return VZ_FS_MOUNTED;
	}
	if (!dq->diskspace || dq->diskspace[1] <= 0) {
		logger(-1, 0, "Error: diskspace not set");
		return VZ_DISKSPACE_NOT_SET;
	}

	snprintf(new_private, sizeof(new_private), "%s.ploop", fs->private);
	if (make_dir_mode(new_private, 1, 0600) != 0)
		return VZ_CANT_CREATE_DIR;

	param.mode = PLOOP_EXPANDED_MODE;
	param.size = dq->diskspace[1]; // limit
	ret = vzctl_create_image(new_private, &param);
	if (ret)
		goto err;

	mount_param.target = fs->root;

	ret = vzctl_mount_image(new_private, &mount_param);
	if (ret)
		goto err;
	ploop_mounted = 1;
	// Copy VE_ROOT -> image
	logger(0, 0, "Copying content to ploop...");
	snprintf(cmd, sizeof(cmd), "/bin/cp -ax %s/. %s",
			fs->private, fs->root);
	logger(1, 0, "Executing %s", cmd);
	ret = system(cmd);
	if (ret) {
		ret = VZ_SYSTEM_ERROR;
		goto err;
	}
	/* Finally, del the old private and replace it with the new one */
	del_dir(fs->private);
	rename(new_private, fs->private);
	logger(0, 0, "Container was successfully converted "
			"to the ploop layout");
err:
	if (ploop_mounted)
		vzctl_umount_image(fs->private, fs->root);
	if (ret != 0)
		del_dir(new_private);
	return ret;
}