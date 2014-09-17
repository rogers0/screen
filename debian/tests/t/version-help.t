#!/bin/sh

echo 1..2

. `dirname $0`/boilerplate.sh

screen -v | fgrep -q 'Screen version '
check_exit_code_true Outputs version

screen -h | fgrep -q 'Options:'
check_exit_code_true Outputs help
