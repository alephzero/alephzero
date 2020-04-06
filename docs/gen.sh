#!/bin/bash

cd "$(dirname "$0")"

docker build -t alephzero_docs .

docker run \
  --rm \
  -it \
  -v ${PWD}/..:/alephzero \
  -w /alephzero/docs \
  alephzero_docs \
  sh -c "make clean; make doxygen; make html"
