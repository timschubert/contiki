Big Red Button Demo
===================


What You Need
-------------

- one pre-built Big Red Button device (Fox inside)
- one extra Fox device (to act as 'border router')
- one Fox ftdi-dongle + mini-usb
- one PC with linux and sudo access
- firmwares or build env

Setup the demo
--------------

- open-up the Big Red Button, access the Fox in it
- plug the ftdi-dongle on top of the extension board
- flash the red-button firmware
- start mini{com|term} and connect to the red-button cli
- type 'p' to print button state
- hit on the big red button
- type 'p' again and check state changed
- type 'l' to get the device ip address
- make a note of the button-ipv6, leaving out the fe80::
- exit minicom
- unplug the ftdi-dongle, place Fox back in Big Red Button
- plug the ftdi-dongle onto the border-router Fox
- flash the border-router firmware


Run the demo
------------

- start tunslip6 on relevant port for the border-router to use
  ``$ sudo ./tunslip6 babe::1/64 -s /dev/ttyUSB3 &> /dev/null &``
- configure a running network interface with a public ipv6 address
  ``$ sudo ifconfig wlan0 add baba::b00b:beef/128``
- configure the Big Red Button destination address to that of the host
  ``$ curl -gs 'http://[babe::<button-ipv6>]/set_destination?baba::b00b:beef'``
- start a basic notification server on port 8000
  ``$ while true; do nc -6 -l 8000 | grep button_state; done``
- now, hit the big red button !
- check the output of notification server to see the message


Firmware Internals
------------------

- border-router: a basic BR setup, slip-bridge only
- red-button: gpio-based button driver, http server, http client, cli

Notes:
- set button-ipv6 to some hardcoded value to ease demo setup
- implement configuration of destination port
- if using spare iotlab-m3 node as BR, use ``tunslip6 -B 500000``


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

