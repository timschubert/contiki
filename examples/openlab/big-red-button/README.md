Big Red Button Demo
===================


What You Need
-------------

- one pre-built Big Red Button device
- one agile-fox device ('border router')
- one ftdi-dongle + mini-usb
- one PC with serial link
- firmware or build env

Setup the demo
--------------

- open-up the big red button and extract the Fox in it
- plug the ftdi-dongle on top of the pile of boards
- flash the red-button.openlab-fox firmware
- start minicom and connect to the red-button cli
- type 'p' to print button state
- hit on the big red button
- type 'p' again and check state changed
- type 'l' to get the device ip address
- make a note of the red-button ipv6 address
- exit minicom
- plug the ftdi-dongle onto the border-router Fox
- flash the border-router firmware
- start tunslip6 (babe::) on relevant port for the BR to use
  ``$ sudo ./tunslip6 babe::1/64 -L -s /dev/ttyUSB3``
- configure your PC's network interface with a public (baba::) ipv6 address
  ``$ sudo ifconfig wlan0 add baba::224:d7ff:fe56:6c64/128``
- start a basic server on port 80 your pc
  ``$ sudo nc -6 -l 80``
- configure the big-red-button destination address to that of your PC
  ``$ curl -v 'http://babe01/set_destination?baba:0:0:0:224:d7ff:fe56:6c64'``
- hit the big red button !
- check the output of nc on your PC and see the message


Firmware Internals
------------------

- border-router: a basic BR setup, slip-bridge only
- red-button: gpio-based button driver, http server, http client, cli


Building the Big Red Button device
----------------------------------

You need:
- one agile-fox
- one fox daughter board
- one big red button
- one resistor
- wires, solder

Recipe:
- wire the big red button to the daughter board
- plug the daughter board onto the Fox

