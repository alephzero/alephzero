FROM alpine:3.10

RUN apk add --no-cache clang g++ linux-headers musl-dev make

COPY . /alephzero/alephzero
WORKDIR /alephzero/alephzero

ENV CC=clang
ENV CXX=clang++
CMD ["make", "test", "-j"]
