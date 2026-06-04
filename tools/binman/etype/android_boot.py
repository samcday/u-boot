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
BOOT_IMAGE_HEADER_V0_SIZE = 608
BOOT_IMAGE_HEADER_V2_SIZE = 1660
QCDT_MAGIC = b'QCDT'
QCDT_VERSION = 2
DTBH_MAGIC = b'DTBH'
DTBH_VERSION = 2
DTBH_PLATFORM_CODE = 0x50a6
DTBH_SUBTYPE_CODE = 0x217584da
DTBH_SPACE = 0x20
SEANDROIDENFORCE = b'SEANDROIDENFORCE'


def _align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def _pad(data, align):
    return data + b'\0' * (_align_up(len(data), align) - len(data))


class Entry_android_boot(Entry_section):
    """Android boot image

    This creates an Android boot image. Header version 2 is supported with a
    kernel payload and a DTB payload. Legacy header version 0 is supported with
    a kernel payload, optional ramdisk payload and optional QCDT or DTBH vendor
    DT payload appended after the regular Android boot payloads.

    Properties / Entry arguments:
        - header-version: Android boot image header version, must be 0 or 2
        - page-size: Image page size, defaults to 4096
        - base: Base address added to the offsets below, defaults to 0
        - kernel-offset: Kernel load offset from base, defaults to 0x00008000
        - ramdisk-offset: Ramdisk load offset from base, defaults to 0x01000000
        - second-offset: Second-stage load offset from base, defaults to
          0x00f00000. This only sets the legacy header field; a second-stage
          payload is not currently supported.
        - tags-offset: ATAGS/FDT offset from base, defaults to 0x00000100
        - dtb-offset: DTB load offset from base, defaults to 0x01f00000,
          used by header version 2 only
        - os-version: Encoded Android OS version and patch level, defaults to 0
        - boot-name: Android boot image board name
        - cmdline: Android boot command line
        - append-seandroid-enforce: Append Samsung SEANDROIDENFORCE trailer,
          used by header version 0 only

    This entry uses the following subnodes:
        - kernel: section containing the executable payload
        - ramdisk: optional section containing a ramdisk payload
        - dtb: section containing the DTB payload, used by header version 2 only
        - vendor-dt: optional legacy vendor DT payload, used by header version
          0 only

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

        android-boot {
            header-version = <0>;
            page-size = <2048>;
            base = <0x80200000>;
            cmdline = "foo";

            kernel {
                u-boot {
                    no-expanded;
                };
            };

            ramdisk {
                fill {
                    size = <1>;
                };
            };
        };

        android-boot {
            header-version = <0>;
            page-size = <2048>;
            base = <0x80000000>;
            kernel-offset = <0x00008000>;
            ramdisk-offset = <0x01000000>;
            tags-offset = <0x00000100>;
            cmdline = "foo";
            append-seandroid-enforce;

            kernel {
                u-boot {
                    no-expanded;
                };
            };

            ramdisk {
                fill {
                    size = <1>;
                };
            };

            vendor-dt {
                qcdt {
                    dtb-0 {
                        compatible = "samsung,a5u-eur", "qcom,msm8916";
                        qcom,msm-id = <206 0>;
                        qcom,board-id = <0xce08ff01 1>;
                    };
                };
            };
        };

        A DTBH vendor DT can use a real DTB entry::

        android-boot {
            header-version = <0>;

            kernel {
                u-boot-nodtb {
                };
            };

            vendor-dt {
                dtbh {
                    dtb-0 {
                        u-boot-dtb {
                        };
                    };
                };
            };
        };

        Or each DTBH entry can describe a skinny DTB directly::

        android-boot {
            header-version = <0>;

            kernel {
                u-boot {
                    no-expanded;
                };
            };

            vendor-dt {
                dtbh {
                    dtb-0 {
                        compatible = "samsung,j7xelte", "samsung,exynos7870";
                        model_info-chip = <7870>;
                        model_info-hw_rev = <6>;
                        model_info-hw_rev_end = <6>;
                    };
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
        self.append_seandroid = fdt_util.GetBool(self._node,
                                                 'append-seandroid-enforce')
        self.vendor_dt_node = self._node.FindNode('vendor-dt')

        if self.header_version not in (0, 2):
            self.Raise('Only Android boot image header versions 0 and 2 are '
                       'supported')
        if self.page_size <= 0 or self.page_size & (self.page_size - 1):
            self.Raise('page-size must be a power of two')
        if (self.header_version == 0 and
                self.page_size < BOOT_IMAGE_HEADER_V0_SIZE):
            self.Raise('page-size must fit the Android boot image header')
        if (self.header_version == 2 and
                self.page_size < BOOT_IMAGE_HEADER_V2_SIZE):
            self.Raise('page-size must fit the Android boot image header')
        if 'kernel' not in self._entries:
            self.Raise("Missing required subnode 'kernel'")
        if self.header_version == 2 and 'dtb' not in self._entries:
            self.Raise("Missing required subnode 'dtb'")
        if self.header_version == 2 and self.vendor_dt_node:
            self.Raise("Subnode 'vendor-dt' requires header-version 0")
        if self.header_version == 2 and self.append_seandroid:
            self.Raise("Property 'append-seandroid-enforce' requires "
                       "header-version 0")
        if self.header_version == 0 and 'dtb' in self._entries:
            self.Raise("Subnode 'dtb' requires header-version 2")

    def ReadEntries(self):
        for node in self._node.subnodes:
            if self.IsSpecialSubnode(node):
                continue
            if node.name == 'vendor-dt':
                self._ReadVendorDtEntries(node)
                continue
            if node.name not in ('kernel', 'ramdisk', 'dtb'):
                self.Raise("Unexpected subnode '%s'" % node.name)

            entry = Entry.Create(self, node, etype='section',
                                 expanded=self.GetImage().use_expanded,
                                 missing_etype=self.GetImage().missing_etype)
            entry.ReadNode()
            entry.SetPrefix(self._name_prefix)
            self._entries[node.name] = entry

    @staticmethod
    def _VendorDtEntryName(node):
        return '_vendor_dt_%s' % node.name

    def _ReadVendorDtEntries(self, vendor_dt_node):
        dtbh_node = vendor_dt_node.FindNode('dtbh')
        if not dtbh_node:
            return

        for node in dtbh_node.subnodes:
            if self._IsSyntheticDtbhNode(node):
                continue

            entry = Entry.Create(self, node, etype='section',
                                 expanded=self.GetImage().use_expanded,
                                 missing_etype=self.GetImage().missing_etype)
            entry.ReadNode()
            entry.SetPrefix(self._name_prefix)
            self._entries[self._VendorDtEntryName(node)] = entry

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

    @staticmethod
    def _GetU32Cells(node, propname):
        prop = node.props.get(propname)
        if not prop:
            raise ValueError("Node '%s': Missing required property '%s'" %
                             (node.path, propname))

        values = prop.value if isinstance(prop.value, list) else [prop.value]
        return [fdt_util.fdt32_to_cpu(value) for value in values]

    @classmethod
    def _GetU32Tuple(cls, node, propname, width):
        values = cls._GetU32Cells(node, propname)
        if len(values) != width:
            raise ValueError("Node '%s': Property '%s' must contain exactly "
                             "%d cells" % (node.path, propname, width))

        return tuple(values)

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
    def _BootId(*payloads):
        digest = hashlib.sha1()
        for data in payloads:
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

    def _GetOptionalEntryData(self, name, required, default=b''):
        entry = self._entries.get(name)
        if not entry:
            return default

        data = entry.GetData(required)
        if data is None and not required:
            return None

        return data

    @staticmethod
    def _BuildDtb(node):
        import libfdt

        fsw = libfdt.FdtSw()
        fsw.INC_SIZE = 65536
        fsw.finish_reservemap()

        def _AddNode(in_node):
            for pname, prop in in_node.props.items():
                fsw.property(pname, prop.bytes)
            for subnode in in_node.subnodes:
                with fsw.add_node(subnode.name):
                    _AddNode(subnode)

        with fsw.add_node(''):
            _AddNode(node)
            if not node.FindNode('chosen'):
                with fsw.add_node('chosen'):
                    pass
        fdt = fsw.as_fdt()
        fdt.pack()
        return bytes(fdt.as_bytearray())

    @staticmethod
    def _IsSyntheticDtbhNode(node):
        return 'model_info-chip' in node.props

    @classmethod
    def _GetNodeU32(cls, node, propname):
        return cls._GetU32Tuple(node, propname, 1)[0]

    @staticmethod
    def _GetDtbRootU32(node, data, propname):
        import libfdt

        try:
            fdt = libfdt.Fdt(data)
            root = fdt.path_offset('/')
            prop = fdt.getprop(root, propname)
        except libfdt.FdtException as exc:
            raise ValueError("Node '%s': Missing required DTB root property "
                             "'%s'" % (node.path, propname)) from exc

        if len(prop) != 4:
            raise ValueError("Node '%s': DTB root property '%s' must contain "
                             "exactly 1 cell" % (node.path, propname))

        return fdt_util.fdt32_to_cpu(prop)

    def _GetVendorDtNode(self):
        if len(self.vendor_dt_node.subnodes) != 1:
            self.Raise("Subnode 'vendor-dt' must contain exactly one format "
                       "subnode")

        node = self.vendor_dt_node.subnodes[0]
        if node.name not in ('qcdt', 'dtbh'):
            self.Raise("Subnode 'vendor-dt' must contain a 'qcdt' or 'dtbh' "
                       "subnode")

        return node

    def _BuildQcdt(self, qcdt_node):
        if not qcdt_node.subnodes:
            raise ValueError("Node '%s': Missing required DTB subnodes" %
                             qcdt_node.path)

        page_size = fdt_util.GetInt(qcdt_node, 'page-size', self.page_size)
        if page_size <= 0 or page_size & (page_size - 1):
            raise ValueError("Node '%s': page-size must be a power of two" %
                             qcdt_node.path)

        dtbs = []
        for node in qcdt_node.subnodes:
            msm_id = self._GetU32Tuple(node, 'qcom,msm-id', 2)
            board_id = self._GetU32Tuple(node, 'qcom,board-id', 2)
            dtbs.append((node, msm_id, board_id, self._BuildDtb(node)))

        dtb_offset = _align_up(12 + len(dtbs) * 24, page_size)
        records = []
        payloads = bytearray()
        for _node, msm_id, board_id, dtb in dtbs:
            dtb_size = _align_up(len(dtb), page_size)
            records.append((*msm_id, *board_id, dtb_offset, dtb_size))
            payloads += _pad(dtb, page_size)
            dtb_offset += dtb_size

        qcdt = bytearray(struct.pack('<4sII', QCDT_MAGIC, QCDT_VERSION,
                                     len(records)))
        for platform_id, soc_rev, variant_id, subtype, offset, size in records:
            qcdt += struct.pack('<IIIIII', platform_id, variant_id, subtype,
                                soc_rev, offset, size)

        return _pad(qcdt, page_size) + bytes(payloads)

    def _BuildDtbh(self, dtbh_node, required):
        if not dtbh_node.subnodes:
            raise ValueError("Node '%s': Missing required DTB subnodes" %
                             dtbh_node.path)

        page_size = fdt_util.GetInt(dtbh_node, 'page-size', self.page_size)
        if page_size <= 0 or page_size & (page_size - 1):
            raise ValueError("Node '%s': page-size must be a power of two" %
                             dtbh_node.path)

        platform = fdt_util.GetInt(dtbh_node, 'platform', DTBH_PLATFORM_CODE)
        subtype = fdt_util.GetInt(dtbh_node, 'subtype', DTBH_SUBTYPE_CODE)

        dtbs = []
        for node in dtbh_node.subnodes:
            if self._IsSyntheticDtbhNode(node):
                data = self._BuildDtb(node)
                chip = self._GetNodeU32(node, 'model_info-chip')
                hw_rev = self._GetNodeU32(node, 'model_info-hw_rev')
                hw_rev_end = self._GetNodeU32(node, 'model_info-hw_rev_end')
            else:
                entry = self._entries[self._VendorDtEntryName(node)]
                data = entry.GetData(required)
                if data is None and not required:
                    return None

                chip = self._GetDtbRootU32(node, data, 'model_info-chip')
                hw_rev = self._GetDtbRootU32(node, data, 'model_info-hw_rev')
                hw_rev_end = self._GetDtbRootU32(node, data,
                                                 'model_info-hw_rev_end')
            dtbs.append((chip, platform, subtype, hw_rev, hw_rev_end, data))

        header_size = _align_up(12 + len(dtbs) * 32 + 4, page_size)
        dtb_offset = header_size
        records = []
        payloads = bytearray()
        for chip, platform, subtype, hw_rev, hw_rev_end, dtb in dtbs:
            dtb_size = _align_up(len(dtb), page_size)
            records.append((chip, platform, subtype, hw_rev, hw_rev_end,
                            dtb_offset, dtb_size, DTBH_SPACE))
            payloads += _pad(dtb, page_size)
            dtb_offset += dtb_size

        dtbh = bytearray(struct.pack('<4sII', DTBH_MAGIC, DTBH_VERSION,
                                     len(records)))
        for record in records:
            dtbh += struct.pack('<8I', *record)

        return _pad(dtbh, page_size) + bytes(payloads)

    def _BuildVendorDt(self, required):
        if not self.vendor_dt_node:
            return b''

        node = self._GetVendorDtNode()
        if node.name == 'qcdt':
            return self._BuildQcdt(node)

        return self._BuildDtbh(node, required)

    def _BuildLegacySectionData(self, required):
        kernel = self._GetEntryData('kernel', required)
        if kernel is None:
            return None

        vendor_dt = self._BuildVendorDt(required)
        if vendor_dt is None:
            return None
        ramdisk = self._GetOptionalEntryData('ramdisk', required)
        if ramdisk is None:
            return None

        second = b''
        boot_name = self._CheckFit('boot-name', self.boot_name.encode('ascii'),
                                   BOOT_NAME_SIZE)
        cmdline = self._CheckFit('cmdline', self.cmdline.encode('ascii'),
                                 BOOT_ARGS_SIZE)
        payloads = (kernel, ramdisk, second)
        if vendor_dt:
            payloads += (vendor_dt,)
        image_id = self._BootId(*payloads)

        header = struct.pack('<8s10I16s512s32s', BOOT_MAGIC,
                             len(kernel),
                             self._GetAddr(self.kernel_offset, 'kernel'),
                             len(ramdisk),
                             self._GetAddr(self.ramdisk_offset, 'ramdisk'),
                             len(second),
                             self._GetAddr(self.second_offset, 'second'),
                             self._GetAddr(self.tags_offset, 'tags'),
                             self.page_size, len(vendor_dt), 0, boot_name,
                             cmdline, image_id)

        image = bytearray()
        image += _pad(header, self.page_size)
        image += _pad(kernel, self.page_size)
        image += _pad(ramdisk, self.page_size)
        image += _pad(vendor_dt, self.page_size)
        if self.append_seandroid:
            image += SEANDROIDENFORCE

        return bytes(image)

    def _BuildV2SectionData(self, required):
        kernel = self._GetEntryData('kernel', required)
        if kernel is None:
            return None

        dtb = self._GetEntryData('dtb', required)
        if dtb is None:
            return None

        ramdisk = self._GetOptionalEntryData('ramdisk', required)
        if ramdisk is None:
            return None

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

    def BuildSectionData(self, required):
        if self.header_version == 0:
            return self._BuildLegacySectionData(required)

        return self._BuildV2SectionData(required)
