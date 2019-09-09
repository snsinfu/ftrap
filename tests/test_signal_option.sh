testcase "Selected signal is sent on file change"

for sig in HUP USR1 USR2 TERM QUIT INT; do
    echo "" > testfile.conf
    (
        sleep 1
        echo "change" >> testfile.conf
    ) &
    ftrap -f testfile.conf -s ${sig} sh -c "trap 'exit 10' ${sig}; sleep 2"
    assert $? -eq 10
done

rm -f testfile.conf
