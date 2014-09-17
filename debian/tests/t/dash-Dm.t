#!/bin/sh

echo 1..5

. `dirname $0`/boilerplate.sh

$SCREEN -D -m &
check_exit_code_true Create session

sleep 1

session_exists_2_tests

$SCREEN -X quit
check_exit_code_true Quit command sent to session

sleep 1;

has_session_with_testname
check_exit_code_false Session is gone
