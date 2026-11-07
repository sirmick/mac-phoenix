FROM debian:13
RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends wine wine64 curl ca-certificates xvfb fonts-dejavu-core && rm -rf /var/lib/apt/lists/*
