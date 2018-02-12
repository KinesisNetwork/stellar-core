#!/usr/bin/env bash

git submodule init
git submodule update

echo "Run ./autogen.sh && ./configure to finalise the setup"
echo "Compile using make. Ensure to do a full rm of the stellar stack to deploy changes"
docker run -v $(pwd):/usr/src -w /usr/src -it abxit/stellar-core-dev sh -c "chown $USER:$USER . && ./autogen.sh && ./configure && bash"

