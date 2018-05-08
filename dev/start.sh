#!/usr/bin/env bash 
git submodule init
git submodule update

echo "WELCOME! to Kinesis development"
echo ""
echo "Run sh dev/init_setup.sh for first time configuration"
echo "Run sh dev/test.sh from WITHIN the container to run the tests"
echo "Run make from WITHIN the docker container for development. Stellar stack must be removed for these to take effect"
echo ""
echo "This script will leave you inside the container"
echo ""

docker pull postgres:9.6
docker pull abxit/stellar-core-dev

docker run --name pgstellar --rm -d postgres:9.6

# Wait for the DB to boot
sleep 15

# Create the needed DBs
docker exec --user postgres pgstellar psql -c "create database test;"
for i in $(seq 0 15)
do
    docker exec --user postgres pgstellar psql -c "create database test$i;"
done

docker run -v $(pwd):/usr/src -e PGHOST=pgstellar -e PGUSER=postgres --link pgstellar -w /usr/src -it abxit/stellar-core-dev sh -c "useradd $USER \
    && su $USER -c sh -c \"cd /usr/src && source ./dev/permission_setup.sh \
    && bash\" "