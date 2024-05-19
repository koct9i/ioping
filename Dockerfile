# Stage 1: Build ioping
# FROM gcc:latest AS builder

# FROM ubuntu:20.04 AS builder
FROM ubuntu:22.04 AS builder

# Install required packages
RUN apt update && apt install -y libmicrohttpd-dev wget make gcc

WORKDIR /tmp

# Download and install prometheus libraries
RUN wget https://github.com/digitalocean/prometheus-client-c/releases/download/v0.1.3/libprom-dev-0.1.3-Linux.deb
RUN wget https://github.com/digitalocean/prometheus-client-c/releases/download/v0.1.3/libpromhttp-dev-0.1.3-Linux.deb
RUN dpkg -i libprom-dev-0.1.3-Linux.deb libpromhttp-dev-0.1.3-Linux.deb

WORKDIR /ioping
ADD . .
RUN make

# FROM ubuntu:20.04
FROM ubuntu:22.04

# Install required packages
RUN apt update && apt install -y libmicrohttpd-dev wget

WORKDIR /tmp

# Download and install prometheus libraries
RUN wget https://github.com/digitalocean/prometheus-client-c/releases/download/v0.1.3/libprom-dev-0.1.3-Linux.deb
RUN wget https://github.com/digitalocean/prometheus-client-c/releases/download/v0.1.3/libpromhttp-dev-0.1.3-Linux.deb
RUN dpkg -i libprom-dev-0.1.3-Linux.deb libpromhttp-dev-0.1.3-Linux.deb
RUN rm -rf libprom-dev-0.1.3-Linux.deb libpromhttp-dev-0.1.3-Linux.deb

WORKDIR /
COPY --from=builder /ioping/ioping /ioping

ENTRYPOINT [ "/ioping" ]


# Stage 2: Create runtime image
# FROM ubuntu:22.04


