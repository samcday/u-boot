.. SPDX-License-Identifier: GPL-2.0+
.. sectionauthor:: Kaustabh Chakraborty <kauschluss@disroot.org>

Samsung Exynos 7870 Boards
==========================

Building
--------

Build U-Boot with the S-BOOT image fragment enabled. Replace ``<dtb>`` with the
upstream DTB path for the target device, for example
``exynos/exynos7870-on7xelte``.

.. prompt:: bash $

	make O=.output exynos-mobile_defconfig sboot-exynos7870.config
	make DEVICE_TREE=<dtb> O=.output -j$(nproc)

This produces a legacy Android boot image for S-BOOT using binman. For example,
``exynos/exynos7870-on7xelte`` produces:

.. prompt:: bash $

	.output/u-boot-samsung-on7xelte.img

Flashing
--------
If flashing for the first time, it must be done via Samsung's Download (Odin)
mode. Heimdall [1]_ can be used for flashing, like so:

.. prompt:: bash $

	heimdall flash --BOOT .output/u-boot-samsung-on7xelte.img

However, if U-Boot is already installed, you may also use its fastboot interface
for flashing. Boot into the boot menu by holding the volume down key. Enable
fastboot mode from there, connect the device to your host, then run:

.. prompt:: bash $

	fastboot flash boot .output/u-boot-samsung-on7xelte.img

To flash an OS image in internal storage, fastboot is a reliable option.

References
----------
.. [1] https://git.sr.ht/~grimler/Heimdall
