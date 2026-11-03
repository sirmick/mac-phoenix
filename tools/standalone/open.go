package main

// --open: the default browser at the server's address once it listens.

import (
	"net"
	"os/exec"
	"runtime"
	"strconv"
)

// browserURL: the address to open; an "any address" bind opens on this machine.
func browserURL(scheme string, addr *net.TCPAddr) string {
	host := addr.IP.String()
	if addr.IP.IsUnspecified() {
		host = "127.0.0.1"
	}
	return scheme + "://" + net.JoinHostPort(host, strconv.Itoa(addr.Port)) + "/"
}

// lanURLs: for an "any address" bind, the address on each of this
// machine's networks (not loopback or link-local).
func lanURLs(scheme string, addr *net.TCPAddr) []string {
	if !addr.IP.IsUnspecified() {
		return nil
	}
	var out []string
	addrs, _ := net.InterfaceAddrs()
	for _, a := range addrs {
		n, ok := a.(*net.IPNet)
		if !ok || n.IP.IsLoopback() || n.IP.IsLinkLocalUnicast() || (addr.IP.To4() != nil && n.IP.To4() == nil) {
			continue
		}
		out = append(out, scheme+"://"+net.JoinHostPort(n.IP.String(), strconv.Itoa(addr.Port))+"/")
	}
	return out
}

func openBrowser(url string) error {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "darwin":
		cmd = exec.Command("open", url)
	case "windows":
		// Not "cmd /c start": cmd.exe would take the URL's & as a command separator.
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	if err := cmd.Start(); err != nil {
		return err
	}
	go cmd.Wait()
	return nil
}
