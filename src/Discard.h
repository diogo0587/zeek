// See the file "COPYING" in the main distribution directory for copyright.

#pragma once

#include <sys/types.h> // for u_char
#include <memory>

#include "zeek/IntrusivePtr.h"
#include "zeek/local_shared_ptr.h"

namespace zeek
	{

class IP_Hdr;
class Val;
class Func;
using FuncPtr = IntrusivePtr<Func>;

namespace detail
	{

class Discarder final
	{
public:
	Discarder();
	~Discarder() = default;

	bool IsActive();

	bool NextPacket(const zeek::detail::local_shared_ptr<IP_Hdr>& ip, int len, int caplen);

protected:
	Val* BuildData(const u_char* data, int hdrlen, int len, int caplen);

	FuncPtr check_ip;
	FuncPtr check_tcp;
	FuncPtr check_udp;
	FuncPtr check_icmp;

	// Maximum amount of application data passed to filtering functions.
	int discarder_maxlen;
	};

	} // namespace detail
	} // namespace zeek
