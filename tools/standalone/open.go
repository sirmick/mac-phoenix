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
