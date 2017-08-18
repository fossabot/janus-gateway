## TODO
#  Mount a volume and dump all the logs into that otherwise logs will be lost as soon as container exits
#  Configure docker to pick certificates from mount volume
#  Evaluate option of creating Nginx as seperate image
#  Check if size of docker image can be reduced in production image, only binaries of janus ? no code ?? 

# set base image debian jessie
FROM ubuntu:16.04
ENV  ETHER_HOME /usr/local/ether
ENV  JANUS_HOME $ETHER_HOME/janus-gateway

RUN mkdir -p $JANUS_HOME

RUN apt-get update

## Install Nginx
RUN apt-get install -y nginx

## Install tools
RUN apt-get install -y vim

## Install Janus Dependencies
RUN apt-get install -y libmicrohttpd-dev libjansson-dev libnice-dev \
    libssl-dev libsrtp-dev libsofia-sip-ua-dev libglib2.0-dev \
    libopus-dev libogg-dev libcurl4-openssl-dev libavutil-dev libavcodec-dev libavformat-dev \
    pkg-config gengetopt libtool automake wget make git


## Configure Nginx for HTTPS port 443
RUN sed -i 's/# listen 443 ssl/listen 443 ssl/g' /etc/nginx/sites-available/default
RUN sed -i 's/# listen \[::\]:443/listen \[::\]:443/g' /etc/nginx/sites-available/default      
RUN sed -i "25i\\\tssl_certificate $JANUS_HOME\/certs\/mycert.pem;" /etc/nginx/sites-available/default 
RUN sed -i "26i\\\tssl_certificate_key $JANUS_HOME\/certs\/mycert.key;" /etc/nginx/sites-available/default

## Configure Nginx for Janus request redirection and static file serving
RUN sed -i '45i\\tlocation \/janus-meet {\n\t\trewrite \^\/janus-meet(\.\*) \$1 break;\n\t\tproxy_pass http:\/\/127.0.0.1:8088;\n\t }' \
           /etc/nginx/sites-available/default
RUN sed -i '54i\\tlocation \/recordings {\n\t\tautoindex on;\n\t }' \
           /etc/nginx/sites-available/default
RUN sed -i "s@root \/var\/www\/html@root $JANUS_HOME\/html@g" /etc/nginx/sites-available/default


## Install LibSRTP
RUN  apt-get purge -y libsrtp0 libsrtp0-dev
RUN  cd $ETHER_HOME && \
     wget https://github.com/cisco/libsrtp/archive/v1.5.4.tar.gz && \
     tar xfv v1.5.4.tar.gz && \
     cd libsrtp-1.5.4  && \
     ./configure --prefix=/usr --enable-openssl && \
     make shared_library && make install

## ADD Janus code, shouldn't it be clone from gitlab ?
ADD .      $JANUS_HOME/

## Build and configure Janus
RUN  cd $JANUS_HOME && \
     ./autogen.sh && \
     ./configure --prefix=/usr/local --disable-websockets --disable-data-channels --enable-post-processing && \
     make && \
     make install && \
     make configs 

RUN sed -i 's/***REMOVED***/stun_server = stun.l.google.com/g' \
     /usr/local/etc/janus/janus.cfg
RUN sed -i 's/***REMOVED***/stun_port = 19302/g' /usr/local/etc/janus/janus.cfg
RUN sed -i 's/;debug_timestamps = yes/debug_timestamps = yes/g' /usr/local/etc/janus/janus.cfg
RUN sed -i 's/***REMOVED***/broadcast = yes/g' /usr/local/etc/janus/janus.cfg
RUN sed -i 's/enabled = no/enabled = yes/g' /usr/local/etc/janus/janus.eventhandler.sampleevh.cfg
RUN sed -i 's/events = all/events = plugins/g' /usr/local/etc/janus/janus.eventhandler.sampleevh.cfg
RUN ln  -s /recordings /usr/local/ether/janus-gateway/html/recordings

#RUN sed -i 's/https = no/https = yes/g' /usr/local/etc/janus/janus.transport.http.cfg
#RUN sed -i 's/;secure_port = 8089/secure_port = 8089/g' /usr/local/etc/janus/janus.transport.http.cfg


## Run Nginx and Janus
RUN chmod +x $JANUS_HOME/scripts/run_janus.sh
CMD ["sh", "-c", "$JANUS_HOME/scripts/run_janus.sh"]

EXPOSE 80 443

