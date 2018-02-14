#!/usr/bin/env bash
git submodule init
git submodule update

echo "Run ./autogen.sh && ./configure to finalise the setup"
echo "Compile using make. Ensure to do a full rm of the stellar stack to deploy changes"
echo "\n"
echo "On the pg container, run the loop declared in dev_db.sh as the postgres user"
echo "\n"
echo "To run the tests, set up the pg databases and then run:"
echo "make -j3"
echo "export ALL_VERSIONS=1"
echo "make check"

docker pull postgres:9.6
docker pull abxit/stellar-core-dev
docker run --name pgstellar --rm -d postgres:9.6
docker run -v $(pwd):/usr/src -e PGHOST=pgstellar -e PGUSER=postgres --link pgstellar -w /usr/src --rm -it abxit/stellar-core-dev
