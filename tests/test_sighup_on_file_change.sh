testcase "SIGHUP is sent on file change"

# Single file
echo "" > testfile_1.conf
(
    sleep 1
    echo "change" >> testfile_1.conf
) &
ftrap -f testfile_1.conf sh -c 'trap "exit 10" HUP; sleep 2'
assert $? -eq 10

# Multiple files
echo "" > testfile_1.conf
echo "" > testfile_2.conf
(
    sleep 1
    echo "change" >> testfile_2.conf
) &
ftrap -f testfile_1.conf -f testfile_2.conf sh -c 'trap "exit 10" HUP; sleep 2'
assert $? -eq 10

rm testfile_1.conf testfile_2.conf
