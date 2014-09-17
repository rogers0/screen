#!/bin/sh

echo 1..6

. `dirname $0`/boilerplate.sh

create_session_3_tests -U
has_window_number_zero
kill_session_2_tests
