//go:build windows

package main

import (
	"strings"
	"syscall"
	"unsafe"
)

// Winsock's codes (WSAE...), which Go's syscall leaves as plain Errnos.
var errnoCodes = map[syscall.Errno]byte{
	10061: errRefused, 10051: errNetUnreach, 10065: errHostUnreach, 10060: errTimedOut,
	10054: errReset, 10053: errReset, 10013: errDenied,
}

// IP_ADDR_STRING and the start of FIXED_INFO (iphlpapi.h).
type ipAddrString struct {
	next      *ipAddrString
	ipAddress [16]byte
	ipMask    [16]byte
	context   uint32
}

type fixedInfo struct {
	hostName         [132]byte
	domainName       [132]byte
	currentDNSServer *ipAddrString
	dnsServerList    ipAddrString
	nodeType         uint32
	scopeID          [260]byte
	enableRouting    uint32
	enableProxy      uint32
	enableDNS        uint32
}

// nameserver: the first of GetNetworkParams' DNS servers.
func nameserver() string {
	proc := syscall.NewLazyDLL("iphlpapi.dll").NewProc("GetNetworkParams")
	size := uint32(unsafe.Sizeof(fixedInfo{}))
	for try := 0; try < 2; try++ {
		buf := make([]byte, size)
		r, _, _ := proc.Call(uintptr(unsafe.Pointer(&buf[0])), uintptr(unsafe.Pointer(&size)))
		if r == 111 { // ERROR_BUFFER_OVERFLOW: size says how much
			continue
		}
		if r != 0 {
			break
		}
		fi := (*fixedInfo)(unsafe.Pointer(&buf[0]))
		for a := &fi.dnsServerList; a != nil; a = a.next {
			ip := string(a.ipAddress[:])
			if i := strings.IndexByte(ip, 0); i >= 0 {
				ip = ip[:i]
			}
			if ip != "" && !strings.Contains(ip, ":") {
				return ip
			}
		}
		break
	}
	return "1.1.1.1"
}
