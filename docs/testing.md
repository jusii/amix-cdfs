# Testing Strategy

## Principles
- Most logic must be testable on host systems
- Every parser/backend must have direct tests
- Every bug fixed becomes a regression test
- Every supported format has golden images

## Test Categories

### Unit Tests (`tests/unit/`)
Test small deterministic components: endian helpers, path parsing, timestamps, directory record parsing, cache logic, logging, charset conversion.

Three suites cover code that cannot otherwise be reached from the host:
`test_remount.c` runs repeated in-process mount/unmount cycles over one image
(the shape the AMIX remount corruption only ever appeared in), and
`test_cdfs_blocks.c` covers the AMIX in-kernel `st_blocks` arithmetic, which is
factored into `platform/amix-kernel/cdfs_blocks.h` for exactly that reason —
`cdfs_vnodeops.c` itself needs SVR4 kernel headers and cannot be host-compiled.
`test_read_span.c` models the in-kernel read path (cdfs_read's 8 KiB staging
loop over a media mock with the AMIX one-sector transfers and page bounce) and
checks that reads around offset 4096 are never short before EOF, at a 2048 and
a 4096 runtime page.

### Golden Image Tests (`tests/golden/`)
For each image, define expected: detected backend(s), volume name, root listing, file metadata, file hashes, timestamps.

The golden suite also includes an optional real-world Amiga `AS` check. It
downloads the small `Arabian Nights` archive from Archive.org on demand,
verifies its MD5, extracts the data track, and skips cleanly if the network or
required extraction tools are unavailable.

### Malformed Image Tests (`tests/malformed/`)
Intentionally broken images to ensure: no crash, no infinite loop, graceful error paths, bounded resource usage.

### Differential Tests
Compare behavior against NetBSD, OpenBSD, Linux, CDVDFS. Differences documented as bug, intentional policy difference, or unsupported legacy quirk.

### Integration Tests (`tests/integration/`)
Test the AmigaDOS-facing handler: mount, list, read, examine, locks, path handling.

### Fuzz Targets (`tests/fuzz/`)
Host-side fuzz targets for each parser backend.

## Running Tests

```
make check    # build and run all unit tests
make golden-check  # run golden image tests with the host tools
make malformed-check  # run malformed-image smoke tests
make fuzz-check  # run parser fuzz smoke tests
make amiga      # release Amiga handler build, serial logging disabled
make amiga-test # test Amiga handler build, serial logging enabled
make adf        # build build/amiga-test/ODFileSystem.adf for emulator testing
make rom        # release ROM-profile build, serial logging disabled
make rom-test   # test ROM-profile build, serial logging enabled
make integration-check  # local-only AmiFUSE test using the amiga-test handler
AMIGAOS32_ISO=/path/to/AmigaOS3.2CD.iso make integration-check  # local-only AmiFUSE handler test
```

`make adf` also places `C/test_handler` on the test disk. It can be used on a
real Amiga to exercise lock/filehandle parent semantics, for example:

```
test_handler CD0:some/deep/directory REL=///sibling
test_handler CD0:CDDA/Track01.wav FILE=CD0:CDDA/Track01.wav
```

The AMIX (SVR4/m68k) arms need the
[gcc-cross-amix](https://github.com/jusii/gcc-cross-amix) toolchain
(`m68k-cbm-sysv4-gcc`) and are **not built in CI**:

```
make amix         # libodfs.a + imginfo/imgls/imgcat + the SVR4 mount helper
make amix-lib     # just build/amix/libodfs.a
make amix-tools   # just the three img* tools
make amix-mount   # just build/amix/mount, the /usr/lib/fs/cdfs/mount helper
make amix-kernel  # build/amix-kernel/libcdfskern.o, the in-kernel filesystem
```

Pass `AMIX_CROSS_PREFIX=/path/to/amix-cross` if the toolchain is not under
`/opt/amix-cross`. See the README's "Building for AMIX (SVR4/m68k)" chapter.

## CI Requirements
Per push: build, unit tests, golden image tests, malformed-image tests, parser fuzz smoke tests, Amiga handler build, ROM-profile build, static checks, warnings-as-errors.

Host baseline: `make check` is **24 suites, 121 tests, 0 failures** (measured
2026-10-02 with `test_read_span.c` added). Record the new number here whenever a suite is added,
so drift is detectable rather than merely surprising.

The five AMIX targets are outside CI — no `m68k-cbm-sysv4` cross toolchain is
available to the workflow, which builds the host, Amiga and ROM arms only. The
AMIX arms are validated out of band instead: cross-build, then a bench cycle on
the emulated box, then metal (A4000 + Z3660) for anything that changes the
mount, read or DMA path. What that has and has not covered is recorded in
[amix-known-unknowns.md](amix-known-unknowns.md) — ISO 9660 with Rock Ridge is
proven end to end there; High Sierra, Joliet, UDF, HFS, HFS+ and CDDA compile
into both AMIX arms but have not been.
