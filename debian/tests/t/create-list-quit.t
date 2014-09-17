#!/bin/sh

echo 1..6

TESTNAME=`basename $0`.`mktemp -u XXXXXXXX`

screen -d -m -S "$TESTNAME"
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 1 - Create session

sleep 1

screen -ls | fgrep -q '(Detached)'
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 2 - Detached session found

screen -ls | fgrep -q "$TESTNAME"
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 3 - Session has expected session name

screen -S "$TESTNAME" -Q windows | egrep -q '^0 '
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 4 - Session has a window with id 0

screen -S "$TESTNAME" -X quit
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 5 - Quit command sent to session

screen -ls | fgrep -q "$TESTNAME"
if [ "$?" = 0 ]; then echo -n 'not '; fi; echo ok 6 - Session is gone
