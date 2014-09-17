#!/bin/sh

echo 1..4

TESTNAME=`basename $0`.`mktemp -u XXXXXXXX`

screen -D -m -S "$TESTNAME" -s /bin/true sleep 2 &
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 1 - Create session

sleep 1

screen -ls | fgrep -q '(Detached)'
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 2 - Detached session found

screen -ls | fgrep -q "$TESTNAME"
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 3 - Session has expected session name

sleep 2

screen -ls | fgrep -q "$TESTNAME"
if [ "$?" = 0 ]; then echo -n 'not '; fi; echo ok 4 - Session is gone
