FROM gcc:latest AS builder

RUN apt update && apt install -y libmicrohttpd-dev

WORKDIR /tmp
RUN wget https://github.com/digitalocean/prometheus-client-c/releases/download/v0.1.3/libprom-dev-0.1.3-Linux.deb
RUN wget https://github.com/digitalocean/prometheus-client-c/releases/download/v0.1.3/libpromhttp-dev-0.1.3-Linux.deb
RUN dpkg -i libprom-dev-0.1.3-Linux.deb libpromhttp-dev-0.1.3-Linux.deb

WORKDIR /ioping
ADD . .
RUN make

FROM ubuntu:20.04

RUN apt update && apt install -y libmicrohttpd-dev curl
WORKDIR /tmp
RUN curl -L https://github.com/digitalocean/prometheus-client-c/releases/download/v0.1.3/libprom-dev-0.1.3-Linux.deb -o libprom-dev-0.1.3-Linux.deb
RUN curl -L https://github.com/digitalocean/prometheus-client-c/releases/download/v0.1.3/libpromhttp-dev-0.1.3-Linux.deb -o libpromhttp-dev-0.1.3-Linux.deb
RUN dpkg -i libprom-dev-0.1.3-Linux.deb libpromhttp-dev-0.1.3-Linux.deb
RUN rm -rf libprom-dev-0.1.3-Linux.deb libpromhttp-dev-0.1.3-Linux.deb

WORKDIR /
COPY --from=builder /ioping/ioping /ioping

ENTRYPOINT [ "./ioping" ]
