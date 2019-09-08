testcase "SIGHUP is sent after a deleted file is re-created"

# rm and touch
touch testfile_1.conf
(
    sleep 1
    rm testfile_1.conf
    touch testfile_1.conf
) &
ftrap -f testfile_1.conf sh -c 'trap "exit 10" HUP; sleep 5'
assert $? -eq 10

# mv-clobber
touch testfile_1.conf
(
    sleep 1
    touch testfile_2.conf
    mv testfile_2.conf testfile_1.conf
) &
ftrap -f testfile_1.conf sh -c 'trap "exit 10" HUP; sleep 5'
assert $? -eq 10

rm -f testfile_1.conf testfile_2.conf
