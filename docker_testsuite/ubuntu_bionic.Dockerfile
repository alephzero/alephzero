FROM ubuntu:bionic

RUN apt update && apt install -y \
    g++ \
    make && \
    rm -rf /var/lib/apt/lists/*

COPY . /alephzero
WORKDIR /alephzero

CMD ["make", "test"]

