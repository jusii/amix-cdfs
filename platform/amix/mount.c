/*
 * /usr/lib/fs/cdfs/mount -- the SVR4 userland mount helper for cdfs, the
 * read-only optical filesystem (the AMIX port of ODFileSystem).
 *
 * SVR4's generic mount(1M) cannot mount anything by itself: it parses its
 * arguments and then execs /usr/lib/fs/<fstype>/mount, handing it the special
 * and the mount point as the LAST TWO positional arguments (any -o options
 * precede them). Without this binary `mount -F cdfs ...` fails outright, so the
 * whole CD stack rests on it. It does exactly two things:
 *
 *   1. mount(2), 6-argument MS_DATA form, fstype "cdfs", always read-only.
 *      The `special` is cdfs's own "<card><sep><unit>" spec -- <sep> is any run
 *      of non-digits, e.g. "0,2" = SCSI host-adapter 0, target 2 -- parsed
 *      kernel-side by cdfs_mount(). It is not a /dev path: a real CD never
 *      traverses /dev/dsk on stock AMIX, so cdfs addresses the drive directly.
 *
 *   2. Append the /etc/mnttab entry. The SVR4 KERNEL does not maintain
 *      /etc/mnttab -- the mount commands do. Omit it and the mounted CD is
 *      invisible to `mount` and `df`, and umount(1M) refuses with "not in
 *      mnttab". The entry is the standard TAB-separated 5-field line:
 *
 *          special \t mountp \t fstype \t options \t time
 *
 * Once the entry exists, the generic umount(1M) works natively and removes it.
 *
 * If the mnttab append fails the filesystem IS still mounted, so we warn and
 * exit 0. Never unmount a working filesystem because bookkeeping failed.
 *
 * PORTABILITY -- load-bearing. This must compile BOTH with the cross toolchain
 * (m68k-cbm-sysv4-gcc 2.7.2.3, `make amix-mount`) and, as the on-box recovery
 * path when an image is rebuilt, with AMIX's own 1991-vintage K&R cc:
 *
 *     cc -O -o /usr/lib/fs/cdfs/mount mount.c
 *
 * Hence: old-style (K&R) function definitions, no prototypes, no C99, and
 * nothing whatsoever from this repo -- no odfs headers, no libodfs, no -I.
 * System headers only, so the file stands alone on a bare box. Keep it so.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/types.h>
#include <stdio.h>		/* must precede <sys/mnttab.h>: its ANSI
				 * getmntent() declaration names FILE */
#include <sys/mount.h>		/* MS_DATA, MS_RDONLY */
#include <sys/mnttab.h>		/* MNTTAB */
#include <fcntl.h>		/* O_WRONLY, O_APPEND, O_CREAT */
#include <errno.h>		/* errno */
#include <string.h>		/* strlen */
#include <stdlib.h>		/* exit */
#include <time.h>		/* time, time_t */
#include <unistd.h>		/* write, close */

#ifndef MNTTAB
#define MNTTAB		"/etc/mnttab"
#endif

#define FSTYPE		"cdfs"	/* vfssw name; must match the kernel's */
#define MNTOPTS		"ro"	/* cdfs is read-only, always */

/*
 * <sys/mount.h> declares mount() under `#if defined(__STDC__)` and offers NO
 * K&R fallback, so a pre-ANSI compiler sees no declaration at all and would
 * default it to int. It does return int, so that is harmless -- but declare it
 * explicitly for that case rather than lean on the implicit-int rule.
 */
#if !defined(__STDC__)
extern int mount();
#endif

/*
 * The mnttab line: 5 TAB-separated fields plus a newline. cdfs_mount() copies
 * in at most 64 bytes of spec and a path is at most PATH_MAX, so this is well
 * clear of the longest line we can emit -- but there is no snprintf() in 1991,
 * so the bound is checked by hand before sprintf() all the same.
 */
#define LINEMAX		2048
#define LINESLACK	64	/* fstype + options + time + tabs + NL */

/*
 * Append this mount to /etc/mnttab. Returns 0, or -1 having warned.
 *
 * The whole line is built first and handed to ONE write(2) on an O_APPEND
 * descriptor, so a concurrent mount cannot interleave with it.
 */
static int
add_mnttab(spec, dir)
	char *spec;
	char *dir;
{
	char	line[LINEMAX];
	int	fd;
	int	len;
	time_t	now;

	if ((int)strlen(spec) + (int)strlen(dir) + LINESLACK > LINEMAX) {
		fprintf(stderr, "cdfs: warning: %s entry too long, not recorded\n",
			MNTTAB);
		return (-1);
	}

	now = time((time_t *)0);
	sprintf(line, "%s\t%s\t%s\t%s\t%ld\n", spec, dir, FSTYPE, MNTOPTS,
		(long)now);
	len = (int)strlen(line);

	fd = open(MNTTAB, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (fd < 0) {
		fprintf(stderr, "cdfs: warning: cannot open %s: errno %d\n",
			MNTTAB, errno);
		return (-1);
	}
	if (write(fd, line, len) != len) {
		fprintf(stderr, "cdfs: warning: cannot write %s: errno %d\n",
			MNTTAB, errno);
		close(fd);
		return (-1);
	}
	close(fd);
	return (0);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *spec;
	char *dir;

	if (argc < 3) {
		fprintf(stderr, "usage: mount -F cdfs special dir\n");
		exit(2);
	}

	/*
	 * mount(1M) may pass options (-o ro, ...) ahead of the two operands;
	 * the special and the mount point are always the LAST TWO arguments.
	 * Anything before them is tolerated and ignored -- cdfs has no tunables,
	 * it is read-only and that is the only mode it has.
	 */
	spec = argv[argc - 2];
	dir  = argv[argc - 1];

	if (mount(spec, dir, MS_DATA | MS_RDONLY, FSTYPE, (char *)0, 0) < 0) {
		fprintf(stderr, "cdfs: mount %s on %s failed: errno %d\n",
			spec, dir, errno);
		exit(1);
	}

	/*
	 * Mounted. Nothing from here on may fail the command: an mnttab problem
	 * is a bookkeeping problem, not a mount failure.
	 */
	(void)add_mnttab(spec, dir);

	exit(0);
	/*NOTREACHED*/
	return (0);
}
