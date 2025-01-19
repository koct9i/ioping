FROM alpine:latest as alpine-build
RUN apk --no-cache add make gcc musl-dev linux-headers
WORKDIR /src
COPY . .
RUN make static

FROM scratch AS alpine-bin
COPY --from=alpine-build /src/ioping /

###

FROM debian:latest as debian-build
RUN apt update && apt install -y --no-install-recommends make gcc libc6-dev
WORKDIR /src
COPY . .
RUN make static

FROM scratch AS debian-bin
COPY --from=debian-build /src/ioping /

###

FROM fedora:latest as fedora-build
RUN dnf install -y make gcc glibc-static
WORKDIR /src
COPY . .
RUN make static

FROM scratch AS fedora-bin
COPY --from=fedora-build /src/ioping /

###

FROM ubuntu:latest as ubuntu-build
RUN apt update && apt install -y --no-install-recommends make gcc libc6-dev
WORKDIR /src
COPY . .
RUN make static

FROM scratch AS ubuntu-bin
COPY --from=ubuntu-build /src/ioping /

###
