# vtserver
Warren Toomey's VTserver, updated

VTserver provides an emulated tape drive to a PDP-11 over a serial
port (RS-232) link.  A small blob of PDP-11 object code is transmitted
to ODT over the serial link, which can then be used to boot whatever
PDP-11 software is on the emulated tape stored on the unix host.

This version started with Jonathan Engdahl's copy, which had been
modified to also support a Windows host, and to (theoretically)
support files larger than 32 MB.

My initial version stripped out the switching mechanism which was
intended to allow vtserver to use short block numbers for smaller files
and long block numbers with larger files, which I was never able to
get working.  I also modified the ODT boot code, which didn't work
with my 11/93.

Subsequent updates have resolved build issues with modern compilers

This git repo is intended to preserve some sense of revision history,
though the dates of the changes are at best approximations.
