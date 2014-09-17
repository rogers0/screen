#!/bin/sh

echo 1..4

. `dirname $0`/boilerplate.sh

$SCREEN -D -m -s /bin/true sleep 2 &
check_exit_code_true Create session

sleep 1

session_exists_2_tests

sleep 2

has_session_with_testname
check_exit_code_false Session is gone
