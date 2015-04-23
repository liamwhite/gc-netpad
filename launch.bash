#!/bin/bash

# user config vars
export WIILOAD=tcp:wii
export DEVKITPRO=/opt/devkitpro/
export DEVKITPPC=/opt/devkitpro/devkitPPC/

echo $#

if [ $# -eq 1 ]; then
  # rebuild
  (cd server; make)
  (cd client; rm client; make)
fi

# launch the server
(cd server; make run)

# wait for server to ifconfig
sleep 8

# launch client
(cd client; sudo ./client wii)
