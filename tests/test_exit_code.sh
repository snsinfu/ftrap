testcase "Command exit code is preserved"

ftrap true
assert $? -eq 0

ftrap false
assert $? -eq 1

ftrap sh -c "exit 10"
assert $? -eq 10
