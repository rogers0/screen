#!/bin/sed -f

s,/usr/local/etc/screenrc,/etc/screenrc,g
s,/usr/local/screens,/var/run/screen,g
s,/local/etc/screenrc,/etc/screenrc,g
s,/etc/utmp,/var/run/utmp,g
s,/local/screens/S-,/var/run/screen/S-,g
