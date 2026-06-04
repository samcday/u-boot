# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 Linaro Ltd.
#
# Entry-type module for Android boot images

import hashlib
import struct

from binman.entry import Entry
from binman.etype.section import Entry_section
from dtoc import fdt_util


BOOT_MAGIC = b'ANDROID!'
BOOT_NAME_SIZE = 16
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024
BOOT_IMAGE_HEADER_V2_SIZE = 1660


def _align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def _pad(data, align):
    return data + b'\0' * (_align_up(len(data), align) - len(data))


class Entry_android_boot(Entry_section):
    """Android boot image

    This creates an Android boot image. Only header version 2 is supported at
    present, with a kernel payload and a DTB payload.

    Properties / Entry arguments:
        - header-version: Android boot image header version, must be 2
        - page-size: Image page size, defaults to 4096
        - base: Base address added to the offsets below, defaults to 0
        - kernel-offset: Kernel load offset from base, defaults to 0x00008000
        - ramdisk-offset: Ramdisk load offset from base, defaults to 0x01000000
        - second-offset: Second-stage load offset from base, defaults to
          0x00f00000
        - tags-offset: ATAGS/FDT offset from base, defaults to 0x00000100
        - dtb-offset: DTB load offset from base, defaults to 0x01f00000
        - os-version: Encoded Android OS version and patch level, defaults to 0
        - boot-name: Android boot image board name
        - cmdline: Android boot command line

    This entry uses the following subnodes:
        - kernel: section containing the executable payload
        - dtb: section containing the DTB payload

    Example::

        android-boot {
            header-version = <2>;
            page-size = <4096>;
            base = <0>;
            kernel-offset = <0x00008000>;
            ramdisk-offset = <0x01000000>;
            second-offset = <0x00f00000>;
            tags-offset = <0x00000100>;
            dtb-offset = <0x01f00000>;

            kernel {
                u-boot-nodtb {
                    compress = "gzip";
                };
            };

            dtb {
                u-boot-dtb {
                };
            };
        };
    """

    def ReadNode(self):
        super().ReadNode()
        self.header_version = fdt_util.GetInt(self._node, 'header-version', 2)
        self.page_size = fdt_util.GetInt(self._node, 'page-size', 4096)
        self.base = self._GetIntCells('base', 0)
        self.kernel_offset = self._GetIntCells('kernel-offset', 0x00008000)
        self.ramdisk_offset = self._GetIntCells('ramdisk-offset', 0x01000000)
        self.second_offset = self._GetIntCells('second-offset', 0x00f00000)
        self.tags_offset = self._GetIntCells('tags-offset', 0x00000100)
        self.dtb_offset = self._GetIntCells('dtb-offset', 0x01f00000)
        self.os_version = fdt_util.GetInt(self._node, 'os-version', 0)
        self.boot_name = fdt_util.GetString(self._node, 'boot-name', '')
        self.cmdline = fdt_util.GetString(self._node, 'cmdline', '')

        if self.header_version != 2:
            self.Raise('Only Android boot image header version 2 is supported')
        if self.page_size <= 0 or self.page_size & (self.page_size - 1):
            self.Raise('page-size must be a power of two')
        if self.page_size < BOOT_IMAGE_HEADER_V2_SIZE:
            self.Raise('page-size must fit the Android boot image header')
        if 'kernel' not in self._entries:
            self.Raise("Missing required subnode 'kernel'")
        if 'dtb' not in self._entries:
            self.Raise("Missing required subnode 'dtb'")

    def ReadEntries(self):
        for node in self._node.subnodes:
            if self.IsSpecialSubnode(node):
                continue
            if node.name not in ('kernel', 'dtb'):
                self.Raise("Unexpected subnode '%s'" % node.name)

            entry = Entry.Create(self, node, etype='section',
                                 expanded=self.GetImage().use_expanded,
                                 missing_etype=self.GetImage().missing_etype)
            entry.ReadNode()
            entry.SetPrefix(self._name_prefix)
            self._entries[node.name] = entry

    def _GetIntCells(self, propname, default):
        prop = self._node.props.get(propname)
        if not prop:
            return default

        values = prop.value if isinstance(prop.value, list) else [prop.value]
        if len(values) > 2:
            self.Raise("Property '%s' must contain one or two cells" %
                       propname)

        value = 0
        for cell in values:
            value = value << 32 | fdt_util.fdt32_to_cpu(cell)

        return value

    def _GetAddr(self, offset, name, size=32):
        addr = self.base + offset
        if addr >= 1 << size:
            self.Raise('%s address %#x does not fit in %d bits' %
                       (name, addr, size))

        return addr

    @staticmethod
    def _CheckFit(name, data, size):
        if len(data) > size:
            raise ValueError('%s is %d bytes, maximum is %d' %
                             (name, len(data), size))

        return data + b'\0' * (size - len(data))

    @staticmethod
    def _BootId(kernel, ramdisk, second, recovery_dtbo, dtb):
        digest = hashlib.sha1()
        for data in (kernel, ramdisk, second, recovery_dtbo, dtb):
            digest.update(data)
            digest.update(struct.pack('<I', len(data)))

        return digest.digest() + b'\0' * 12

    def _SplitCmdline(self):
        cmdline = self.cmdline.encode('ascii') + b'\0'
        return (self._CheckFit('cmdline', cmdline[:BOOT_ARGS_SIZE],
                               BOOT_ARGS_SIZE),
                self._CheckFit('extra-cmdline', cmdline[BOOT_ARGS_SIZE:],
                               BOOT_EXTRA_ARGS_SIZE))

    def _GetEntryData(self, name, required):
        entry = self._entries.get(name)
        data = entry.GetData(required)
        if data is None and not required:
            return None

        return data

    def BuildSectionData(self, required):
        kernel = self._GetEntryData('kernel', required)
        if kernel is None:
            return None

        dtb = self._GetEntryData('dtb', required)
        if dtb is None:
            return None

        ramdisk = b''
        second = b''
        recovery_dtbo = b''
        boot_name = self._CheckFit('boot-name', self.boot_name.encode('ascii'),
                                   BOOT_NAME_SIZE)
        cmdline, extra_cmdline = self._SplitCmdline()
        image_id = self._BootId(kernel, ramdisk, second, recovery_dtbo, dtb)

        header = struct.pack('<8s10I16s512s32s1024sIQIIQ', BOOT_MAGIC,
                             len(kernel),
                             self._GetAddr(self.kernel_offset, 'kernel'),
                             len(ramdisk),
                             self._GetAddr(self.ramdisk_offset, 'ramdisk'),
                             len(second),
                             self._GetAddr(self.second_offset, 'second'),
                             self._GetAddr(self.tags_offset, 'tags'),
                             self.page_size, self.header_version,
                             self.os_version, boot_name, cmdline, image_id,
                             extra_cmdline, len(recovery_dtbo), 0,
                             BOOT_IMAGE_HEADER_V2_SIZE, len(dtb),
                             self._GetAddr(self.dtb_offset, 'dtb', size=64))

        image = bytearray()
        image += _pad(header, self.page_size)
        image += _pad(kernel, self.page_size)
        image += _pad(ramdisk, self.page_size)
        image += _pad(second, self.page_size)
        image += _pad(recovery_dtbo, self.page_size)
        image += _pad(dtb, self.page_size)

        return bytes(image)
