FROM abxit/stellar-core-dev as builder

# All compilation requirements above
COPY . /usr/src
WORKDIR /usr/src

RUN git submodule init && git submodule update
RUN ./autogen.sh && ./configure
RUN make

FROM stellar/base:latest

COPY --from=builder /usr/src/src/stellar-core /usr/local/bin/stellar-core

MAINTAINER Mat Schaffer <mat@stellar.org>

ENV STELLAR_CORE_VERSION 9.1.0-506-14017829

EXPOSE 11625
EXPOSE 11626

VOLUME /data
VOLUME /postgresql-unix-sockets
VOLUME /heka

ADD docker/install /
RUN /install

ADD docker/heka /heka
ADD docker/confd /etc/confd
ADD docker/utils /utils
ADD docker/start /

CMD ["/start"]
