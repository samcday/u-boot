#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+

"""Build a legacy Qualcomm QCDT Android boot image."""

import argparse
import hashlib
import struct
import sys


BOOT_MAGIC = b'ANDROID!'
QCDT_MAGIC = b'QCDT'
SEANDROIDENFORCE = b'SEANDROIDENFORCE'


def parse_int(value):
    return int(value, 0)


def align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def pad(data, align):
    return data + b'\0' * (align_up(len(data), align) - len(data))


def read_file(fname):
    with open(fname, 'rb') as inf:
        return inf.read()


def write_file(fname, data):
    with open(fname, 'wb') as outf:
        outf.write(data)


def check_fit(name, data, size):
    if len(data) > size:
        raise ValueError('%s is %d bytes, maximum is %d' %
                         (name, len(data), size))

    return data + b'\0' * (size - len(data))


def build_qcdt(dtb, page_size, platform_id, variant_id, board_subtype,
               soc_rev):
    dtb_offset = page_size
    dtb_size = align_up(len(dtb), page_size)
    header = struct.pack('<4sII', QCDT_MAGIC, 2, 1)
    entry = struct.pack('<IIIIII', platform_id, variant_id, board_subtype,
                        soc_rev, dtb_offset, dtb_size)

    return pad(header + entry, page_size) + pad(dtb, page_size)


def boot_id(kernel, ramdisk, second, dt):
    digest = hashlib.sha1()
    for data in (kernel, ramdisk, second, dt):
        digest.update(data)
        digest.update(struct.pack('<I', len(data)))

    return digest.digest() + b'\0' * 12


def build_bootimg(kernel, ramdisk, qcdt, args):
    second = b''
    base = args.base
    page_size = args.page_size
    name = check_fit('board name', args.name.encode('ascii'), 16)
    cmdline = check_fit('cmdline', args.cmdline.encode('ascii'), 512)
    image_id = boot_id(kernel, ramdisk, second, qcdt)
    header = struct.pack(
        '<8s10I16s512s32s',
        BOOT_MAGIC,
        len(kernel), base + args.kernel_offset,
        len(ramdisk), base + args.ramdisk_offset,
        len(second), base + args.second_offset,
        base + args.tags_offset,
        page_size,
        len(qcdt),
        0,
        name,
        cmdline,
        image_id)

    image = bytearray()
    image += pad(header, page_size)
    image += pad(kernel, page_size)
    image += pad(ramdisk, page_size)
    if second:
        image += pad(second, page_size)
    image += pad(qcdt, page_size)
    if args.append_seandroid:
        image += SEANDROIDENFORCE

    return bytes(image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', required=True)
    parser.add_argument('--dtb', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--ramdisk')
    parser.add_argument('--qcdt-output')
    parser.add_argument('--page-size', type=parse_int, default=2048)
    parser.add_argument('--base', type=parse_int, default=0x80000000)
    parser.add_argument('--kernel-offset', type=parse_int, default=0x00008000)
    parser.add_argument('--ramdisk-offset', type=parse_int, default=0x01000000)
    parser.add_argument('--second-offset', type=parse_int, default=0x00f00000)
    parser.add_argument('--tags-offset', type=parse_int, default=0x00000100)
    parser.add_argument('--cmdline', default='')
    parser.add_argument('--name', default='')
    parser.add_argument('--platform-id', type=parse_int, required=True)
    parser.add_argument('--variant-id', type=parse_int, required=True)
    parser.add_argument('--board-subtype', type=parse_int, required=True)
    parser.add_argument('--soc-rev', type=parse_int, default=0)
    parser.add_argument('--append-seandroid', action='store_true')
    args = parser.parse_args()

    if args.page_size <= 0 or args.page_size & (args.page_size - 1):
        raise ValueError('page size must be a power of two')

    kernel = read_file(args.kernel)
    dtb = read_file(args.dtb)
    ramdisk = read_file(args.ramdisk) if args.ramdisk else b'\0'
    qcdt = build_qcdt(dtb, args.page_size, args.platform_id,
                      args.variant_id, args.board_subtype, args.soc_rev)
    image = build_bootimg(kernel, ramdisk, qcdt, args)

    if args.qcdt_output:
        write_file(args.qcdt_output, qcdt)
    write_file(args.output, image)

    return 0


if __name__ == '__main__':
    sys.exit(main())
