# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 Linaro Ltd.
#
# Entry-type module for Qualcomm QCDT images

import struct

from binman.entry import Entry
from dtoc import fdt_util


QCDT_MAGIC = b'QCDT'


def _align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def _pad(data, align):
    return data + b'\0' * (_align_up(len(data), align) - len(data))


class Entry_qcom_qcdt(Entry):
    """Qualcomm legacy device-tree table

    This creates a Qualcomm QCDT v2 image, suitable for legacy Qualcomm Android
    bootloaders. It synthesizes a tiny selector DTB from properties in the
    binman node and emits QCDT entries for each msm-id/board-id tuple.

    Properties / Entry arguments:
        - page-size: Image page size and DTB alignment, defaults to 2048
        - qcom,msm-id: One or more <platform_id soc_rev> tuples
        - qcom,board-id: One or more <variant_id subtype> tuples
        - memory-base: Base address for the synthesized /memory node
        - memory-size: Size for the synthesized /memory node, defaults to 0

    Example::

        qcdt {
            type = "qcom-qcdt";
            page-size = <2048>;
            qcom,msm-id = <206 0>;
            qcom,board-id = <0xce08ff01 1>;
        };
    """

    def ReadNode(self):
        super().ReadNode()
        self.page_size = fdt_util.GetInt(self._node, 'page-size', 2048)
        self.msm_ids = self._GetTupleList('qcom,msm-id', 2)
        self.board_ids = self._GetTupleList('qcom,board-id', 2)
        self.memory_base = self._GetIntCells('memory-base', 0x80000000)
        self.memory_size = self._GetIntCells('memory-size', 0)

        if self.page_size <= 0 or self.page_size & (self.page_size - 1):
            self.Raise('page-size must be a power of two')

    def _GetU32List(self, propname):
        prop = self._node.props.get(propname)
        if not prop:
            return None

        values = prop.value if isinstance(prop.value, list) else [prop.value]
        return [fdt_util.fdt32_to_cpu(value) for value in values]

    def _GetTupleList(self, propname, width):
        values = self._GetU32List(propname)
        if values is None:
            self.Raise("Missing required property '%s'" % propname)
        if len(values) % width:
            self.Raise("Property '%s' must contain %d-cell tuples" %
                       (propname, width))

        return [tuple(values[idx:idx + width])
                for idx in range(0, len(values), width)]

    def _GetIntCells(self, propname, default):
        values = self._GetU32List(propname)
        if values is None:
            return default
        if len(values) == 1:
            return values[0]
        if len(values) == 2:
            return values[0] << 32 | values[1]

        self.Raise("Property '%s' must contain one or two cells" % propname)

    @staticmethod
    def _Cells(values):
        return struct.pack('>%dI' % len(values), *values)

    @staticmethod
    def _AddrCells(value):
        return [(value >> 32) & 0xffffffff, value & 0xffffffff]

    def _BuildSelectorDtb(self):
        import libfdt

        fsw = libfdt.FdtSw()
        fsw.INC_SIZE = 4096
        fsw.finish_reservemap()

        with fsw.add_node(''):
            fsw.property_u32('#address-cells', 2)
            fsw.property_u32('#size-cells', 2)
            fsw.property('qcom,msm-id',
                         self._Cells([cell for tup in self.msm_ids
                                      for cell in tup]))
            fsw.property('qcom,board-id',
                         self._Cells([cell for tup in self.board_ids
                                      for cell in tup]))

            with fsw.add_node('chosen'):
                pass

            with fsw.add_node('memory@%x' % self.memory_base):
                fsw.property_string('device_type', 'memory')
                reg = self._AddrCells(self.memory_base)
                reg += self._AddrCells(self.memory_size)
                fsw.property('reg', self._Cells(reg))

        fdt = fsw.as_fdt()
        fdt.pack()
        return bytes(fdt.as_bytearray())

    def ObtainContents(self):
        dtb = self._BuildSelectorDtb()
        dtb_offset = self.page_size
        dtb_size = _align_up(len(dtb), self.page_size)

        qcdt = bytearray(struct.pack('<4sII', QCDT_MAGIC, 2,
                                     len(self.msm_ids) *
                                     len(self.board_ids)))
        for platform_id, soc_rev in self.msm_ids:
            for variant_id, subtype in self.board_ids:
                qcdt += struct.pack('<IIIIII', platform_id, variant_id,
                                    subtype, soc_rev, dtb_offset, dtb_size)

        qcdt = _pad(qcdt, self.page_size) + _pad(dtb, self.page_size)
        self.SetContents(bytes(qcdt))

        return True
