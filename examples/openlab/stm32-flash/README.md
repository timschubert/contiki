STM32-FLASH
===========

Shows basic STM32 memory mapping: .data and .bss segments in RAM,
firmware flash memory sections at the bottom of the addressing space.
 
To come:
--------

Print the first bytes in the STM32 internal flash (firmware),
and writes (increments) the last byte in said flash.

This in effects uses the firmware flash as pseudo-permanent storage.

Hopefully,
OpenOCD erases the required number of pages starting from the bottom
of the memory when downloading new firmware, so the last byte is safe
as long as the last page (2K) is not used by the firmware.
