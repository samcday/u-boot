# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 Linaro Ltd.
#
# Entry-type module for legacy Android boot images

import hashlib
import struct

from binman.entry import Entry
from binman.etype.section import Entry_section
from dtoc import fdt_util


BOOT_MAGIC = b'ANDROID!'
SEANDROIDENFORCE = b'SEANDROIDENFORCE'


def _align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def _pad(data, align):
    return data + b'\0' * (_align_up(len(data), align) - len(data))


class Entry_android_boot_legacy(Entry_section):
    """Legacy Android boot image

    This creates an old Android boot image with a vendor-specific DT payload
    placed after the kernel, ramdisk and optional second stage. The old
    dt_size/header_version union field is set to the DT payload size and the
    following header field is set to zero.

    Properties / Entry arguments:
        - page-size: Image page size, defaults to 2048
        - kernel-addr: Kernel load address, defaults to 0x10008000
        - ramdisk-addr: Ramdisk load address, defaults to 0x11000000
        - second-addr: Second-stage load address, defaults to 0x10f00000
        - tags-addr: ATAGS/FDT address, defaults to 0x10000100
        - cmdline: Android boot command line
        - boot-name: Android boot image board name
        - default-ramdisk-size: NUL-byte ramdisk size to use when no ramdisk
          subnode exists, defaults to 0
        - append-seandroid-enforce: Append Samsung SEANDROIDENFORCE trailer

    This entry uses the following subnodes:
        - kernel: section containing the executable payload, normally u-boot
        - ramdisk: optional section containing a ramdisk
        - second: optional section containing a second-stage payload
        - dt: vendor-specific DT payload entry, e.g. qcom-qcdt or samsung-dtbh

    Example::

        android-boot-legacy {
            page-size = <2048>;
            kernel-addr = <0x80008000>;
            cmdline = "lk2nd";
            append-seandroid-enforce;

            kernel {
                u-boot {
                    no-expanded;
                };
            };

            dt {
                type = "qcom-qcdt";
                qcom,msm-id = <206 0>;
                qcom,board-id = <0xce08ff01 1>;
            };
        };
    """

    def ReadNode(self):
        super().ReadNode()
        self.page_size = fdt_util.GetInt(self._node, 'page-size', 2048)
        self.kernel_addr = fdt_util.GetInt(self._node, 'kernel-addr',
                                           0x10008000)
        self.ramdisk_addr = fdt_util.GetInt(self._node, 'ramdisk-addr',
                                            0x11000000)
        self.second_addr = fdt_util.GetInt(self._node, 'second-addr',
                                           0x10f00000)
        self.tags_addr = fdt_util.GetInt(self._node, 'tags-addr', 0x10000100)
        self.cmdline = fdt_util.GetString(self._node, 'cmdline', '')
        self.boot_name = fdt_util.GetString(self._node, 'boot-name', '')
        self.default_ramdisk_size = fdt_util.GetInt(self._node,
                                                    'default-ramdisk-size', 0)
        self.append_seandroid = fdt_util.GetBool(self._node,
                                                 'append-seandroid-enforce')

        if self.page_size <= 0 or self.page_size & (self.page_size - 1):
            self.Raise('page-size must be a power of two')
        if self.default_ramdisk_size < 0:
            self.Raise('default-ramdisk-size must be non-negative')

    def ReadEntries(self):
        for node in self._node.subnodes:
            if self.IsSpecialSubnode(node):
                continue

            etype = None
            if node.name in ('kernel', 'ramdisk', 'second'):
                etype = 'section'

            entry = Entry.Create(self, node, etype=etype,
                                 expanded=self.GetImage().use_expanded,
                                 missing_etype=self.GetImage().missing_etype)
            entry.ReadNode()
            entry.SetPrefix(self._name_prefix)
            self._entries[node.name] = entry

    @staticmethod
    def _CheckFit(name, data, size):
        if len(data) > size:
            raise ValueError('%s is %d bytes, maximum is %d' %
                             (name, len(data), size))

        return data + b'\0' * (size - len(data))

    @staticmethod
    def _BootId(kernel, ramdisk, second, dt):
        digest = hashlib.sha1()
        for data in (kernel, ramdisk, second, dt):
            digest.update(data)
            digest.update(struct.pack('<I', len(data)))

        return digest.digest() + b'\0' * 12

    def _GetEntryData(self, name, required, default=None):
        entry = self._entries.get(name)
        if not entry:
            return default

        data = entry.GetData(required)
        if data is None and not required:
            return None

        return data

    def BuildSectionData(self, required):
        kernel = self._GetEntryData('kernel', required)
        if kernel is None:
            return None

        dt = self._GetEntryData('dt', required)
        if dt is None:
            return None

        ramdisk = self._GetEntryData('ramdisk', required,
                                     b'\0' * self.default_ramdisk_size)
        if ramdisk is None:
            return None

        second = self._GetEntryData('second', required, b'')
        if second is None:
            return None

        boot_name = self._CheckFit('boot-name', self.boot_name.encode('ascii'),
                                   16)
        cmdline = self._CheckFit('cmdline', self.cmdline.encode('ascii'), 512)
        image_id = self._BootId(kernel, ramdisk, second, dt)
        header = struct.pack('<8s10I16s512s32s', BOOT_MAGIC,
                             len(kernel), self.kernel_addr,
                             len(ramdisk), self.ramdisk_addr,
                             len(second), self.second_addr,
                             self.tags_addr, self.page_size,
                             len(dt), 0, boot_name, cmdline, image_id)

        image = bytearray()
        image += _pad(header, self.page_size)
        image += _pad(kernel, self.page_size)
        image += _pad(ramdisk, self.page_size)
        if second:
            image += _pad(second, self.page_size)
        image += _pad(dt, self.page_size)
        if self.append_seandroid:
            image += SEANDROIDENFORCE

        return bytes(image)
