SUBDIR=	src \
	lib \
	x11 \
	doc

.if exists(nvml)
SUBDIR+=	nvml
.endif

.if exists(firmware)
SUBDIR+=	firmware
.endif

afterinstall:
	@${.CURDIR}/scripts/setup.sh ${.CURDIR}/src
	@${.CURDIR}/scripts/linux.sh
	@echo
	@echo "Installation of the NVIDIA Accelerated Graphics Driver"
	@echo "570.133.07 for FreeBSD is now complete.  You can now"
	@echo "run the nvidia-xconfig utility to automatically update"
	@echo "your X server configuration file.  Please see the README"
	@echo "for details if you wish to update your X configuration"
	@echo "file manually."
	@echo

setup:	install

.include <bsd.subdir.mk>
