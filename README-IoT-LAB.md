Welcome to Contiki for IoT-LAB !
================================

This repository is a fork of the official Contiki repository, bringing support for the IoT-LAB platforms.

You may retrieve the last changes from the official repository with these commands:

    git remote add contiki https://github.com/contiki-os/contiki
    git fetch contiki
    git merge contiki/master

Supported platforms:
- iotlab-m3
- iotlab-a8-m3

Requirements:
- gcc-toolchain: https://launchpad.net/gcc-arm-embedded
- openlab (already checked-out if you used iot-lab)

See this [tutorial](https://www.iot-lab.info/tutorials/contiki-compilation/) for explanations on how to setup your environment.

Basic setup:
- ``$ make TARGET=iotlab-m3     savetarget ``  # for m3 nodes
- ``$ make TARGET=iotlab-a8-m3  savetarget ``  # for a8 nodes

Further doc:
- README-BUILDING.md
- README-EXAMPLES.md
