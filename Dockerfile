## TODO
#  Mount a volume and dump all the logs into that otherwise logs will be lost as soon as container exits
#  Configure docker to pick certificates from mount volume
#  Evaluate option of creating Nginx as seperate image
#  Check if size of docker image can be reduced in production image, only binaries of janus ? no code ?? 

FROM ubuntu:16.04

## Install build essential
RUN  apt-get update
RUN  apt-get install -y make vim wget git

## Install dependencies
COPY MakefileDeployment .
RUN  make -f MakefileDeployment install-dep

## Keep the commands which don't change above this, will save time in container build
## Accessing the variables at later stage will avoid redoing constant steps defined above
ARG  CERT_PATH
ARG  RECORDING_PATH
ARG  ETHERMEET_HOME
ARG  ACTIVE_ENV
ENV  JANUS_HOME $ETHERMEET_HOME/janus-gateway

## ADD Janus Code, shouldn't it be clone from gitlab ?
RUN   mkdir -p $ETHERMEET_HOME
RUN   mkdir -p $JANUS_HOME
ADD . $JANUS_HOME/

## Build and Install Janus
RUN  make -f MakefileDeployment install-ether ETHERMEET_HOME=$ETHERMEET_HOME JANUS_HOME=$JANUS_HOME 

## Configure Janus and dependencies
RUN  make -f MakefileDeployment config ACTIVE_ENV=$ACTIVE_ENV JANUS_HOME=$JANUS_HOME CERT_PATH=$CERT_PATH RECORDING_PATH=$RECORDING_PATH 

CMD ["sh", "-c", "$JANUS_HOME/scripts/run_janus.sh"]

EXPOSE 80 443
EXPOSE 0:65535/udp



