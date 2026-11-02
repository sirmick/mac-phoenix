// executor-standalone: Executor's tab and what it needs from a server,
// as one program (docs/executor/Standalone.md).
//
//	executor-standalone [--data DIR] [--config FILE] [--images DIR] [--bind ADDR] [--port N]
//	                    [--site DIR] [--tls auto | --tls-cert F [--tls-key F]]
//	                    [--open] [--quiet]
//
// It prints its address; the first browser to open it has the server
// until it stops (claim.go); --open opens it.
// The tab (make_site.sh's output) is embedded; --site serves a directory
// instead. DATA (default: the user's config directory's executor-web:
// ~/.config on Linux, ~/Library/Application Support on macOS, %AppData% on
// Windows) has settings.json (or --config's file; settings.go), the
// overlays of disks not in the library, Executor's volumes and their
// objects, the tab's logs (and --tls auto's certificate).
package main

import (
	"context"
	"crypto/tls"
	"embed"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

//go:embed all:site
var embedded embed.FS

func main() {
	defData := "executor-web"
	if d, err := os.UserConfigDir(); err == nil {
		defData = filepath.Join(d, "executor-web")
	}
	data := flag.String("data", defData, "the data directory")
	config := flag.String("config", "", "the settings file (default: DATA/settings.json)")
	images := flag.String("images", "", "the library's images (default: settings', DATA/images)")
	bind := flag.String("bind", "", "the address to listen on (default: settings', 127.0.0.1)")
	port := flag.Int("port", -1, "the port (0: any; default: settings', 8090)")
	site := flag.String("site", "", "serve the tab from this directory, not the embedded one")
	tlsMode := flag.String("tls", "", "auto: serve HTTPS with a self-signed certificate made once in DATA/tls")
	tlsCert := flag.String("tls-cert", "", "PEM certificate (chain): serve HTTPS")
	tlsKey := flag.String("tls-key", "", "its key (default: in the certificate's file)")
	quiet := flag.Bool("quiet", false, "errors only")
	open := flag.Bool("open", false, "open the default browser at the server's address")
	flag.Parse()

	if err := os.MkdirAll(*data, 0o755); err != nil {
		fatal(err)
	}
	abs, _ := filepath.Abs(*data)
	settingsPath := filepath.Join(abs, "settings.json")
	if *config != "" {
		settingsPath, _ = filepath.Abs(*config)
	}
	s := &Server{data: abs, settingsPath: settingsPath, quiet: *quiet}
	if flag.NArg() > 0 {
		fatal(fmt.Errorf("unknown command %q", flag.Arg(0)))
	}
	if *tlsMode != "" && *tlsMode != "auto" && *tlsMode != "off" {
		fatal(fmt.Errorf("--tls %s: auto or off", *tlsMode))
	}
	doc, err := loadSettings(s.settingsPath)
	if err != nil {
		fatal(err)
	}
	for _, f := range []string{"sessions.json", "pair.key", "paired.json"} { // the password's, the pairing's
		os.Remove(filepath.Join(abs, f))
	}
	s.override = func(st *Settings) {
		if *images != "" {
			st.Images, _ = filepath.Abs(*images)
		}
		if *bind != "" {
			st.Bind = *bind
		}
		if *port >= 0 {
			st.Port = *port
		}
		if *tlsCert != "" {
			st.TLSCert, st.TLSKey = *tlsCert, *tlsKey
		}
		switch *tlsMode {
		case "auto":
			st.TLSCert, st.TLSKey = "auto", ""
		case "off":
			st.TLSCert, st.TLSKey = "", ""
		}
	}
	if *site != "" {
		s.site, s.siteName = os.DirFS(*site), *site
	} else {
		s.site, _ = fs.Sub(embedded, "site")
		s.siteName = "(embedded)"
	}
	s.ids = newIds(filepath.Join(abs, "image-ids.json"))
	s.applySettings(doc)
	if st := s.settings(); st.Images != "" {
		os.MkdirAll(st.Images, 0o755)
	}
	s.overlays = newOverlayStore(abs, s.imageFor)
	s.volumes = newVolumeStore(abs)
	s.relay = &Relay{s: s}

	st := s.settings()
	ln, err := net.Listen("tcp", net.JoinHostPort(st.Bind, strconv.Itoa(st.Port)))
	if err != nil {
		fatal(err)
	}
	ln = &limitListener{Listener: ln, slots: make(chan struct{}, maxConnections)}
	scheme, fingerprint := "http", ""
	if st.TLSCert != "" {
		var cert tls.Certificate
		if st.TLSCert == "auto" {
			cert, fingerprint, err = autoCert(abs)
		} else {
			key := st.TLSKey
			if key == "" {
				key = st.TLSCert
			}
			cert, err = tls.LoadX509KeyPair(expand(st.TLSCert), expand(key))
		}
		if err != nil {
			fatal(err)
		}
		ln = tls.NewListener(ln, &tls.Config{Certificates: []tls.Certificate{cert}, NextProtos: []string{"http/1.1"}})
		scheme, s.tls = "https", true
	}
	addr := ln.Addr().(*net.TCPAddr)
	fmt.Printf("Serving %s on %s://%s/\n", s.siteName, scheme, net.JoinHostPort(addr.IP.String(), strconv.Itoa(addr.Port)))
	fmt.Printf("Open %s (the first browser to open it has it until the server stops)\n", browserURL(scheme, addr))
	for _, u := range lanURLs(scheme, addr) {
		fmt.Printf("  or on the LAN: %s\n", u)
	}
	if fingerprint != "" {
		fmt.Printf("The certificate (self-signed: the browser asks once), SHA-256 %s\n", fingerprint)
	} else if !addr.IP.IsLoopback() {
		fmt.Println("Listening beyond this computer over plain HTTP: --tls auto serves HTTPS.")
	}
	os.Stdout.Sync()
	if *open {
		if err := openBrowser(browserURL(scheme, addr)); err != nil {
			fmt.Fprintln(os.Stderr, "executor-standalone: --open:", err)
		}
	}
	srv := &http.Server{Handler: s, ReadHeaderTimeout: idle, IdleTimeout: idle,
		ErrorLog: nil, MaxHeaderBytes: 64 << 10}
	if *quiet {
		srv.ErrorLog = quietLog()
	}
	// SIGTERM/SIGINT (systemd's stop, a restart, ^C): no new connections,
	// and what is being answered (a disk's download) has shutdownGrace to
	// finish; a second signal ends it now.
	stop := make(chan os.Signal, 2)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	served := make(chan error, 1)
	go func() { served <- srv.Serve(ln) }()
	select {
	case err := <-served:
		if !errors.Is(err, http.ErrServerClosed) {
			fatal(err)
		}
	case <-stop:
		if !*quiet {
			fmt.Println("Stopping: letting the answers under way finish")
		}
		ctx, cancel := context.WithTimeout(context.Background(), shutdownGrace)
		go func() {
			<-stop
			cancel()
		}()
		if err := srv.Shutdown(ctx); err != nil && !*quiet {
			fmt.Fprintln(os.Stderr, "executor-standalone: stopped with answers unfinished:", err)
		}
	}
}

const shutdownGrace = 60 * time.Second

func expand(p string) string {
	if rest, ok := strings.CutPrefix(p, "~/"); ok {
		if h, err := os.UserHomeDir(); err == nil {
			return filepath.Join(h, rest)
		}
	}
	return p
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, "executor-standalone:", err)
	os.Exit(1)
}

// limitListener: maxConnections at once; the rest are closed.
type limitListener struct {
	net.Listener
	slots chan struct{}
}

type limitConn struct {
	net.Conn
	once    sync.Once
	release func()
}

func (c *limitConn) Close() error {
	c.once.Do(c.release)
	return c.Conn.Close()
}

func (l *limitListener) Accept() (net.Conn, error) {
	for {
		c, err := l.Listener.Accept()
		if err != nil {
			return nil, err
		}
		select {
		case l.slots <- struct{}{}:
			return &limitConn{Conn: c, release: func() { <-l.slots }}, nil
		default:
			c.Close()
		}
	}
}

func quietLog() *log.Logger { return log.New(io.Discard, "", 0) }
