#!/bin/bash
cd "$(dirname "$0")"

pushd ..
make clean
popd

docker build -t alephzero_testsuite_alpine_3.10 -f alpine_3.10.Dockerfile ..
docker build -t alephzero_testsuite_ubuntu_bionic -f ubuntu_bionic.Dockerfile ..
docker build -t alephzero_testsuite_ubuntu_disco -f ubuntu_disco.Dockerfile ..

echo -e "\e[1m\e[92mTesting on alpine:3.10\e[0m"
docker run --rm -it alephzero_testsuite_alpine_3.10

echo -e "\e[1m\e[92mTesting on ubuntu:bionic\e[0m"
docker run --rm -it alephzero_testsuite_ubuntu_bionic

echo -e "\e[1m\e[92mTesting on ubuntu:disco\e[0m"
docker run --rm -it alephzero_testsuite_ubuntu_disco
