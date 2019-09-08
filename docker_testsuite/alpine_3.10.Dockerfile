FROM alpine:3.10

RUN apk add --no-cache g++ make

COPY . /alephzero/alephzero
WORKDIR /alephzero/alephzero

CMD ["make", "test", "-j"]

