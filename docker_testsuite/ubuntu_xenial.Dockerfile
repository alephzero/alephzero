FROM ubuntu:xenial

RUN apt update && apt install -y \
    g++ \
    make && \
    rm -rf /var/lib/apt/lists/*

COPY . /alephzero/alephzero
WORKDIR /alephzero/alephzero

CMD ["make", "test", "-j"]

