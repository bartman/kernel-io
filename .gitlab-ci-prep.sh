#!/bin/bash
# install our dependencies, using local debian cache to save on downloads

set -e -x

# enable for debug (see GitLab CI variables)
#export

# https://gitlab.com/gitlab-org/gitlab-runner/issues/991
# ended up using squid-deb-proxy...
if [ -d /etc/apt/apt.conf.d ] ; then
        echo "10.10.11.232 ci.jukie.net" >> /etc/hosts
        echo "Acquire::http::Proxy \"http://ci.jukie.net:8000/\";" > /etc/apt/apt.conf.d/apt-proxy.conf
fi
apt update -qq

# update submodules (now taken care of by GIT_SUBMODULE_STRATEGY=recursive)
#apt install -y -qq git
#git submodule sync --recursive
#git submodule update --init --recursive

# we need to do this dance to remove grafted commits, such that git-describe works
apt install -y -qq git

git fetch origin --tags --unshallow || git fetch origin --tags
git describe --always --tags | sed -r -e 's/^v([0-9.-]*)(-g.*|)$$/\1/'

# install all the other packages required for stage
./dependencies.sh
