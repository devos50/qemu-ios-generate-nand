"""
Copies files into an HFS+ filesystem image and makes them owned by
root:wheel, without needing sudo.

  python3 add_files.py <image> <source root> <path> [<path> ...]

<image> is a read/write disk image (UDRW .dmg) or a raw HFS+ partition image
(.img). Each <path> is absolute inside the filesystem, e.g.
/System/Library/LaunchDaemons/com.apple.syslogd.plist, and is copied from the
same path below <source root> (e.g. a mounted original filesystem).
Missing parent directories are created and made root-owned as well.
Symbolic links are copied as links.

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
import time

# HFSPlusCatalogFile: recordType, flags, reserved, fileID, 5 dates, then
# HFSPlusBSDInfo with ownerID and groupID.
# HFSPlusCatalogFolder has the same layout up to the BSD info.
FOLDER_RECORD = 1
FILE_RECORD = 2
OWNER_OFFSET = 0x20


def attach(image, mountpoint):
    args = ["hdiutil", "attach", "-nobrowse", "-owners", "off", "-mountpoint", mountpoint]
    if not image.endswith(".dmg"):
        args += ["-imagekey", "diskimage-class=CRawDiskImage"]
    subprocess.run(args + [image], check=True, stdout=subprocess.DEVNULL)


def detach(mountpoint):
    # Spotlight or fseventsd can briefly keep a fresh mount busy.
    for _ in range(5):
        if subprocess.run(["hdiutil", "detach", mountpoint], stdout=subprocess.DEVNULL).returncode == 0:
            return
        time.sleep(1)
    subprocess.run(["hdiutil", "detach", mountpoint], check=True, stdout=subprocess.DEVNULL)


def chown_root(image, entries):
    """entries: list of (name, parent directory node ID, record type)."""
    with open(image, "r+b") as f:
        data = bytearray(f.read())
        for name, parent, record_type in entries:
            uname = name.encode("utf-16-be")
            key = struct.pack(">HIH", 6 + len(uname), parent, len(name)) + uname
            matches = []
            pos = data.find(key)
            while pos != -1:
                rec = pos + len(key)
                if struct.unpack_from(">H", data, rec)[0] == record_type:
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
            if not (os.path.isfile(src) or os.path.islink(src)):
                raise SystemExit("not a file: %s" % src)
            missing = []
            parent = os.path.dirname(dst)
            while not os.path.isdir(parent):
                missing.append(parent)
                parent = os.path.dirname(parent)
            for directory in reversed(missing):
                os.mkdir(directory, 0o755)
                entries.append((os.path.basename(directory),
                                os.stat(os.path.dirname(directory)).st_ino, FOLDER_RECORD))
            if os.path.lexists(dst) and os.path.islink(src):
                os.remove(dst)
            shutil.copy2(src, dst, follow_symlinks=False)
            # Symlinks are file records too.
            entries.append((os.path.basename(path), os.stat(os.path.dirname(dst)).st_ino, FILE_RECORD))
    finally:
        detach(mountpoint)
        os.rmdir(mountpoint)

    chown_root(image, entries)


if __name__ == "__main__":
    main()
