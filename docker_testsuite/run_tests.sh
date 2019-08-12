#!/bin/bash
cd "$(dirname "$0")"

pushd ..
make clean
popd

docker build -t alephzero_testsuite_alpine_3.10 -f alpine_3.10.Dockerfile ..
docker build -t alephzero_testsuite_ubuntu_xenial -f ubuntu_xenial.Dockerfile ..
docker build -t alephzero_testsuite_ubuntu_bionic -f ubuntu_bionic.Dockerfile ..

docker run --rm -it alephzero_testsuite_alpine_3.10
docker run --rm -it alephzero_testsuite_ubuntu_xenial
docker run --rm -it alephzero_testsuite_ubuntu_bionic
