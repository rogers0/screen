# -*- sh -*-

TESTNAME=`basename $0`.`mktemp -u XXXXXXXX`
SCREEN="screen -S $TESTNAME"
SCREENRC=/dev/null
export SCREENRC
count=0

check_exit_code_true() {
    if [ "$?" != 0 ]; then echo -n 'not '; fi; echo ok $(( count+=1 )) - "$@"
}

check_exit_code_false() {
    if [ "$?" = 0 ]; then echo -n 'not '; fi; echo ok $(( count+=1 )) - "$@"
}

has_session_with_testname() {
    screen -ls | fgrep -q "$TESTNAME"
}

session_exists_2_tests() {
    screen -ls | fgrep -q '(Detached)'
    check_exit_code_true Detached session found

    has_session_with_testname
    check_exit_code_true Session has expected session name
}

create_session_3_tests() {
    $SCREEN -d -m "$@"
    check_exit_code_true Create session

    sleep 1

    session_exists_2_tests
}

kill_session_2_tests() {
    $SCREEN -X quit
    check_exit_code_true Quit command sent to session

    has_session_with_testname
    check_exit_code_false Session is gone
}

has_window_number_zero() {
    $SCREEN -Q windows | egrep -q '^0 '
    check_exit_code_true Session has a window with id 0
}
