# Tests processing of half-duplex client-side connections, including no
# analyzer.log output.

# @TEST-EXEC: zeek -r $TRACES/ssh/ssh.client-side-half-duplex.pcap %INPUT
# @TEST-EXEC: btest-diff analyzer.log
# @TEST-EXEC: btest-diff ssh.log
# @TEST-EXEC: btest-diff conn.log
# @TEST-EXEC: btest-diff weird.log
