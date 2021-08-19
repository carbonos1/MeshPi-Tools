#!/bin/sh
docker run --net=host --env="DISPLAY" --rm -it -v "$(pwd):/root/models" -u "$(id -u):$(id -g)" omnetpp/omnetpp-gui:u18.04-5.6.2
