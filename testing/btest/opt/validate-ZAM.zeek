# @TEST-DOC: ZAM maintenance script for validating synthesized operations.
# @TEST-REQUIRES: test "${ZEEK_ZAM}" == "1"
#
# @TEST-EXEC: zeek -b -O validate-ZAM %INPUT >output
# @TEST-EXEC: btest-diff output
