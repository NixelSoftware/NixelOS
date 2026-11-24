"""Dependency-free ext4 image writer for the NixelOS filesystem profile.

The output matches what ``src/kernel/fs.c`` can mount and modify: 4 KiB
blocks, a single block group, 256-byte inodes, extent-mapped files, filetype
directory entries, and no journal or metadata checksums. It lets Windows
builds create the image without external filesystem tools.
"""
import os
from pathlib import Path
import struct
import time

BLOCK_SIZE = 4096
INODE_SIZE = 256
INODE_COUNT = 2048
BLOCKS_PER_GROUP = 8 * BLOCK_SIZE
INODE_TABLE_BLOCKS = INODE_COUNT * INODE_SIZE // BLOCK_SIZE

GROUP_DESC_BLOCK = 1
BLOCK_BITMAP_BLOCK = 2
INODE_BITMAP_BLOCK = 3
INODE_TABLE_BLOCK = 4
FIRST_DATA_BLOCK = INODE_TABLE_BLOCK + INODE_TABLE_BLOCKS

ROOT_INO = 2
LOST_FOUND_INO = 11
FIRST_INO = 11
EXTENT_MAGIC = 0xF30A
EXTENTS_FL = 0x80000


class _Node:
    __slots__ = ("rel", "name", "is_dir", "mode", "owner", "inode", "parent",
                 "children", "start", "blocks", "size", "links", "payload")

    def __init__(self, rel, name, is_dir, mode, owner):
        self.rel = rel
        self.name = name
        self.is_dir = is_dir
        self.mode = mode
        self.owner = owner
        self.inode = 0
        self.parent = None
        self.children = []
        self.start = 0
        self.blocks = 0
        self.size = 0
        self.links = 1
        self.payload = b""


def _directory_payload(node):
    parent = node.parent if node.parent is not None else node
    entries = [(".", node.inode, 2), ("..", parent.inode, 2)]
    entries += [(child.name, child.inode, 2 if child.is_dir else 1)
                for child in sorted(node.children, key=lambda child: child.name)]

    pages, page, used = [], [], 0
    for entry in entries:
        need = (8 + len(entry[0].encode()) + 3) & ~3
        if page and used + need > BLOCK_SIZE:
            pages.append(page)
            page, used = [], 0
        page.append((entry, need))
        used += need
    pages.append(page)

    payload = bytearray()
    for page in pages:
        block = bytearray(BLOCK_SIZE)
        offset = 0
        for index, (entry, need) in enumerate(page):
            name, ino, kind = entry
            raw = name.encode()
            record = need if index < len(page) - 1 else BLOCK_SIZE - offset
            struct.pack_into("<IHBB", block, offset, ino, record, len(raw), kind)
            block[offset + 8:offset + 8 + len(raw)] = raw
            offset += record
        payload += block
    return bytes(payload)


def _inode_bytes(node, now):
    raw = bytearray(INODE_SIZE)
    struct.pack_into("<H", raw, 0, node.mode)
    struct.pack_into("<H", raw, 2, node.owner)
    struct.pack_into("<I", raw, 4, node.size)
    struct.pack_into("<I", raw, 8, now)
    struct.pack_into("<I", raw, 12, now)
    struct.pack_into("<I", raw, 16, now)
    struct.pack_into("<H", raw, 24, node.owner)
    struct.pack_into("<H", raw, 26, node.links)
    struct.pack_into("<I", raw, 28, node.blocks * (BLOCK_SIZE // 512))
    struct.pack_into("<I", raw, 32, EXTENTS_FL)
    struct.pack_into("<HHHHI", raw, 40, EXTENT_MAGIC, 1 if node.blocks else 0, 4, 0, 0)
    if node.blocks:
        struct.pack_into("<IHHI", raw, 52, 0, node.blocks, 0, node.start)
    struct.pack_into("<H", raw, 128, 32)  # i_extra_isize
    return bytes(raw)


def _superblock(total_blocks, free_blocks, free_inodes, now, uuid, label):
    sb = bytearray(1024)

    def put32(offset, value):
        struct.pack_into("<I", sb, offset, value & 0xFFFFFFFF)

    def put16(offset, value):
        struct.pack_into("<H", sb, offset, value & 0xFFFF)

    put32(0x00, INODE_COUNT)
    put32(0x04, total_blocks)
    put32(0x08, 0)                      # reserved blocks
    put32(0x0C, free_blocks)
    put32(0x10, free_inodes)
    put32(0x14, 0)                      # first data block (4 KiB blocks)
    put32(0x18, 2)                      # 4096-byte blocks
    put32(0x1C, 2)                      # 4096-byte clusters
    put32(0x20, BLOCKS_PER_GROUP)
    put32(0x24, BLOCKS_PER_GROUP)
    put32(0x28, INODE_COUNT)
    put32(0x2C, 0)                      # mount time
    put32(0x30, now)                    # write time
    put16(0x34, 0)                      # mount count
    put16(0x36, 0xFFFF)                 # disable mount-count checks
    put16(0x38, 0xEF53)                 # magic
    put16(0x3A, 1)                      # clean
    put16(0x3C, 1)                      # continue on errors
    put16(0x3E, 0)                      # minor revision
    put32(0x40, now)                    # last check
    put32(0x44, 0)                      # check interval
    put32(0x48, 0)                      # Linux
    put32(0x4C, 1)                      # dynamic revision
    put16(0x50, 0)
    put16(0x52, 0)
    put32(0x54, FIRST_INO)
    put16(0x58, INODE_SIZE)
    put16(0x5A, 0)                      # block group number
    put32(0x5C, 0)                      # compatible features
    put32(0x60, 0x42)                   # filetype + extents
    put32(0x64, 0x03)                   # sparse_super + large_file
    sb[0x68:0x78] = uuid
    sb[0x78:0x88] = label.encode("ascii")[:16].ljust(16, b"\0")
    sb[0xF0:0x100] = os.urandom(16)     # hash seed
    sb[0x100] = 1                       # half_md4
    put16(0x102, 0)                     # 32-byte group descriptors
    put32(0x104, 0)                     # default mount options
    put32(0x108, now)                   # creation time
    put32(0x150, 0)                     # blocks_count_hi
    put32(0x154, 0)                     # r_blocks_count_hi
    put32(0x158, 0)                     # free_blocks_count_hi
    put16(0x15C, 32)                    # min extra inode size
    put16(0x15E, 32)                    # wanted extra inode size
    put32(0x160, 1)                     # signed directory hashes
    return bytes(sb)


def build_ext4(root, metadata, destination, sectors, label="NIXELOS"):
    """Create an ext4 partition image of ``sectors`` 512-byte sectors.

    ``metadata`` maps each path below ``root`` (POSIX separators) to
    ``(uid, mode)`` where mode includes the file-type bits.
    """
    root = Path(root)
    total_blocks = sectors * 512 // BLOCK_SIZE
    if total_blocks < FIRST_DATA_BLOCK + 2 or total_blocks > BLOCKS_PER_GROUP:
        raise ValueError(f"Unsupported ext4 partition size: {total_blocks} blocks")

    root_node = _Node("", "", True, 0o040755, 0)
    nodes = {"": root_node}
    for path in sorted(root.rglob("*")):
        rel = path.relative_to(root).as_posix()
        if rel not in metadata:
            raise ValueError(f"Missing inode metadata for {rel}")
        owner, mode = metadata[rel]
        node = _Node(rel, path.name, path.is_dir(), mode, owner)
        node.parent = nodes[rel.rpartition("/")[0]]
        node.parent.children.append(node)
        nodes[rel] = node

    lost_found = _Node("lost+found", "lost+found", True, 0o040700, 0)
    lost_found.parent = root_node
    root_node.children.append(lost_found)
    nodes["lost+found"] = lost_found

    root_node.inode = ROOT_INO
    lost_found.inode = LOST_FOUND_INO
    next_inode = 12
    for rel in sorted(nodes):
        node = nodes[rel]
        if node.inode:
            continue
        node.inode = next_inode
        next_inode += 1
    used_inodes = next_inode - 1
    if used_inodes > INODE_COUNT:
        raise ValueError(f"{used_inodes} inodes needed, only {INODE_COUNT} available")

    for node in nodes.values():
        if node.is_dir:
            node.links = 2 + sum(1 for child in node.children if child.is_dir)
        else:
            node.links = 1

    directories = sorted((node for node in nodes.values() if node.is_dir),
                         key=lambda node: node.inode)
    files = sorted((node for node in nodes.values() if not node.is_dir),
                   key=lambda node: node.inode)

    next_block = FIRST_DATA_BLOCK
    for node in directories:
        node.payload = _directory_payload(node)
        node.size = len(node.payload)
        node.blocks = len(node.payload) // BLOCK_SIZE
        node.start = next_block
        next_block += node.blocks
    for node in files:
        node.size = (root / node.rel).stat().st_size
        node.blocks = (node.size + BLOCK_SIZE - 1) // BLOCK_SIZE
        if node.blocks > 32768:
            raise ValueError(f"{node.rel} is too large for a single extent")
        node.start = next_block if node.blocks else 0
        next_block += node.blocks
    if next_block > total_blocks:
        raise ValueError(f"Filesystem contents need {next_block} blocks, "
                         f"partition has {total_blocks}")

    image = bytearray(total_blocks * BLOCK_SIZE)
    now = int(time.time())
    uuid = bytearray(os.urandom(16))
    uuid[6] = (uuid[6] & 0x0F) | 0x40
    uuid[8] = (uuid[8] & 0x3F) | 0x80
    image[1024:2048] = _superblock(total_blocks, total_blocks - next_block,
                                   INODE_COUNT - used_inodes, now, bytes(uuid), label)

    descriptor = GROUP_DESC_BLOCK * BLOCK_SIZE
    struct.pack_into("<IIIHHH", image, descriptor, BLOCK_BITMAP_BLOCK,
                     INODE_BITMAP_BLOCK, INODE_TABLE_BLOCK,
                     total_blocks - next_block, INODE_COUNT - used_inodes,
                     len(directories))
    struct.pack_into("<H", image, descriptor + 0x12, 4)  # inode table zeroed

    block_bitmap = bytearray(BLOCK_SIZE)
    for block in range(next_block):
        block_bitmap[block // 8] |= 1 << (block % 8)
    for block in range(total_blocks, BLOCKS_PER_GROUP):
        block_bitmap[block // 8] |= 1 << (block % 8)
    image[BLOCK_BITMAP_BLOCK * BLOCK_SIZE:(BLOCK_BITMAP_BLOCK + 1) * BLOCK_SIZE] = block_bitmap

    inode_bitmap = bytearray(BLOCK_SIZE)
    for inode in range(1, used_inodes + 1):
        inode_bitmap[(inode - 1) // 8] |= 1 << ((inode - 1) % 8)
    for inode in range(INODE_COUNT, BLOCK_SIZE * 8):
        inode_bitmap[inode // 8] |= 1 << (inode % 8)
    image[INODE_BITMAP_BLOCK * BLOCK_SIZE:(INODE_BITMAP_BLOCK + 1) * BLOCK_SIZE] = inode_bitmap

    table = INODE_TABLE_BLOCK * BLOCK_SIZE
    for node in nodes.values():
        offset = table + (node.inode - 1) * INODE_SIZE
        image[offset:offset + INODE_SIZE] = _inode_bytes(node, now)

    for node in directories:
        offset = node.start * BLOCK_SIZE
        image[offset:offset + len(node.payload)] = node.payload
    for node in files:
        if not node.blocks:
            continue
        data = (root / node.rel).read_bytes()
        offset = node.start * BLOCK_SIZE
        image[offset:offset + len(data)] = data

    Path(destination).write_bytes(image)
