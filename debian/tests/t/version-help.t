#!/bin/sh

echo 1..2

screen -v | fgrep -q 'Screen version '
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 1 - Outputs version

screen -h | fgrep -q 'Options:'
if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok 2 - Outputs help
