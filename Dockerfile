FROM abxit/stellar-core-dev

# All compilation requirements above
COPY . /usr/src
WORKDIR /usr/src

RUN ./autogen.sh && ./configure
RUN make
