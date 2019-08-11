FROM alpine:3.10

RUN apk add --no-cache g++ linux-headers make

COPY . /alephzero
WORKDIR /alephzero

CMD ["make", "test"]

