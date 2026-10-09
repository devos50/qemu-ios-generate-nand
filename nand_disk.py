"""
Takes the disk out of a NAND image and puts it back, so that the filesystems
on a NAND that iOS wrote itself (e.g. after a restore) can be changed on the
host.

  python3 nand_disk.py extract <nand> <dir>
  python3 nand_disk.py build <dir> <new nand>

extract reads every data page of <nand> and writes the disk the FTL presents
to <dir>/disk.img (logical page n at offset n * 4096), plus each partition of
its MBR to <dir>/partition<n>.img. All of them are sparse: pages that were
never written are holes. A partition image can be changed with add_files.py
or mounted with hdiutil (-imagekey diskimage-class=CRawDiskImage).

build copies the partition images back into disk.img and writes a new NAND
from it with `generate_nand --disk`. The new NAND has a fresh VFL and FTL
layout, with every logical block that holds data written in place.

The FTL stores the logical page number of each data page in its spare area,
with the write age of the block it is in. Logical pages can have several
copies (in log blocks and in blocks that were not erased yet); the newest is
the one in the youngest block, and within a block the one written last.
"""
import argparse
import os
import struct
import subprocess
import sys

BYTES_PER_PAGE = 4096
PAGES_PER_BLOCK = 128
NUM_CE = 4
NUM_BANKS = 8
PAGES_PER_SUBLOCK = PAGES_PER_BLOCK * NUM_BANKS
SPARE_TYPE_DATA = (0x40, 0x41)
MBR_PARTITIONS = 0x1BE

HERE = os.path.dirname(os.path.abspath(__file__))


def virtual_page(ce, ppn):
    """The virtual page that VSVFL keeps at physical page ppn of a CE (inverse of vpn_to_ppn in generate_nand.c for
    the data blocks, which are never remapped)."""
    chip_block, page_in_block = divmod(ppn, PAGES_PER_BLOCK)
    vbn, plane = divmod(chip_block, 2)
    bank = ce + NUM_CE * plane
    return vbn * PAGES_PER_SUBLOCK + page_in_block * NUM_BANKS + bank


def newest_pages(nand):
    """Returns {logical page: path of the page file with its newest copy}."""
    newest = {}
    for ce in range(NUM_CE):
        directory = os.path.join(nand, "cs%d" % ce)
        if not os.path.isdir(directory):
            continue
        for entry in os.scandir(directory):
            if not entry.name.endswith(".page"):
                continue
            with open(entry.path, "rb") as f:
                f.seek(BYTES_PER_PAGE)
                meta = f.read(12)
            if len(meta) < 12 or meta[9] not in SPARE_TYPE_DATA:
                continue
            lpn, age = struct.unpack_from("<II", meta)
            order = (age, virtual_page(ce, int(entry.name[:-5])))
            if lpn not in newest or order > newest[lpn][0]:
                newest[lpn] = (order, entry.path)
    return {lpn: path for lpn, (order, path) in newest.items()}


def mbr_partitions(disk):
    """Returns [(number, first page, pages)] of the MBR at logical page 0."""
    with open(disk, "rb") as f:
        mbr = f.read(512)
    if mbr[510:512] != b"\x55\xaa":
        raise SystemExit("no MBR on %s" % disk)
    partitions = []
    for i in range(4):
        entry = mbr[MBR_PARTITIONS + 16 * i:MBR_PARTITIONS + 16 * (i + 1)]
        kind = entry[4]
        start, size = struct.unpack_from("<II", entry, 8)
        if kind and size:
            partitions.append((i + 1, start, size))
    return partitions


def data_regions(fd, size):
    """Yields (offset, length) of the data (non-hole) regions of a file."""
    pos = 0
    while pos < size:
        try:
            start = os.lseek(fd, pos, os.SEEK_DATA)
        except OSError:
            return
        end = os.lseek(fd, start, os.SEEK_HOLE)
        yield start, end - start
        pos = end


def copy_regions(src, dst, src_offset, dst_offset, length):
    """Copies the data regions of src[src_offset:src_offset + length] to dst at dst_offset, keeping holes."""
    with open(src, "rb") as fs, open(dst, "r+b") as fd:
        size = os.fstat(fs.fileno()).st_size
        end = min(size, src_offset + length)
        for start, count in data_regions(fs.fileno(), end):
            start, stop = max(start, src_offset), min(start + count, end)
            while start < stop:
                chunk = min(stop - start, 1 << 22)
                data = os.pread(fs.fileno(), chunk, start)
                os.pwrite(fd.fileno(), data, dst_offset + start - src_offset)
                start += chunk


def create_sparse(path, size):
    with open(path, "wb") as f:
        f.truncate(size)


def extract(nand, out):
    os.makedirs(out, exist_ok=True)
    disk = os.path.join(out, "disk.img")
    pages = newest_pages(nand)
    if not pages:
        raise SystemExit("no data pages in %s" % nand)
    create_sparse(disk, (max(pages) + 1) * BYTES_PER_PAGE)
    with open(disk, "r+b") as f:
        for lpn, path in pages.items():
            with open(path, "rb") as page:
                os.pwrite(f.fileno(), page.read(BYTES_PER_PAGE), lpn * BYTES_PER_PAGE)
    print("%s: %d pages" % (disk, len(pages)))

    for number, start, size in mbr_partitions(disk):
        image = os.path.join(out, "partition%d.img" % number)
        create_sparse(image, size * BYTES_PER_PAGE)
        copy_regions(disk, image, start * BYTES_PER_PAGE, 0, size * BYTES_PER_PAGE)
        print("%s: pages %d-%d (%d MB)" % (image, start, start + size - 1, size * BYTES_PER_PAGE >> 20))


def build(directory, nand, generate_nand):
    disk = os.path.join(directory, "disk.img")
    for number, start, size in mbr_partitions(disk):
        image = os.path.join(directory, "partition%d.img" % number)
        if os.path.exists(image):
            copy_regions(image, disk, 0, start * BYTES_PER_PAGE, size * BYTES_PER_PAGE)
            print("copied %s into the disk" % image)
    subprocess.run([generate_nand, "--disk", disk, nand], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("extract", help="write the disk and partitions of a NAND to a directory")
    p.add_argument("nand")
    p.add_argument("dir")
    p = sub.add_parser("build", help="write a new NAND from an extracted directory")
    p.add_argument("dir")
    p.add_argument("nand")
    p.add_argument("--generate-nand", default=os.path.join(HERE, "generate_nand"))
    args = parser.parse_args()

    if args.command == "extract":
        extract(args.nand, args.dir)
    else:
        if os.path.exists(args.nand):
            parser.error("%s exists" % args.nand)
        build(args.dir, args.nand, args.generate_nand)
    return 0


if __name__ == "__main__":
    sys.exit(main())
