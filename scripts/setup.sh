#!/bin/sh

srcdir="$1"
if [ ! -d "${srcdir}" ] ; then
    echo "$0: ERROR: First argument must be source directory"
    exit 1
fi

unload_module()
{
    _module=$1
    kldstat -n ${_module} > /dev/null 2>&1; RESULT=$?
    if [ ${RESULT} -eq 0 ]; then
        kldunload -n ${_module} > /dev/null 2>&1; RESULT=$?
        if [ ${RESULT} -ne 0 ]; then
            echo 'ERROR: Failed to unload the ${_module} module!'
            echo 'ERROR: Is ${_module}.ko in use?'
            exit 1;
        fi
    fi
}

module_exists_in_package()
{
    _module=$1

    [ -d "${srcdir}/${_module}" ]
}

load_module()
{
    _module=$1

    if ! module_exists_in_package "${_module}"; then
        return
    fi
    kldload ${_module} > /dev/null 2>&1 ; RESULT=$?
    if [ ${RESULT} -ne 0 ]; then
        echo 'ERROR: Failed to load the ${_module} module!'
        exit 1;
    fi
}

load_module_on_boot()
{
    _prefix=$1
    _module=$2

    if ! module_exists_in_package "${_module}"; then
        return
    fi

    # Use the sysrc tool to add nvidia-modeset to the list of
    # kernel modules loaded on boot
    #
    # Old versions of the driver would load modules from loader.conf.
    # We no longer do this, as they can be quite large, especially for
    # debug builds. The kernel can have trouble growing the reserved
    # memory large enough during the bootloader stage, so instead we
    # load it through rc.conf where it will be loaded later in the boot
    # process.
    # Remove any directives to load the modules from loader.conf which
    # may be left over from an older driver installation.
    sed -e /${_prefix}_load=.*/d -i.orig /boot/loader.conf
    sed -e /${_prefix}_name=\"${_module}\"/d -i.orig /boot/loader.conf

    sysrc kld_list+="${_module}"
}

unload_module "nvidia-modeset"
unload_module "nvidia"

load_module "nvidia"
load_module "nvidia-modeset"

load_module_on_boot "nvidia" "nvidia"
load_module_on_boot "nvidia_modeset" "nvidia-modeset"

