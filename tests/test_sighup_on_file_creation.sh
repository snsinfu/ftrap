testcase "SIGHUP is sent after file is created"

# Single file
rm -f testfile_1.conf
(
    sleep 1
    touch testfile_1.conf
) &
ftrap -f testfile_1.conf sh -c 'trap "exit 10" HUP; sleep 5'
assert $? -eq 10

# One of multiple files
touch testfile_1.conf
rm -f testfile_2.conf
(
    sleep 1
    touch testfile_2.conf
) &
ftrap -f testfile_1.conf -f testfile_2.conf sh -c 'trap "exit 10" HUP; sleep 5'
assert $? -eq 10

rm -f testfile_1.conf testfile_2.conf
