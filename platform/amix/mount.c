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
 *      ...where the `special` we record is the MOUNT POINT, not the spec of
 *      (1) above. That is not a typo, and not a detail: get it wrong and
 *      umount(1M) silently fails to unmount. See add_mnttab().
 *
 * Once the entry exists, the generic umount(1M) unmounts the CD natively and
 * removes the entry again. There is no cdfs umount helper -- umount(1M) does
 * not exec one (see add_mnttab()), and none is needed.
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
 * The mnttab line: 5 TAB-separated fields plus a newline. The mount point
 * appears TWICE in it (as the special and as the mountp -- see add_mnttab()),
 * and a path is at most PATH_MAX, so this is well clear of the longest line we
 * can emit -- but there is no snprintf() in 1991, so the bound is checked by
 * hand before sprintf() all the same.
 */
#define LINEMAX		2048
#define LINESLACK	64	/* fstype + options + time + tabs + NL */

/*
 * Append this mount to /etc/mnttab. Returns 0, or -1 having warned.
 *
 * WHY THE `special` FIELD IS THE MOUNT POINT AND NOT THE cdfs SPEC
 * ----------------------------------------------------------------
 * umount(1M) looks its argument up in /etc/mnttab and then unmounts BY THE
 * `special` FIELD of the entry it matched -- not by the mount point it was
 * handed. So recording cdfs's own spec ("0,2") as the special asks the kernel
 * to unmount "0,2", which is not a path and cannot be unmounted -- and yet
 * umount(1M) still reports success and still DELETES the mnttab entry.
 *
 * Measured on the box, with `0,2 /cdrom cdfs ro <time>` in mnttab:
 * `umount /cdrom` -> rc 0, entry gone, filesystem STILL MOUNTED (ls /cdrom went
 * right on listing the CD's root). That is strictly WORSE than having no entry
 * at all: a missing entry merely drew a "not in mnttab" warning, whereas a
 * wrong one makes the bookkeeping lie about reality. Rewriting only that one
 * field to `/cdrom /cdrom cdfs ro <time>` and repeating the identical sequence
 * unmounts for real: rc 0, entry removed, filesystem gone.
 *
 * Using the mount point as the special is the established SVR4 convention for
 * filesystems that have no block device, and the box's own /etc/mnttab shows
 * it:
 *
 *     /proc     /proc     proc  rw  ...
 *     /dev/fd   /dev/fd   fd    rw  ...
 *
 * (spaced out here for legibility; the real fields are TAB-separated)
 *
 * cdfs is in exactly that category: its spec is a <card>,<unit> pair, not a
 * /dev path (a real CD never traverses /dev/dsk on stock AMIX).
 *
 * The cost, stated plainly: `mount` and `df` now show "/cdrom on /cdrom", so
 * the mnttab entry no longer records WHICH drive is mounted -- the 0,2 is gone
 * from the bookkeeping (mount(2) is still given it, and the kernel still knows
 * it). That is an accepted trade for an umount that actually unmounts. It goes
 * away only if cdfs ever grows a real block-device spec; the hook for that,
 * `devvp` in `struct cdfs_vfs` (platform/amix-kernel/cdfs.h), is already there
 * but unused.
 *
 * And a cdfs umount helper is NOT the alternative fix: umount(1M) never execs
 * one. Probed directly -- a script planted at /usr/lib/fs/cdfs/umount was never
 * run, and stock /usr/lib/fs/ufs/ ships no umount either.
 *
 * The whole line is built first and handed to ONE write(2) on an O_APPEND
 * descriptor, so a concurrent mount cannot interleave with it.
 */
static int
add_mnttab(dir)
	char *dir;
{
	char	line[LINEMAX];
	int	fd;
	int	len;
	int	dirlen;
	time_t	now;

	/* `dir` lands in the line twice over: as the special AND as the mountp. */
	dirlen = (int)strlen(dir);
	if (dirlen * 2 + LINESLACK > LINEMAX) {
		fprintf(stderr, "cdfs: warning: %s entry too long, not recorded\n",
			MNTTAB);
		return (-1);
	}

	now = time((time_t *)0);
	sprintf(line, "%s\t%s\t%s\t%s\t%ld\n", dir, dir, FSTYPE, MNTOPTS,
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
	 *
	 * `dir` goes in, NOT `spec`: the mnttab special field is what umount(1M)
	 * unmounts by, so it has to be the mount point. Recording the spec there
	 * makes umount(1M) drop the entry while leaving the CD mounted. This is
	 * the subtle one -- add_mnttab() has the full story.
	 */
	(void)add_mnttab(dir);

	exit(0);
	/*NOTREACHED*/
	return (0);
}
