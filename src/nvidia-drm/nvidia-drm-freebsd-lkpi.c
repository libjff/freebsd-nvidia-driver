/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Include the FreeBSD structures (nvidia_softc)
 * which have to be included from src/nvidia/nv-freebsd.h
 *
 * This has to be done first before the LIST_HEAD linux version.
 */
#include "nvmisc.h"
#define NVRM
#include "../nvidia/nv.h"
#include "../nvidia/nv-freebsd.h"

/* undef BIT, since it was just identically defined in nvmisc.h */
#undef BIT
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>

#include "nvidia-drm-os-interface.h"
#include "nvidia-drm.h"

#include "nvidia-drm-conftest.h"

#if defined(NV_LINUX_SYNC_FILE_H_PRESENT)
#include <linux/file.h>
#include <linux/sync_file.h>
#endif

#if defined(NV_DRM_AVAILABLE)

#include "nv-mm.h"

#include "nv-gpu-info.h"
#include "nvidia-drm-drv.h"
#include "nvidia-drm-priv.h"
#include "nv-pci-table.h"

#include <linux/device.h>
#include <linux/vmalloc.h>

SYSCTL_NODE(_hw, OID_AUTO, nvidiadrm, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "nvidia-drm kernel module parameters");
SYSCTL_BOOL(_hw_nvidiadrm, OID_AUTO, modeset,  CTLFLAG_RDTUN | CTLFLAG_MPSAFE,
    &nv_drm_modeset_module_param,  1,
    "Enable atomic kernel modesetting (1 = enable, 0 = disable (default))");
#if defined(NV_DRM_FBDEV_AVAILABLE)
SYSCTL_BOOL(_hw_nvidiadrm, OID_AUTO, fbdev,  CTLFLAG_RDTUN | CTLFLAG_MPSAFE,
    &nv_drm_fbdev_module_param,  1,
    "Enable atomic kernel modesetting (1 = enable (default), 0 = disable");
#endif

#if __FreeBSD_version < 1300000
static inline void
lkpifill_pci_dev(device_t dev, struct pci_dev *pdev)
{

       pdev->devfn = PCI_DEVFN(pci_get_slot(dev), pci_get_function(dev));
       pdev->vendor = pci_get_vendor(dev);
       pdev->device = pci_get_device(dev);
       pdev->class = pci_get_class(dev);
       pdev->revision = pci_get_revid(dev);
       pdev->dev.bsddev = dev;
       pdev->bus->self = pdev;
       pdev->bus->number = pci_get_bus(dev);
       pdev->bus->domain = pci_get_domain(dev);
}

static inline struct pci_dev *
lkpinew_pci_dev(device_t dev)
{
       struct pci_dev *pdev;
       struct pci_bus *pbus;

       pdev = malloc(sizeof(*pdev), M_DEVBUF, M_WAITOK|M_ZERO);
       pbus = malloc(sizeof(*pbus), M_DEVBUF, M_WAITOK|M_ZERO);
       pdev->bus = pbus;
       lkpifill_pci_dev(dev, pdev);
       return (pdev);
}
#endif

/*************************************************************************
 * FreeBSD linuxkpi based loading support code.
 *************************************************************************/

static struct pci_dev *nv_lkpi_pci_devs[NV_MAX_DEVICES];

int nv_drm_probe_devices(void)
{
    nv_drm_update_drm_driver_features();

    /*
     * Conveniently we can get all of the nvidia devices that were initialized
     * by the native nvidia.ko by using our devclass.
     */
    for (int i = 0; i < NV_MAX_DEVICES; i++) {
        struct pci_dev *pdev;
        nv_gpu_info_t gpu_info;
        struct nvidia_softc *sc = devclass_get_softc(nvidia_devclass, i);
        if (!sc) {
            nv_lkpi_pci_devs[i] = NULL;
            continue;
        }
        nv_state_t *nv = sc->nv_state;

        /*
         * Set the ivars for this device if they are not already populated. This
         * is the bus specific data, and linuxkpi will try to use it.
         */
        if (!device_get_ivars(sc->dev)) {
            device_t parent = device_get_parent(sc->dev);
            struct pci_devinfo *dinfo = device_get_ivars(parent);
            device_set_ivars(sc->dev, dinfo);
        }

        /*
         * Now we have the state (which gives us the device_t), but what nvidia-drm
         * wants is a pci_dev suitable for use with linuxkpi code. We can use
         * lkpinew_pci_dev to fill in a pci_dev struct, or linux_pci_attach on more
         * recent kernels (introduced by 253dbe7487705).
         */
#if __FreeBSD_version < 1300093
        pdev = lkpinew_pci_dev(sc->dev);
        if (!pdev) {
            NV_DRM_LOG_ERR("Failed to allocate linuxkpi PCI device");
            return -ENOMEM;
        }
#else
        pdev = malloc(sizeof(*pdev), M_DEVBUF, M_WAITOK|M_ZERO);
        if (!pdev) {
            NV_DRM_LOG_ERR("Failed to allocate linuxkpi PCI device");
            return -ENOMEM;
        }

        if (linux_pci_attach_device(sc->dev, NULL, NULL, pdev)) {
            NV_DRM_LOG_ERR("Failed to attach linuxkpi PCI device");
            free(pdev, M_DEVBUF);
            return -ENOMEM;
        }
#endif
        nv_lkpi_pci_devs[i] = pdev;

        gpu_info.gpu_id = nv->gpu_id;

        gpu_info.pci_info.domain   = nv->pci_info.domain;
        gpu_info.pci_info.bus      = nv->pci_info.bus;
        gpu_info.pci_info.slot     = nv->pci_info.slot;
        gpu_info.pci_info.function = nv->pci_info.function;
        gpu_info.os_device_ptr = pdev;

        nv_drm_register_drm_device(&gpu_info);
    }

    return 0;
}

static void nv_bsd_drm_exit(void)
{
    nv_drm_exit();

    // After doing the common DRM teardown, free any linuxkpi devices we
    // allocated during init
    for (int i = 0; i < NV_MAX_DEVICES; i++) {
        if (nv_lkpi_pci_devs[i]) {
#if __FreeBSD_version < 1300093
            kobject_put(&nv_lkpi_pci_devs[i]->dev.kobj);
#else
            linux_pci_detach_device(nv_lkpi_pci_devs[i]);
            free(nv_lkpi_pci_devs[i], M_DEVBUF);
#endif
        }
    }
}

LKPI_DRIVER_MODULE(nvidia_drm, nv_drm_init, nv_bsd_drm_exit);
LKPI_PNP_INFO(pci, nvidia_drm, nv_module_device_table);
MODULE_DEPEND(nvidia_drm, linuxkpi, 1, 1, 1);
MODULE_DEPEND(nvidia_drm, drmn, 2, 2, 2);
MODULE_DEPEND(nvidia_drm, dmabuf, 1, 1, 1);
MODULE_DEPEND(nvidia_drm, nvidia, 1, 1, 1);
MODULE_DEPEND(nvidia_drm, nvidia_modeset, 1, 1, 1);
#endif /* NV_DRM_AVAILABLE */
