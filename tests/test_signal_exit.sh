testcase "Signal-killed status is propagated"

# See: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_08_02
ftrap sh -c 'kill -INT $$'
assert $? -gt 128
