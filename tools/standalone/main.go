// executor-standalone: Executor's tab and what it needs from a server,
// as one program (docs/executor/Standalone.md).
//
//	executor-standalone [--data DIR] [--images DIR] [--bind ADDR] [--port N]
//	                    [--site DIR] [--password-file FILE] [--tls-cert F --tls-key F] [--quiet]
//	executor-standalone [--data DIR] passwd     the password from stdin ("" : none)
//
// The tab (make_site.sh's output) is embedded; --site serves a directory
// instead. DATA (default: the user's config directory's executor-web)
// has settings.json, the overlays of disks not in the library, Executor's
// volumes and their objects, and the tab's logs.
package main

import (
	"bufio"
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
	"path/filepath"
	"strconv"
	"strings"
	"sync"
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
	images := flag.String("images", "", "the library's images (default: settings', DATA/images)")
	bind := flag.String("bind", "", "the address to listen on (default: settings', 127.0.0.1)")
	port := flag.Int("port", -1, "the port (0: any; default: settings', 8090)")
	site := flag.String("site", "", "serve the tab from this directory, not the embedded one")
	pwFile := flag.String("password-file", "", "the password for this run, from a file")
	tlsCert := flag.String("tls-cert", "", "PEM certificate (chain): serve HTTPS")
	tlsKey := flag.String("tls-key", "", "its key (default: in the certificate's file)")
	quiet := flag.Bool("quiet", false, "errors only")
	flag.Parse()

	if err := os.MkdirAll(*data, 0o755); err != nil {
		fatal(err)
	}
	abs, _ := filepath.Abs(*data)
	s := &Server{data: abs, settingsPath: filepath.Join(abs, "settings.json"), quiet: *quiet,
		failures: map[string][]time.Time{}}
	file, err := loadSettings(s.settingsPath, abs)
	if err != nil {
		fatal(err)
	}

	if flag.Arg(0) == "passwd" {
		fmt.Fprint(os.Stderr, "Password (empty: none): ")
		line, _ := bufio.NewReader(os.Stdin).ReadString('\n')
		pw := strings.TrimRight(line, "\r\n")
		file.PasswordHash = ""
		if pw != "" {
			file.PasswordHash = hashPassword(pw)
		}
		if err := saveSettings(s.settingsPath, file); err != nil {
			fatal(err)
		}
		return
	} else if flag.NArg() > 0 {
		fatal(fmt.Errorf("unknown command %q", flag.Arg(0)))
	}

	if *pwFile != "" {
		b, err := os.ReadFile(*pwFile)
		if err != nil {
			fatal(err)
		}
		s.pwOverride = hashPassword(strings.TrimRight(string(b), "\r\n"))
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
	}
	if *site != "" {
		s.site, s.siteName = os.DirFS(*site), *site
	} else {
		s.site, _ = fs.Sub(embedded, "site")
		s.siteName = "(embedded)"
	}
	s.ids = newIds(filepath.Join(abs, "image-ids.json"))
	s.loadSessions()
	s.applySettings(file)
	if st := s.settings(); st.Images != "" {
		os.MkdirAll(st.Images, 0o755)
	}
	s.overlays = newOverlayStore(abs, s.imageFor)
	s.volumes = newVolumeStore(abs)
	s.relay = &Relay{s: s}

	st := s.settings()
	s.running = st
	ln, err := net.Listen("tcp", net.JoinHostPort(st.Bind, strconv.Itoa(st.Port)))
	if err != nil {
		fatal(err)
	}
	ln = &limitListener{Listener: ln, slots: make(chan struct{}, maxConnections)}
	scheme := "http"
	if st.TLSCert != "" {
		key := st.TLSKey
		if key == "" {
			key = st.TLSCert
		}
		cert, err := tls.LoadX509KeyPair(expand(st.TLSCert), expand(key))
		if err != nil {
			fatal(err)
		}
		ln = tls.NewListener(ln, &tls.Config{Certificates: []tls.Certificate{cert}, NextProtos: []string{"http/1.1"}})
		scheme = "https"
	}
	addr := ln.Addr().(*net.TCPAddr)
	s.mu.Lock()
	s.running.Port = addr.Port
	s.mu.Unlock()
	fmt.Printf("Serving %s on %s://%s/\n", s.siteName, scheme, net.JoinHostPort(addr.IP.String(), strconv.Itoa(addr.Port)))
	os.Stdout.Sync()
	srv := &http.Server{Handler: s, ReadHeaderTimeout: idle, IdleTimeout: idle,
		ErrorLog: nil, MaxHeaderBytes: 64 << 10}
	if *quiet {
		srv.ErrorLog = quietLog()
	}
	if err := srv.Serve(ln); err != nil && !errors.Is(err, http.ErrServerClosed) {
		fatal(err)
	}
}

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
