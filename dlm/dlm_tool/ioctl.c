/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_tool.h"

#define PROC_MISC               "/proc/misc"
#define PROC_DEVICES            "/proc/devices"
#define MISC_NAME               "misc"
#define DLM_MEMBER_CONTROL_DIR  "/dev/misc"
#define DLM_MEMBER_CONTROL_NAME "dlm-member"

int control_fd = -1;

static int get_proc_number(const char *file, const char *name, uint32_t *number)
{
	FILE *fl;
	char nm[256];
	int c;

	if (!(fl = fopen(file, "r"))) {
		die("%s: fopen failed: %s", file, strerror(errno));
		return 0;
	}

	while (!feof(fl)) {
		if (fscanf(fl, "%d %255s\n", number, &nm[0]) == 2) {
			if (!strcmp(name, nm)) {
				fclose(fl);
				return 1;
			}
		} else do {
			c = fgetc(fl);
		} while (c != EOF && c != '\n');
	}
	fclose(fl);

	log_error("%s: No entry for %s found", file, name);
	return 0;
}

static int control_device_number(uint32_t *major, uint32_t *minor)
{
	if (!get_proc_number(PROC_DEVICES, MISC_NAME, major) ||
	    !get_proc_number(PROC_MISC, DLM_MEMBER_MISC_NAME, minor)) {
		*major = 0;
		return 0;
	}

	return 1;
}

/*
 * Returns 1 if exists; 0 if it doesn't; -1 if it's wrong
 */
static int control_exists(const char *control, uint32_t major, uint32_t minor)
{
	struct stat buf;

	if (stat(control, &buf) < 0) {
		if (errno != ENOENT)
			log_error("%s: stat failed: %s", control,
				  strerror(errno));
		return 0;
	}

	if (!S_ISCHR(buf.st_mode)) {
		log_error("%s: Wrong inode type", control);
		if (!unlink(control))
			return 0;
		log_error("%s: unlink failed: %s", control, strerror(errno));
		return -1;
	}

	if (major && buf.st_rdev != makedev(major, minor)) {
		log_error("%s: Wrong device number: (%u, %u) instead of "
			  "(%u, %u)", control, major(buf.st_mode),
			  minor(buf.st_mode), major, minor);
		if (!unlink(control))
			return 0;
		log_error("%s: unlink failed: %s", control, strerror(errno));
		return -1;
	}

	return 1;
}

static int create_control(const char *control, uint32_t major, uint32_t minor)
{
	int ret;
	mode_t old_umask;

	if (!major)
		return 0;

	old_umask = umask(0022);
	ret = mkdir(DLM_MEMBER_CONTROL_DIR, 0777);
	umask(old_umask);
	if (ret < 0 && errno != EEXIST) {
		log_error("%s: mkdir failed: %s", DLM_MEMBER_CONTROL_DIR,
			  strerror(errno));
		return 0;
	}

	if (mknod(control, S_IFCHR | S_IRUSR | S_IWUSR, makedev(major, minor)) < 0) {
		log_error("%s: mknod failed: %s", control, strerror(errno));
		return 0;
	}

	return 1;
}

void open_control(void)
{
	char control[PATH_MAX];
	uint32_t major = 0, minor = 0;

	if (control_fd != -1)
		return;

	snprintf(control, sizeof(control), "%s/%s",
		 DLM_MEMBER_CONTROL_DIR, DLM_MEMBER_CONTROL_NAME);

	if (!control_device_number(&major, &minor))
		die("Is dlm missing from kernel?");

	if (!control_exists(control, major, minor) &&
	    !create_control(control, major, minor))
		die("Failure to communicate with kernel dlm");

	control_fd = open(control, O_RDWR);
	if (control_fd < 0) 
		die("Failure to communicate with kernel dlm: %s",
		    strerror(errno));
}

int do_command(struct dlm_member_ioctl *mi)
{
	int error;

	open_control();

	printf("do_command %s data_size %d\n", mi->op, mi->data_size);

	error = ioctl(control_fd, DLM_MEMBER_OP, mi);

	if (error)
		printf("ioctl error %d errno %d\n", error, errno);

	return error;
}

