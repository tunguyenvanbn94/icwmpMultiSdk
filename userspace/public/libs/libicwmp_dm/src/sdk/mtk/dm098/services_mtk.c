/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Services. -- ported from
 *	functions/tr098/services_storage_service.
 *
 *	STBService.1. -- placeholders: ServiceMonitoring.ServiceType "default",
 *	Enable "false" and the MainStream.1.Total counters at 0.  STBService.1
 *	and MainStream.1 are fixed instances, as in the shell.
 *
 *	StorageService.{i} -- one per whole disk under /sys/class/block whose
 *	name looks like sd?, hd?, nvme*n*, mmcblk* (in name order); a single
 *	instance 1 standing for "no disk" when there is none.
 *	  Enable          a logical volume of the disk is mounted.  A set to true
 *	                  mounts every unmounted volume on its registered
 *	                  /tmp/mnt/usb<n> or the first free one and registers it
 *	                  in mediashare (disk, dev_path, mount_path, LABEL as
 *	                  name); a volume already mounted elsewhere than
 *	                  /tmp/mnt/usb<n> is E_INTERNAL_ERROR.  A set to false
 *	                  unmounts every volume, removes the mount directories
 *	                  and the mediashare entries.  Done inside the setter, as
 *	                  the shell did (the result is the setter's answer)
 *	  PhysicalMediumNumberOfEntries  1 for an existing disk
 *	  LogicalVolumeNumberOfEntries, UserAccountNumberOfEntries (the
 *	                  /etc/passwd lines not starting with "#")
 *	  Capabilities.*  what is installed: vsftpd/proftpd, sshd/sftp-server,
 *	                  lighttpd/nginx/uhttpd, cryptsetup + device-mapper; the
 *	                  non-nodev /proc/filesystems types
 *	  LogicalVolume.{i}  the disk's partitions, or the disk itself when it
 *	                  has none:
 *	    Enable        mounted; true mounts an unmounted one on /mnt/<part>,
 *	                  false unmounts (failures ignored, as in the shell)
 *	    Name          the LABEL, else the partition name; a set relabels with
 *	                  the tool of the filesystem (tune.exfat, fatlabel,
 *	                  ntfslabel, tune2fs), unmounting and remounting around
 *	                  it, and names mediashare.@usb[0] after it
 *	    Status        Error / Online / Offline
 *	    FileSystem    blkid TYPE, "Unknown"
 *	    Capacity, UsedSpace  in MB (512-byte sectors; used blocks of the
 *	                  mounted filesystem); xsd:string on the wire as through
 *	                  the shell bridge, the engine has no xsd:unsignedLong
 *	    Encrypted     "cryptsetup isLuks"
 *	    PhysicalReference  the disk name of the partition
 *	    FolderNumberOfEntries  directories at depth 0..1 of its mount point
 *	                  minus 2 (the shell's count, -1 for an empty volume)
 *	AddObject/DeleteObject answer 9005 on both objects, as the shell did.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define SS_NONE		"__none__"
#define SS_MEDIASHARE	"mediashare"

/* ------------------------------------------------------------------ */
/* STBService                                                          */
/* ------------------------------------------------------------------ */

static int get_stb_default(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "default";
	return 0;
}

static int get_stb_false(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "false";
	return 0;
}

static int get_stb_zero(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0";
	return 0;
}

/* ------------------------------------------------------------------ */
/* disks and volumes                                                   */
/* ------------------------------------------------------------------ */

static int ss_supported(const char *name)
{
	return !fnmatch("sd[a-z]", name, 0) || !fnmatch("hd[a-z]", name, 0) ||
	       !fnmatch("nvme[0-9]*n[0-9]*", name, 0) || !fnmatch("mmcblk[0-9]*", name, 0);
}

static int ss_exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

static int ss_is_block(const char *name)
{
	char path[128];
	struct stat st;

	snprintf(path, sizeof(path), "/dev/%s", name);
	return stat(path, &st) == 0 && S_ISBLK(st.st_mode);
}

/* <dir>/<entry>/partition exists */
static int ss_is_partition(const char *dir, const char *entry)
{
	char path[256];

	snprintf(path, sizeof(path), "%s/%s/partition", dir, entry);
	return ss_exists(path);
}

/* storage_service_list_devices, in glob (name) order; NULL-terminated,
 * dm-allocated */
static char **ss_disks(int *count)
{
	struct dirent **ents = NULL;
	char **out;
	int n, i, k = 0;

	*count = 0;
	n = scandir("/sys/class/block", &ents, NULL, alphasort);
	if (n < 0)
		return NULL;
	out = dmcalloc(n + 1, sizeof(char *));
	for (i = 0; i < n; i++) {
		const char *name = ents[i]->d_name;

		if (out && name[0] != '.' && !ss_is_partition("/sys/class/block", name) && ss_supported(name))
			out[k++] = dmstrdup(name);
		free(ents[i]);
	}
	free(ents);
	*count = k;
	return out;
}

/* storage_service_list_logical_volumes: the partitions in /sys/block/<disk>,
 * else the disk itself when /dev/<disk> is a block device */
static char **ss_volumes(const char *disk, int *count)
{
	struct dirent **ents = NULL;
	char dir[128], **out;
	int n, i, k = 0;

	*count = 0;
	if (!disk || strcmp(disk, SS_NONE) == 0)
		return NULL;
	snprintf(dir, sizeof(dir), "/sys/block/%s", disk);
	n = scandir(dir, &ents, NULL, alphasort);
	if (n < 0)
		return NULL;
	out = dmcalloc(n + 2, sizeof(char *));
	for (i = 0; i < n; i++) {
		const char *name = ents[i]->d_name;

		if (out && name[0] != '.' && ss_is_partition(dir, name))
			out[k++] = dmstrdup(name);
		free(ents[i]);
	}
	free(ents);
	if (out && k == 0 && ss_is_block(disk))
		out[k++] = dmstrdup(disk);
	*count = k;
	return out;
}

/* storage_service_get_mountpoint: the first /proc/mounts line of /dev/<p> */
static char *ss_mountpoint(const char *part)
{
	FILE *f = fopen("/proc/mounts", "r");
	char src[256], mp[512], want[160];

	if (!f)
		return NULL;
	snprintf(want, sizeof(want), "/dev/%s", part);
	while (fscanf(f, "%255s %511s%*[^\n]", src, mp) == 2) {
		if (strcmp(src, want) == 0) {
			fclose(f);
			return dmstrdup(mp);
		}
	}
	fclose(f);
	return NULL;
}

static int ss_run(char *const argv[])
{
	return mtk_run(argv);
}

static char *ss_blkid(const char *part, const char *tag)
{
	char dev[160];
	char *argv[] = { "blkid", "-s", (char *)tag, "-o", "value", dev, NULL };

	snprintf(dev, sizeof(dev), "/dev/%s", part);
	return mtk_exec_line(argv);
}

/* ------------------------------------------------------------------ */
/* mediashare                                                          */
/* ------------------------------------------------------------------ */

/* storage_service_find_usb_section_idx: the @usb[<i>] whose dev_path is
 * /dev/<part>, -1 when none */
static int ss_usb_idx(const char *part)
{
	char sec[32], want[160];
	int i;

	snprintf(want, sizeof(want), "/dev/%s", part);
	for (i = 0; i < 64; i++) {
		char *t = NULL;

		snprintf(sec, sizeof(sec), "@usb[%d]", i);
		dmuci_get_section_type(SS_MEDIASHARE, sec, &t);
		if (!t || strcmp(t, "usb") != 0)
			return -1;
		if (strcmp(mtk_uci(SS_MEDIASHARE, sec, "dev_path"), want) == 0)
			return i;
	}
	return -1;
}

/* storage_service_get_usb_mountpoint: a registered /tmp/mnt/usb<n> */
static char *ss_usb_mountpoint(const char *part)
{
	char sec[32], *mp;
	int i = ss_usb_idx(part);

	if (i < 0)
		return NULL;
	snprintf(sec, sizeof(sec), "@usb[%d]", i);
	mp = mtk_uci(SS_MEDIASHARE, sec, "mount_path");
	return !fnmatch("/tmp/mnt/usb[0-9]*", mp, 0) ? mp : NULL;
}

/* storage_service_alloc_usb_mountpoint */
static char *ss_alloc_mountpoint(void)
{
	char path[64];
	int i;

	for (i = 1; i < 1000; i++) {
		snprintf(path, sizeof(path), "/tmp/mnt/usb%d", i);
		if (!ss_exists(path))
			return dmstrdup(path);
	}
	return NULL;
}

/* storage_service_register_usb */
static int ss_register(const char *disk, const char *part, const char *mp)
{
	struct uci_section *s = NULL;
	char sec[32], dev[160], *name = NULL, *label;
	int i = ss_usb_idx(part);

	snprintf(dev, sizeof(dev), "/dev/%s", part);
	label = ss_blkid(part, "LABEL");
	if (i >= 0) {
		snprintf(sec, sizeof(sec), "@usb[%d]", i);
		dmuci_set_value(SS_MEDIASHARE, sec, "disk", (char *)disk);
		dmuci_set_value(SS_MEDIASHARE, sec, "dev_path", dev);
		dmuci_set_value(SS_MEDIASHARE, sec, "mount_path", (char *)mp);
		dmuci_set_value(SS_MEDIASHARE, sec, "name", label);
		return 0;
	}
	dmuci_add_section(SS_MEDIASHARE, "usb", &s, &name);
	if (!s)
		return -1;
	dmuci_set_value_by_section(s, "disk", (char *)disk);
	dmuci_set_value_by_section(s, "dev_path", dev);
	dmuci_set_value_by_section(s, "mount_path", (char *)mp);
	dmuci_set_value_by_section(s, "name", label);
	return 0;
}

/* storage_service_unregister_usb: every entry of the partition */
static void ss_unregister(const char *part)
{
	char sec[32];
	int i, guard = 0;

	while ((i = ss_usb_idx(part)) >= 0 && guard++ < 64) {
		snprintf(sec, sizeof(sec), "@usb[%d]", i);
		dmuci_delete(SS_MEDIASHARE, sec, NULL, NULL);
	}
}

/* ------------------------------------------------------------------ */
/* StorageService.{i}                                                  */
/* ------------------------------------------------------------------ */

#define SS_MAX	16

static int browse_ss(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char **disks, *idx, *idx_last = NULL;
	int n, i;

	disks = ss_disks(&n);
	if (n == 0) {
		idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, 1);
		DM_LINK_INST_OBJ(dmctx, parent_node, (void *)SS_NONE, idx);
		return 0;
	}
	for (i = 0; i < n && i < SS_MAX; i++) {
		idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)disks[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

static int add_ss_none(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	return FAULT_9005;	/* add_storage_service_instance / add_logical_volume */
}

static int del_ss_none(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	return FAULT_9005;
}

#define SS_DISK(data)	((const char *)(data))

static int get_ss_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char **vols;
	int n, i;

	*value = "false";
	if (!ss_is_block(SS_DISK(data)))
		return 0;
	vols = ss_volumes(SS_DISK(data), &n);
	for (i = 0; i < n; i++) {
		if (ss_mountpoint(vols[i])) {
			*value = "true";
			break;
		}
	}
	return 0;
}

static int ss_enable_on(const char *disk)
{
	char **vols, *mp, dev[160];
	int n, i;

	vols = ss_volumes(disk, &n);
	for (i = 0; i < n; i++) {
		mp = ss_mountpoint(vols[i]);
		if (mp) {
			if (fnmatch("/tmp/mnt/usb[0-9]*", mp, 0) != 0)
				return FAULT_9002;
			if (ss_register(disk, vols[i], mp) != 0)
				return FAULT_9002;
			continue;
		}
		mp = ss_usb_mountpoint(vols[i]);
		if (!mp)
			mp = ss_alloc_mountpoint();
		if (!mp)
			return FAULT_9002;
		{
			char *mk[] = { "mkdir", "-p", mp, NULL };

			if (ss_run(mk) != 0)
				return FAULT_9002;
		}
		snprintf(dev, sizeof(dev), "/dev/%s", vols[i]);
		{
			char *mnt[] = { "mount", dev, mp, NULL };

			if (ss_run(mnt) != 0) {
				rmdir(mp);
				return FAULT_9002;
			}
		}
		if (ss_register(disk, vols[i], mp) != 0) {
			char *um[] = { "umount", dev, NULL };

			ss_run(um);
			rmdir(mp);
			return FAULT_9002;
		}
	}
	return 0;
}

static int ss_enable_off(const char *disk)
{
	char **vols, *mp, *reg, dev[160];
	int n, i;

	vols = ss_volumes(disk, &n);
	for (i = 0; i < n; i++) {
		reg = ss_usb_mountpoint(vols[i]);
		mp = ss_mountpoint(vols[i]);
		if (mp) {
			char *um[] = { "umount", dev, NULL };

			snprintf(dev, sizeof(dev), "/dev/%s", vols[i]);
			if (ss_run(um) != 0)
				return FAULT_9002;
			rmdir(mp);
		}
		if (reg)
			rmdir(reg);
		ss_unregister(vols[i]);
	}
	return 0;
}

static int set_ss_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int on;

	if (!ss_is_block(SS_DISK(data)))
		return FAULT_9007;
	if (!strcmp(value, "true") || !strcmp(value, "1"))
		on = 1;
	else if (!strcmp(value, "false") || !strcmp(value, "0"))
		on = 0;
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return on ? ss_enable_on(SS_DISK(data)) : ss_enable_off(SS_DISK(data));
}

static char *ss_num(long long v)
{
	char *s = NULL;

	dmasprintf(&s, "%lld", v);
	return s ? s : "0";
}

static int get_ss_physical(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char dir[128];

	snprintf(dir, sizeof(dir), "/sys/block/%s", SS_DISK(data));
	*value = (ss_is_block(SS_DISK(data)) && ss_exists(dir)) ? "1" : "0";
	return 0;
}

static int get_ss_volume_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int n;

	ss_volumes(SS_DISK(data), &n);
	*value = ss_num(n);
	return 0;
}

/* grep -c "^[^#]" /etc/passwd */
static int get_ss_users(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	FILE *f = fopen("/etc/passwd", "r");
	char line[512];
	int n = 0;

	if (f) {
		while (fgets(line, sizeof(line), f)) {
			if (line[0] && line[0] != '#' && line[0] != '\n')
				n++;
		}
		fclose(f);
	}
	*value = ss_num(n);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

static int ss_file(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int get_ss_ftp(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (ss_file("/usr/sbin/vsftpd") || ss_file("/usr/sbin/proftpd")) ? "true" : "false";
	return 0;
}

static int get_ss_sftp(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (ss_file("/usr/sbin/sshd") || ss_file("/usr/libexec/sftp-server")) ? "true" : "false";
	return 0;
}

static int get_ss_http(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (ss_file("/usr/sbin/lighttpd") || ss_file("/usr/sbin/nginx") || ss_file("/usr/sbin/uhttpd")) ?
		 "true" : "false";
	return 0;
}

static int get_ss_protocols(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char out[64] = "";

	if (ss_file("/usr/sbin/smbd"))
		strcat(out, "SMB,");
	if (ss_file("/usr/sbin/vsftpd"))
		strcat(out, "FTP,");
	if (ss_file("/usr/sbin/sshd"))
		strcat(out, "SFTP,");
	if (ss_file("/usr/sbin/uhttpd"))
		strcat(out, "HTTP,");
	if (*out)
		out[strlen(out) - 1] = '\0';
	*value = *out ? dmstrdup(out) : "None";
	return 0;
}

/* grep -v nodev /proc/filesystems | awk '{print $1}', comma joined */
static int get_ss_filesystems(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	FILE *f = fopen("/proc/filesystems", "r");
	char line[128], word[64], out[1024] = "";

	if (!f) {
		*value = "ext4,ext3,ext2,vfat,ntfs,exfat";
		return 0;
	}
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "nodev") || sscanf(line, "%63s", word) != 1)
			continue;
		if (*out)
			strncat(out, ",", sizeof(out) - strlen(out) - 1);
		strncat(out, word, sizeof(out) - strlen(out) - 1);
	}
	fclose(f);
	*value = dmstrdup(out);
	return 0;
}

/* "command -v cryptsetup" */
static int ss_have_cryptsetup(void)
{
	char *argv[] = { "/bin/sh", "-c", "command -v cryptsetup", NULL };

	return mtk_run(argv) == 0;
}

static int ss_grep(const char *path, const char *needle)
{
	FILE *f = fopen(path, "r");
	char line[512];
	int found = 0;

	if (!f)
		return 0;
	while (!found && fgets(line, sizeof(line), f))
		found = strstr(line, needle) != NULL;
	fclose(f);
	return found;
}

static int get_ss_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct stat st;

	if (!ss_have_cryptsetup())
		*value = "false";
	else if (ss_grep("/proc/devices", "device-mapper") && ss_grep("/proc/crypto", "aes"))
		*value = "true";
	else
		*value = (stat("/sys/class/misc/device-mapper", &st) == 0 && S_ISDIR(st.st_mode)) ? "true" : "false";
	return 0;
}

/* ------------------------------------------------------------------ */
/* LogicalVolume.{i}                                                   */
/* ------------------------------------------------------------------ */

static int browse_lv(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char **vols, *idx, *idx_last = NULL;
	int n, i;

	vols = ss_volumes(SS_DISK(prev_data), &n);
	for (i = 0; i < n; i++) {
		idx = handle_update_instance(2, dmctx, &idx_last, update_instance_without_section, 1, i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)vols[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

#define LV_PART(data)	((const char *)(data))

static int get_lv_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ss_mountpoint(LV_PART(data)) ? "true" : "false";
	return 0;
}

static int set_lv_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char dev[160], mp[160];
	int on;

	if (!strcmp(value, "true") || !strcmp(value, "1"))
		on = 1;
	else if (!strcmp(value, "false") || !strcmp(value, "0"))
		on = 0;
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	snprintf(dev, sizeof(dev), "/dev/%s", LV_PART(data));
	if (on && !ss_mountpoint(LV_PART(data))) {
		char *mk[] = { "mkdir", "-p", mp, NULL };
		char *mnt[] = { "mount", dev, mp, NULL };

		snprintf(mp, sizeof(mp), "/mnt/%s", LV_PART(data));
		ss_run(mk);
		ss_run(mnt);
	} else if (!on && ss_mountpoint(LV_PART(data))) {
		char *um[] = { "umount", dev, NULL };

		ss_run(um);
	}
	return 0;
}

static int get_lv_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *label = ss_blkid(LV_PART(data), "LABEL");

	*value = *label ? label : (char *)LV_PART(data);
	return 0;
}

/* set_volume_label */
static int set_lv_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *part = LV_PART(data);
	char dev[160], *mp, *fstype;
	int rc;

	if (action == VALUECHECK)
		return 0;
	if (!ss_is_block(part))
		return FAULT_9002;
	snprintf(dev, sizeof(dev), "/dev/%s", part);
	/* label tools want the filesystem to themselves */
	mp = ss_mountpoint(part);
	if (mp) {
		char *um[] = { "umount", dev, NULL };

		if (ss_run(um) != 0)
			return FAULT_9002;
	}
	fstype = ss_blkid(part, "TYPE");
	if (!strcmp(fstype, "exfat")) {
		char *argv[] = { "tune.exfat", "-L", value, dev, NULL };

		rc = ss_run(argv);
	} else if (!strcmp(fstype, "vfat") || !strcmp(fstype, "fat")) {
		char *argv[] = { "fatlabel", dev, value, NULL };

		rc = ss_run(argv);
	} else if (!strcmp(fstype, "ntfs")) {
		char *argv[] = { "ntfslabel", dev, value, NULL };

		rc = ss_run(argv);
	} else if (!strcmp(fstype, "ext2") || !strcmp(fstype, "ext3") || !strcmp(fstype, "ext4")) {
		char *argv[] = { "tune2fs", "-L", value, dev, NULL };

		rc = ss_run(argv);
	} else {
		return FAULT_9002;	/* the shell left it unmounted here too */
	}
	/* the previous mount state comes back even when relabelling failed */
	if (mp) {
		char *mnt[] = { "mount", dev, mp, NULL };

		if (ss_run(mnt) != 0)
			return FAULT_9002;
	}
	if (rc != 0)
		return FAULT_9007;
	/* the product has one USB port: mediashare.@usb[0] */
	{
		char *t = NULL;

		dmuci_get_section_type(SS_MEDIASHARE, "@usb[0]", &t);
		if (t && *t)
			dmuci_set_value(SS_MEDIASHARE, "@usb[0]", "name", value);
	}
	return 0;
}

static int get_lv_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	if (!ss_is_block(LV_PART(data)))
		*value = "Error";
	else
		*value = ss_mountpoint(LV_PART(data)) ? "Online" : "Offline";
	return 0;
}

static int get_lv_fs(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *t = ss_blkid(LV_PART(data), "TYPE");

	*value = *t ? t : "Unknown";
	return 0;
}

static int get_lv_capacity(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char path[160], *blocks;

	*value = "0";
	if (!ss_is_block(LV_PART(data)))
		return 0;
	snprintf(path, sizeof(path), "/sys/class/block/%s/size", LV_PART(data));
	blocks = mtk_file_line(path);
	if (*blocks)
		*value = ss_num(strtoll(blocks, NULL, 10) * 512 / 1048576);
	return 0;
}

/* df -kP <mp>: the used 1K blocks, in MB */
static int get_lv_used(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *mp = ss_mountpoint(LV_PART(data));
	struct statvfs sv;

	if (!mp || statvfs(mp, &sv) != 0) {
		*value = "0";
		return 0;
	}
	*value = ss_num((long long)((sv.f_blocks - sv.f_bfree) * (unsigned long long)sv.f_frsize / 1024) / 1024);
	return 0;
}

static int get_lv_encrypted(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char dev[160];
	char *argv[] = { "cryptsetup", "isLuks", dev, NULL };

	snprintf(dev, sizeof(dev), "/dev/%s", LV_PART(data));
	*value = (ss_have_cryptsetup() && ss_run(argv) == 0) ? "true" : "false";
	return 0;
}

static int get_lv_physref(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *p = LV_PART(data);
	char *out = dmstrdup(p);
	size_t l;

	if (!out) {
		*value = "";
		return 0;
	}
	l = strlen(out);
	if (!fnmatch("dm-[0-9]*", p, 0)) {
		/* as is */
	} else if (!fnmatch("nvme[0-9]*n[0-9]*p[0-9]*", p, 0) || !fnmatch("mmcblk[0-9]*p[0-9]*", p, 0)) {
		/* ${partition%p[0-9]*}: the shortest "p<digit>..." suffix */
		while (l > 0) {
			l--;
			if (out[l] == 'p' && out[l + 1] >= '0' && out[l + 1] <= '9') {
				out[l] = '\0';
				break;
			}
		}
	} else {
		while (l > 0 && out[l - 1] >= '0' && out[l - 1] <= '9')
			out[--l] = '\0';
	}
	*value = out;
	return 0;
}

/* find <mp> -maxdepth 1 -type d | wc -l, minus 2 */
static int get_lv_folders(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *mp = ss_mountpoint(LV_PART(data));
	DIR *d;
	struct dirent *de;
	int n = 1;	/* <mp> itself */

	if (!mp) {
		*value = "0";
		return 0;
	}
	d = opendir(mp);
	if (d) {
		while ((de = readdir(d)) != NULL) {
			char path[1024];
			struct stat st;

			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			snprintf(path, sizeof(path), "%s/%s", mp, de->d_name);
			if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
				n++;
		}
		closedir(d);
	}
	*value = ss_num(n - 2);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tStbVideoParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"DecodedFrames", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{"LostFrames", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{0}
};

static DMLEAF tStbRtpParams[] = {
{"PacketsLost", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{"LossEvents", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{0}
};

static DMLEAF tStbTsParams[] = {
{"TSPacketsReceived", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{"PacketDiscontinuityCounter", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{0}
};

static DMLEAF tStbDejitterParams[] = {
{"Underruns", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{"Overruns", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{0}
};

static DMLEAF tStbAudioParams[] = {
{"DecodedFrames", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{"DecodingErrors", &DMREAD, DMT_UNINT, get_stb_zero, NULL, NULL, NULL},
{0}
};

static DMOBJ tStbTotalObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"VideoDecoderStats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tStbVideoParams, NULL},
{"RTPStats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tStbRtpParams, NULL},
{"MPEG2TSStats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tStbTsParams, NULL},
{"DejitteringStats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tStbDejitterParams, NULL},
{"AudioDecoderStats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tStbAudioParams, NULL},
{0}
};

static DMOBJ tStbMainInstObj[] = {
{"Total", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tStbTotalObj, NULL, NULL},
{0}
};

static DMOBJ tStbMainObj[] = {
{"1", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tStbMainInstObj, NULL, NULL},
{0}
};

static DMOBJ tStbMonitoringObj[] = {
{"MainStream", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tStbMainObj, NULL, NULL},
{0}
};

static DMLEAF tStbMonitoringParams[] = {
{"ServiceType", &DMREAD, DMT_STRING, get_stb_default, NULL, NULL, NULL},
{"Enable", &DMREAD, DMT_BOOL, get_stb_false, NULL, NULL, NULL},
{0}
};

static DMOBJ tStbInstChildObj[] = {
{"ServiceMonitoring", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tStbMonitoringObj, tStbMonitoringParams, NULL},
{0}
};

static DMOBJ tStbObj[] = {
{"1", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tStbInstChildObj, NULL, NULL},
{0}
};

static DMLEAF tLvParams[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_lv_enable, set_lv_enable, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_lv_name, set_lv_name, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_lv_status, NULL, NULL, NULL},
{"FileSystem", &DMREAD, DMT_STRING, get_lv_fs, NULL, NULL, NULL},
{"Capacity", &DMREAD, DMT_STRING, get_lv_capacity, NULL, NULL, NULL},
{"UsedSpace", &DMREAD, DMT_STRING, get_lv_used, NULL, NULL, NULL},
{"Encrypted", &DMREAD, DMT_BOOL, get_lv_encrypted, NULL, NULL, NULL},
{"PhysicalReference", &DMREAD, DMT_STRING, get_lv_physref, NULL, NULL, NULL},
{"FolderNumberOfEntries", &DMREAD, DMT_UNINT, get_lv_folders, NULL, NULL, NULL},
{0}
};

static DMLEAF tSsCapParams[] = {
{"FTPCapable", &DMREAD, DMT_BOOL, get_ss_ftp, NULL, NULL, NULL},
{"SFTPCapable", &DMREAD, DMT_BOOL, get_ss_sftp, NULL, NULL, NULL},
{"HTTPCapable", &DMREAD, DMT_BOOL, get_ss_http, NULL, NULL, NULL},
{"HTTPSCapable", &DMREAD, DMT_BOOL, get_ss_http, NULL, NULL, NULL},
{"HTTPWritable", &DMREAD, DMT_BOOL, get_stb_false, NULL, NULL, NULL},
{"SupportedNetworkProtocols", &DMREAD, DMT_STRING, get_ss_protocols, NULL, NULL, NULL},
{"SupportedFileSystemTypes", &DMREAD, DMT_STRING, get_ss_filesystems, NULL, NULL, NULL},
{"VolumeEncryptionCapable", &DMREAD, DMT_BOOL, get_ss_encryption, NULL, NULL, NULL},
{0}
};

static DMOBJ tSsChildObj[] = {
{"Capabilities", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tSsCapParams, NULL},
{"LogicalVolume", &DMWRITE, add_ss_none, del_ss_none, NULL, browse_lv, NULL, NULL, NULL, tLvParams, NULL},
{0}
};

static DMLEAF tSsParams[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_ss_enable, set_ss_enable, NULL, NULL},
{"PhysicalMediumNumberOfEntries", &DMREAD, DMT_UNINT, get_ss_physical, NULL, NULL, NULL},
{"LogicalVolumeNumberOfEntries", &DMREAD, DMT_UNINT, get_ss_volume_count, NULL, NULL, NULL},
{"UserAccountNumberOfEntries", &DMREAD, DMT_UNINT, get_ss_users, NULL, NULL, NULL},
{0}
};

static DMOBJ tServicesObj[] = {
{"StorageService", &DMWRITE, add_ss_none, del_ss_none, NULL, browse_ss, NULL, NULL, tSsChildObj, tSsParams, NULL},
{"STBService", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tStbObj, NULL, NULL},
{0}
};

static DMOBJ tServicesRoot[] = {
{"Services", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tServicesObj, NULL, NULL},
{0}
};

static const char *const services_mtk_paths[] = {
	"InternetGatewayDevice.Services.",
	NULL
};

static const struct dm_module services_mtk_module = {
	.name  = "mtk-services",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tServicesRoot,
	.paths = services_mtk_paths,
};
DM_MODULE_REGISTER(services_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): StorageService.{i} with the same
 * tables under Device. (type A of docs/plan/tr181_mtk_design.md).  The
 * STBService.1 placeholder (fixed values, no set-top box on the product) is
 * not in the TR-181 tree (T7). */
static DMOBJ tServices181Obj[] = {
{"StorageService", &DMWRITE, add_ss_none, del_ss_none, NULL, browse_ss, NULL, NULL, tSsChildObj, tSsParams, NULL},
{0}
};

static DMOBJ tServices181Root[] = {
{"Services", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tServices181Obj, NULL, NULL},
{0}
};

static const char *const services_mtk_paths181[] = {
	"Device.Services.",
	NULL
};

static const struct dm_module services_mtk_module181 = {
	.name  = "mtk-services-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tServices181Root,
	.paths = services_mtk_paths181,
};
DM_MODULE_REGISTER(services_mtk_module181);
