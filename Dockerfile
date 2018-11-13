FROM ubuntu:16.04

## Install build essential
RUN  apt-get clean && \
    apt-get update && \
    apt-get install -y make vim wget git && \
	apt-get install -y cron && \
	apt-get install -y libmicrohttpd-dev libjansson-dev  gtk-doc-tools && \
	apt-get install -y libssl-dev libsrtp-dev libsofia-sip-ua-dev libglib2.0-dev  && \
	apt-get install -y libopus-dev libogg-dev libcurl4-openssl-dev libavutil-dev libavcodec-dev libavformat-dev  && \
    apt-get install -y pkg-config gengetopt libtool automake cmake

## Keep the commands which don't change above this, will save time in container build
## Accessing the variables at later stage will avoid redoing constant steps defined above
ARG TURN_SERVER
ARG TURN_PORT
ARG TURN_TYPE
ARG TURN_USER
ARG TURN_PWD

ENV SRC_PATH /src
ENV RECORDING_PATH /recordings

## ADD Janus Code, shouldn't it be clone from gitlab ?
RUN mkdir -p $SRC_PATH

RUN	apt-get purge -y libsrtp0 libsrtp0-dev && \
    cd ${SRC_PATH} && wget -nv https://github.com/cisco/libsrtp/archive/v1.5.4.tar.gz && tar xf v1.5.4.tar.gz && cd libsrtp-1.5.4  && ./configure --prefix=/usr --enable-openssl && make shared_library && make install && \
    cd ${SRC_PATH} && wget -nv https://github.com/warmcat/libwebsockets/archive/v2.4.1.tar.gz && tar xf v2.4.1.tar.gz && cd libwebsockets-2.4.1 && mkdir build && cd build &&  cmake -DLWS_MAX_SMP=1 -DCMAKE_INSTALL_PREFIX:PATH=/usr -DCMAKE_C_FLAGS="-fpic" .. && make && make install && \
    cd ${SRC_PATH} && git clone https://gitlab.freedesktop.org/libnice/libnice && cd libnice && git checkout 5496500b1535d9343fdac2a3408864643fe65d7e && ./autogen.sh && ./configure --prefix=/usr && make && make install && \
    cd ${JANUS_PATH} && ./autogen.sh &&  ./configure --prefix=/usr/local --disable-data-channels --disable-rabbitmq --disable-mqtt --disable-docs --disable-plugin-recordplay --disable-plugin-streaming --disable-plugin-sip --disable-plugin-audiobridge --disable-plugin-videocall && make && make install && make configs

ENV JANUS_PATH /janus-gateway
COPY . $JANUS_PATH/
WORKDIR $JANUS_PATH

## Configure Janus and dependencies
RUN JANUS_PATH=$JANUS_PATH \
    RECORDING_PATH=$RECORDING_PATH \
    TURN_SERVER=$TURN_SERVER \
    TURN_PORT=$TURN_PORT \
    TURN_TYPE=$TURN_TYPE \
    TURN_USER=$TURN_USER \
    TURN_PWD=$TURN_PWD \
    ./scripts/configure_build.sh

CMD ["sh", "-c", "scripts/run_janus.sh"]

