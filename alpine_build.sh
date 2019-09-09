#!/bin/sh
cd "$(dirname $0)"
apk add --no-cache alpine-sdk
make LDFLAGS=-static
strip ftrap
