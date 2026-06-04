# SPDX-License-Identifier: GPL-2.0+
# Entry-type module for Qualcomm Android device tree tables

import struct

from binman.entry import Entry
from binman.etype.section import Entry_section
from dtoc import fdt_util


QCDT_MAGIC = b'QCDT'
QCDT_VERSION = 2


def _align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def _pad(data, align):
    return data + b'\0' * (_align_up(len(data), align) - len(data))


class Entry_qcdt(Entry_section):
    """Qualcomm Android device tree table

    This creates a QCDT table, the legacy device-tree table format used by
    some Qualcomm Android bootloaders.

    Properties / Entry arguments:
        - page-size: QCDT page size, defaults to the parent android-boot page
          size or 2048 when used elsewhere

    This entry uses the following subnodes:
        - dtb-*: DTB records, each containing qcom,msm-id, qcom,board-id and
          exactly one DTB payload entry

    Example::

        qcdt {
            dtb-0 {
                qcom,msm-id = <206 0>;
                qcom,board-id = <0xce08ff01 1>;

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

    def BuildSectionData(self, required):
        if not self._node.subnodes:
            raise ValueError("Node '%s': Missing required DTB subnodes" %
                             self._node.path)

        page_size = self._GetPageSize()
        dtbs = []
        for node in self._node.subnodes:
            if self.IsSpecialSubnode(node):
                continue

            msm_id = self._GetU32Tuple(node, 'qcom,msm-id', 2)
            board_id = self._GetU32Tuple(node, 'qcom,board-id', 2)
            data = self._GetDtbData(node, required)
            if data is None and not required:
                return None

            dtbs.append((msm_id, board_id, data))

        if not dtbs:
            raise ValueError("Node '%s': Missing required DTB subnodes" %
                             self._node.path)

        dtb_offset = _align_up(12 + len(dtbs) * 24, page_size)
        records = []
        payloads = bytearray()
        for msm_id, board_id, dtb in dtbs:
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
