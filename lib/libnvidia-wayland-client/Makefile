NVIDIA_ROOT=	${.CURDIR}/../..

.if !defined(LOCALBASE)
LOCALBASE=	/usr/local
.endif

LIB=		   nvidia-wayland-client
SHLIB_VERSION=	570.133.07
SHLIB_NAME=    lib${LIB}.so.${SHLIB_VERSION}
SHLIB_NO_LINK= 1
LIBDIR=		   ${LOCALBASE}/lib

.include <${NVIDIA_ROOT}/mk/nvidia.lib.mk>
