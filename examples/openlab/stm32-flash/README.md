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


On-chip Flash APIs:
-------------------

ST provides two levels of API for accessing the STM32 on-chip flash:

* nvm.h: exposes one page of flash via readFrom/writeTo
* flash.h: low-level write access to flash pages via erase/write

The high-level API takes care of low-level flash accesses and alternates
writes to two separate pages to reduce wear-out.  This API is used for
radio calibration, which requires and reserves the lower 64 bytes of nvm. 

The low-level API offers per page erase and write primitives, along with
specific customer-information-block (cib) access.  Flash memory is mapped
at addresses ``0x08000000 - 0x0807FFFF`` and aliassed at ``0x00000 - 7FFFF``.
