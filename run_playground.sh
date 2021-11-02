#!/bin/bash
cd "$(dirname "$0")"

if [[ "$(docker images -q alephzero/playground 2> /dev/null)" == "" ]]; then
  echo "TODO: This requires alephzero/playground"
  exit 1
fi

docker run                          \
  --rm                              \
  -it                               \
  -v "${PWD}":/alephzero/alephzero/ \
  -p 12385:12385                    \
  alephzero/playground
