//go:build !windows

package main

import (
	"bufio"
	"os"
	"strings"
	"syscall"
)

var errnoCodes = map[syscall.Errno]byte{
	syscall.ECONNREFUSED: errRefused, syscall.ENETUNREACH: errNetUnreach, syscall.EHOSTUNREACH: errHostUnreach,
	syscall.ETIMEDOUT: errTimedOut, syscall.ECONNRESET: errReset, syscall.EPIPE: errReset,
	syscall.ECONNABORTED: errReset, syscall.EACCES: errDenied, syscall.EPERM: errDenied,
}

// nameserver: the first IPv4 name server in /etc/resolv.conf (else
// systemd-resolved's).
func nameserver() string {
	f, err := os.Open("/etc/resolv.conf")
	if err == nil {
		defer f.Close()
		sc := bufio.NewScanner(f)
		for sc.Scan() {
			w := strings.Fields(sc.Text())
			if len(w) >= 2 && w[0] == "nameserver" && !strings.Contains(w[1], ":") {
				return w[1]
			}
		}
	}
	return "127.0.0.53"
}
