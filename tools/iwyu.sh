cd $(dirname "$(readlink -f "$0")")/..

docker build -t alephzero_iwyu -f tools/iwyu.Dockerfile tools/

sudo make clean
docker run \
    --rm \
    -it \
    -v ${PWD}:/alephzero/alephzero \
    -w /alephzero/alephzero \
    alephzero_iwyu sh -c 'bear make install -j && /include-what-you-use/iwyu_tool.py -o clang -p .'
