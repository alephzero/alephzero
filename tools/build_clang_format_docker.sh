cd $(dirname "$(readlink -f "$0")")/..

docker build -t alephzero-clang-format -f ./tools/clang-format.Dockerfile tools/
