testcase "Fails if command is not specified"

ftrap 2> /dev/null
assert $? -eq 112

ftrap -f /etc/hosts 2> /dev/null
assert $? -eq 112
