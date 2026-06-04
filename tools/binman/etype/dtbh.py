# SPDX-License-Identifier: GPL-2.0+
# Entry-type module for Samsung Android DTBH tables

import struct

from binman.entry import Entry
from binman.etype.section import Entry_section
from dtoc import fdt_util


DTBH_MAGIC = b'DTBH'
DTBH_VERSION = 2
DTBH_PLATFORM_CODE_DEF = 0x50a6
DTBH_SUBTYPE_CODE_DEF = 0x217584da
DTBH_SPACE = 0x20


def _align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def _pad(data, align):
    return data + b'\0' * (_align_up(len(data), align) - len(data))


class Entry_dtbh(Entry_section):
    """Samsung Android device tree table

    This creates a DTBH table, the legacy device-tree table format used by
    some Samsung Android bootloaders.

    Properties / Entry arguments:
        - page-size: DTBH page size, defaults to the parent android-boot page
          size or 2048 when used elsewhere
        - platform: DTBH platform code, defaults to 0x50a6
        - subtype: DTBH subtype code, defaults to 0x217584da

    This entry uses the following subnodes:
        - dtb-*: DTB records, each containing exactly one DTB payload entry

    Each payload DTB must contain these root properties:
        - model_info-chip
        - model_info-hw_rev
        - model_info-hw_rev_end

    Example::

        dtbh {
            dtb-0 {
                u-boot-dtb {
                };
            };
        };
    """

    @staticmethod
    def _DtbEntryName(node):
        return '_dtb_%s' % node.name

    def _GetPayloadSubnodes(self, node):
        return [subnode for subnode in node.subnodes
                if not self.IsSpecialSubnode(subnode)]

    def ReadNode(self):
        super().ReadNode()
        self._page_size = fdt_util.GetInt(self._node, 'page-size')
        if (self._page_size is not None and
                (self._page_size <= 0 or
                 self._page_size & (self._page_size - 1))):
            self.Raise('page-size must be a power of two')
        self.platform = fdt_util.GetInt(self._node, 'platform',
                                        DTBH_PLATFORM_CODE_DEF)
        self.subtype = fdt_util.GetInt(self._node, 'subtype',
                                       DTBH_SUBTYPE_CODE_DEF)

    def ReadEntries(self):
        for node in self._node.subnodes:
            if self.IsSpecialSubnode(node):
                continue

            payloads = self._GetPayloadSubnodes(node)
            if len(payloads) > 1:
                raise ValueError("Node '%s': must contain exactly one DTB "
                                 "payload subnode" % node.path)
            if not payloads:
                continue

            entry = Entry.Create(self, payloads[0],
                                 expanded=self.GetImage().use_expanded,
                                 missing_etype=self.GetImage().missing_etype)
            entry.ReadNode()
            entry.SetPrefix(self._name_prefix)
            self._entries[self._DtbEntryName(node)] = entry

    def _GetPageSize(self):
        if self._page_size is not None:
            return self._page_size

        return getattr(self.section, 'page_size', 2048)

    def _GetDtbData(self, node, required):
        entry = self._entries.get(self._DtbEntryName(node))
        if not entry:
            raise ValueError("Node '%s': Missing required DTB payload subnode" %
                             node.path)

        data = entry.GetData(required)
        if data is None and not required:
            return None

        return data

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

    def BuildSectionData(self, required):
        if not self._node.subnodes:
            raise ValueError("Node '%s': Missing required DTB subnodes" %
                             self._node.path)

        page_size = self._GetPageSize()
        dtbs = []
        for node in self._node.subnodes:
            if self.IsSpecialSubnode(node):
                continue

            data = self._GetDtbData(node, required)
            if data is None and not required:
                return None

            chip = self._GetDtbRootU32(node, data, 'model_info-chip')
            hw_rev = self._GetDtbRootU32(node, data, 'model_info-hw_rev')
            hw_rev_end = self._GetDtbRootU32(node, data,
                                             'model_info-hw_rev_end')
            dtbs.append((chip, self.platform, self.subtype, hw_rev,
                         hw_rev_end, data))

        if not dtbs:
            raise ValueError("Node '%s': Missing required DTB subnodes" %
                             self._node.path)

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
