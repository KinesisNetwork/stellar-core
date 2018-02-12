#!/usr/bin/env bash

git submodule init
git submodule update

echo 'Compile using make. Ensure to do a full rm of the stellar stack to deploy changes'
docker run -v $(pwd):/usr/src -w /usr/src -it abxit/stellar-core-dev sh -c "useradd $USER \
    && su $USER -c sh -c \"cd /usr/src && source ./setup.sh && ./autogen.sh && ./configure \
    && echo 'Compile using make. Ensure to do a full rm of the stellar stack to deploy changes' \
    && bash\" "
