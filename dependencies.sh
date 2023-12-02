#!/bin/bash
set -e

USER=$( id -u -n )
SUDO=
[ $USER = root ] || SUDO=sudo

run() { echo >&2 "# $@" ; $SUDO $@ ; }
say() { echo >&2 "$@" ; }
die() { echo >&2 "$@" ; exit 1 ; }

# run dependencies.sh from blot project (ignore errors)
./blot/dependencies.sh || true

if [ -s /etc/redhat-release ]; then

        die "RHEL/CentOS not yet supported"

        run yum install -y devtoolset-7

        run yum install -y kernel kernel-devel dkms
        run yum install "kernel-devel-uname-r == $(uname -r)"

elif [ -f /etc/SuSE-release ] || [ -f /etc/SUSE-brand ]; then

        die "SuSE not yet supported"

elif [ -f /etc/debian_version ]; then

        say "Installing Debian/Ubuntu dependencies"

        run apt install -y build-essential cmake ninja-build \
                git gcc gcc-multilib g++ autoconf automake \
                libtool net-tools flawfinder zlib1g-dev \
                libglib2.0-dev liburiparser-dev libev-dev \
                libbz2-dev liblzma-dev libnetpbm10-dev libgif-dev \
                libnuma-dev dpkg-dev fakeroot dkms cscope exuberant-ctags

        # for libjemalloc build

        if ! run apt build-dep -y libjemalloc-dev ; then
                run apt install -y debhelper docbook-xsl xsltproc
        fi

        if grep Ubuntu /etc/lsb-release ; then
                say "Installing Ubuntu specific dependencies"

                run apt install linux-headers-generic

        else
                say "Installing Debian specific dependencies"

                run apt install linux-headers-amd64

        fi

else
        die "Distribution is not handled"
fi

