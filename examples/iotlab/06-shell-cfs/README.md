CoffeFileSystem shell example
=============================

Usage from your computer:

    # in one terminal
    ssh -N -L 20000:m3-2:20000 site.iot-lab.info


    # in another terminal
    ./client 
    > 
    > 
    > help
    Command: upload
    Command: ls
    Command: format
    Command: cat
    Command: rm
    Command: loadelf
    Command: exit
    Command: help
    > ls
    > upload hello_world
    Uploading file hello_world
    File uploaded (13 bytes)
    > ls
    File: hello_world (13 bytes)
    > cat hello_world
    Hello World!

    > rm hello_world
    > ls
    >


TODO client may be adapted to be run from the server by parsing a hostname

Warning
-------

Formatting the flash takes 3 minutes, nothing can be done in this time

