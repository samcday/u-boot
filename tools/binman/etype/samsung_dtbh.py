# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 Linaro Ltd.
#
# Entry-type module for Samsung Exynos DTBH images

import struct

from binman.entry import Entry
from binman.etype.section import Entry_section
from dtoc import fdt_util


DTBH_MAGIC = b'DTBH'
DTBH_VERSION = 2
DTBH_SPACE = 0x20


def _align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def _pad(data, align):
    return data + b'\0' * (_align_up(len(data), align) - len(data))


class Entry_samsung_dtbh(Entry_section):
    """Samsung Exynos legacy device-tree table

    This creates a DTBH v2 image, suitable for Samsung Exynos S-BOOT devices
    using the legacy Android boot-image DT payload. It can either wrap a DTB
    supplied in the dtb subnode, or synthesize a tiny selector DTB from the
    selector subnode.

    Properties / Entry arguments:
        - page-size: Image page size and DTB alignment, defaults to 2048
        - chip: Samsung model_info-chip value
        - platform: Platform code, defaults to 0x50a6
        - subtype: Subtype code, defaults to 0x217584da
        - hw-rev: First supported hardware revision, defaults to 0
        - hw-rev-end: Last supported hardware revision, defaults to 255

    Optional selector subnode properties:
        - compatible: Compatible strings for the synthesized selector DTB
        - memory-base: Base address for the synthesized /memory node,
          defaults to 0x40000000
        - memory-size: Size for the synthesized /memory node, defaults to 0
        - address-cells: Root #address-cells, defaults to 2
        - size-cells: Root #size-cells, defaults to 1

    Optional dtb subnode:
        - dtb: section containing a full DTB to wrap instead of synthesizing a
          selector DTB

    Example::

        dt {
            type = "samsung-dtbh";
            chip = <7870>;
            hw-rev = <0>;
            hw-rev-end = <255>;

            selector {
                compatible = "samsung,on7xelte", "samsung,exynos7870";
            };
        };
    """

    def ReadNode(self):
        super().ReadNode()
        self.page_size = fdt_util.GetInt(self._node, 'page-size', 2048)
        self.chip = fdt_util.GetInt(self._node, 'chip')
        self.platform = fdt_util.GetInt(self._node, 'platform', 0x50a6)
        self.subtype = fdt_util.GetInt(self._node, 'subtype', 0x217584da)
        self.hw_rev = fdt_util.GetInt(self._node, 'hw-rev', 0)
        self.hw_rev_end = fdt_util.GetInt(self._node, 'hw-rev-end', 255)

        if self.page_size <= 0 or self.page_size & (self.page_size - 1):
            self.Raise('page-size must be a power of two')
        if self.chip is None:
            self.Raise("Missing required property 'chip'")
        if self._entries.get('dtb') and self._node.FindNode('selector'):
            self.Raise("Use either 'dtb' or 'selector', not both")

    def ReadEntries(self):
        for node in self._node.subnodes:
            if self.IsSpecialSubnode(node) or node.name == 'selector':
                continue

            if node.name != 'dtb':
                self.Raise("Unexpected subnode '%s'" % node.name)

            entry = Entry.Create(self, node, etype='section',
                                 expanded=self.GetImage().use_expanded,
                                 missing_etype=self.GetImage().missing_etype)
            entry.ReadNode()
            entry.SetPrefix(self._name_prefix)
            self._entries[node.name] = entry

    def _GetU32List(self, node, propname):
        prop = node.props.get(propname)
        if not prop:
            return None

        values = prop.value if isinstance(prop.value, list) else [prop.value]
        return [fdt_util.fdt32_to_cpu(value) for value in values]

    def _GetIntCells(self, node, propname, default):
        values = self._GetU32List(node, propname)
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
    def _IntCells(value, cells):
        return [(value >> (32 * shift)) & 0xffffffff
                for shift in range(cells - 1, -1, -1)]

    def _BuildSelectorDtb(self):
        import libfdt

        selector = self._node.FindNode('selector')
        if not selector:
            self.Raise("Missing 'selector' subnode or 'dtb' subnode")

        compatible = fdt_util.GetStringList(selector, 'compatible')
        if not compatible:
            self.Raise("Missing required property 'compatible' in selector")

        address_cells = fdt_util.GetInt(selector, 'address-cells', 2)
        size_cells = fdt_util.GetInt(selector, 'size-cells', 1)
        memory_base = self._GetIntCells(selector, 'memory-base', 0x40000000)
        memory_size = self._GetIntCells(selector, 'memory-size', 0)

        fsw = libfdt.FdtSw()
        fsw.INC_SIZE = 4096
        fsw.finish_reservemap()

        with fsw.add_node(''):
            fsw.property_u32('#address-cells', address_cells)
            fsw.property_u32('#size-cells', size_cells)
            fsw.property('compatible',
                         b'\0'.join(string.encode('ascii')
                                     for string in compatible) + b'\0')
            fsw.property_u32('model_info-chip', self.chip)
            fsw.property_u32('model_info-hw_rev', self.hw_rev)
            fsw.property_u32('model_info-hw_rev_end', self.hw_rev_end)

            with fsw.add_node('chosen'):
                pass

            with fsw.add_node('memory@%x' % memory_base):
                fsw.property_string('device_type', 'memory')
                reg = self._IntCells(memory_base, address_cells)
                reg += self._IntCells(memory_size, size_cells)
                fsw.property('reg', self._Cells(reg))

        fdt = fsw.as_fdt()
        fdt.pack()
        return bytes(fdt.as_bytearray())

    def _GetDtbData(self, required):
        entry = self._entries.get('dtb')
        if not entry:
            return self._BuildSelectorDtb()

        data = entry.GetData(required)
        if data is None and not required:
            return None

        return data

    def BuildSectionData(self, required):
        dtb = self._GetDtbData(required)
        if dtb is None:
            return None

        dtb_offset = self.page_size
        dtb_size = _align_up(len(dtb), self.page_size)
        header = bytearray(struct.pack('<4sII', DTBH_MAGIC, DTBH_VERSION, 1))
        header += struct.pack('<IIIIIIII', self.chip, self.platform,
                              self.subtype, self.hw_rev, self.hw_rev_end,
                              dtb_offset, dtb_size, DTBH_SPACE)
        header += struct.pack('<I', 0)

        return _pad(header, self.page_size) + _pad(dtb, self.page_size)
