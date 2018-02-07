#!/usr/bin/env bash

git submodule init
git submodule update

# docker pull abxit/stellar-core-dev
docker run -v $(pwd):/usr/src -w /usr/src -it abxit/stellar-core-dev
