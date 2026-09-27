"""
Copies files into an HFS+ filesystem image and makes them owned by
root:wheel, without needing sudo.

  python3 add_files.py <image> <source root> <path> [<path> ...]

<image> is a read/write disk image (UDRW .dmg) or a raw HFS+ partition image
(.img). Each <path> is absolute inside the filesystem, e.g.
/System/Library/LaunchDaemons/com.apple.syslogd.plist, and is copied from the
same path below <source root> (e.g. a mounted original filesystem).

macOS mounts the image with ownership ignored, so copied files end up owned
by the current user, and launchd skips daemons that are not owned by root.
After unmounting, this script sets the owner and group in each file's HFS+
catalog record to 0. It only touches records whose parent is the destination
directory, which it identifies by its catalog node ID (the inode number).
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# HFSPlusCatalogFile: recordType, flags, reserved, fileID, 5 dates, then
# HFSPlusBSDInfo with ownerID and groupID.
FILE_RECORD = 2
OWNER_OFFSET = 0x20


def attach(image, mountpoint):
    args = ["hdiutil", "attach", "-nobrowse", "-owners", "off", "-mountpoint", mountpoint]
    if not image.endswith(".dmg"):
        args += ["-imagekey", "diskimage-class=CRawDiskImage"]
    subprocess.run(args + [image], check=True, stdout=subprocess.DEVNULL)


def detach(mountpoint):
    subprocess.run(["hdiutil", "detach", mountpoint], check=True, stdout=subprocess.DEVNULL)


def chown_root(image, entries):
    """entries: list of (file name, parent directory node ID)."""
    with open(image, "r+b") as f:
        data = bytearray(f.read())
        for name, parent in entries:
            uname = name.encode("utf-16-be")
            key = struct.pack(">HIH", 6 + len(uname), parent, len(name)) + uname
            matches = []
            pos = data.find(key)
            while pos != -1:
                rec = pos + len(key)
                if struct.unpack_from(">H", data, rec)[0] == FILE_RECORD:
                    matches.append(rec)
                pos = data.find(key, pos + 1)
            # Unused B-tree node space can hold stale copies of a record, so
            # patch every copy rather than guess which one is live.
            if not matches:
                raise SystemExit("no catalog record for %s in directory %d" % (name, parent))
            for rec in matches:
                struct.pack_into(">II", data, rec + OWNER_OFFSET, 0, 0)
            print("%s: root:wheel (%d record%s)" % (name, len(matches), "s" if len(matches) > 1 else ""))
        f.seek(0)
        f.write(data)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    image, source_root, paths = sys.argv[1], sys.argv[2], sys.argv[3:]

    entries = []
    mountpoint = tempfile.mkdtemp(prefix="add_files.")
    attach(image, mountpoint)
    try:
        for path in paths:
            src = os.path.join(source_root, path.lstrip("/"))
            dst = os.path.join(mountpoint, path.lstrip("/"))
            if not os.path.isfile(src):
                raise SystemExit("not a file: %s" % src)
            shutil.copy2(src, dst)
            entries.append((os.path.basename(path), os.stat(os.path.dirname(dst)).st_ino))
    finally:
        detach(mountpoint)
        os.rmdir(mountpoint)

    chown_root(image, entries)


if __name__ == "__main__":
    main()
