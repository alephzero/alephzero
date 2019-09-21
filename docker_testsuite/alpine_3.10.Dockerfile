FROM alpine:3.10

RUN apk add --no-cache g++ linux-headers make

COPY . /alephzero/alephzero
WORKDIR /alephzero/alephzero

CMD ["make", "test", "-j"]

