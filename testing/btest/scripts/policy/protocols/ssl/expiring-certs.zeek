# @TEST-EXEC: zeek -b -r $TRACES/tls/tls-expired-cert.trace %INPUT
# @TEST-EXEC: btest-diff notice.log

@load protocols/ssl/expiring-certs

redef SSL::notify_certs_expiration = ALL_HOSTS;

