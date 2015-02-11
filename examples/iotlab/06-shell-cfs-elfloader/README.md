CoffeFileSystem shell example with elfloader
============================================

Usage
-----

    ./client
    Usage: ./client <hostname> <port>


    ./client m3-1 20000
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


Loading program
---------------

To dynamicly load a program, a loader capable firmware should be compiled.
Then, a contiki executable (`.ce`) firmware should be upload to ROM and then
loaded.

### Compiling the loader ###

It should be done three times to generate the symbols table correctly

    make console_server.iotlab-m3 TARGET=iotlab-m3
    make console_server.iotlab-m3 TARGET=iotlab-m3 CORE=console_server.iotlab-m3
    make console_server.iotlab-m3 TARGET=iotlab-m3 CORE=console_server.iotlab-m3

https://github.com/contiki-os/contiki/wiki/The-dynamic-loader#Preparing_a_Firmware_for_ELF_Loading


### Compiling loaded process ###

Generate a 'Contiki Executable' firmware

    iotlab/00-hello-world $ make TARGET=iotlab-m3 hello-world.ce


Program 00-hello-world has been tested and is working.
05-blinking-leds is known for not working for the moment.


### Load the progmam ###

    ./client m3-1 20000

    # flash the firmware

    Platform starting in 1...
    GO!
    [in clock_init() DEBUG] Starting systick timer at 100Hz
    Starting 'Console_server'
    Console server started !
    > ls
    > upload hello-world.ce
    Uploading file hello-world.ce
    File uploaded (1356 bytes)
    > ls
    File: hello-world.ce (1356 bytes)
    > loadelf hello-world.ce
    Result of loading 0
    Remove dirty firmware 'hello-world.ce'
    exec: starting process 'Serial Echo'
    Hello World !
    > exit

Now exit `client` a run a simple `netcat` to be allowed to pass all commands:

    nc m3-1 20000
    test getting echo from hello world
    Hello World !
    Echo cmd: 'test getting echo from hello world'
    > test getting echo from hello world (0 bytes received)

You can see in the output that all processes are answering to the serial line
data. Both `console_server` and `hello_world`.


### Known Issues ###

For some firmware, required firmwares are not linked in the application, like
05-blinking-leds. A fix for this must be investigated.

> Warning: once a file has been elfloaded, it should be re-uploaded to be
> re-executed (source `core/loader/elfloader.h`)
> That's why we are deleting it after load.



Warning
-------

Formatting the flash takes 3 minutes, nothing can be done in this time

