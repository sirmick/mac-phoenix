package main

// The relay: /relay, a WebSocket, as tools/web/server/relay.py (the
// protocol: docs/executor/DIRECTION.md, "The network relay"; the tab's
// side: web/executor/net.js). Executor's MacTCP and Open Transport reach
// the network through it: TCP connections out, UDP datagrams to and from
// the hosts the tab sent to, DNS on this host's resolver. One binary
// message is one relay message: op (u8), id (u32), the op's fields;
// big-endian.
//
// Limits (settings' relay): destination ports from an allow-list; no
// private, loopback, link-local, multicast or reserved addresses unless
// allow_private; max_sockets and max_connections, all tabs together;
// window bytes in flight each way per TCP socket.

import (
	"bufio"
	"context"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math/rand/v2"
	"net"
	"net/http"
	"net/netip"
	"strings"
	"sync"
	"syscall"
	"time"
)

const (
	wsGUID      = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
	relayVer    = 1
	window      = 64 * 1024 // unacknowledged bytes each way, per TCP socket
	maxMessage  = 128 * 1024
	maxDatagram = 65507
	maxLookups  = 8
	pingEvery   = 30 * time.Second
)

// Tab -> relay.
const (
	opConnect  = 0x01
	opUDP      = 0x02
	opSend     = 0x03
	opSendTo   = 0x04
	opShutdown = 0x05
	opClose    = 0x06
	opCredit   = 0x07
	opLookup   = 0x10
	opReverse  = 0x11
	opQuery    = 0x12
)

// Relay -> tab.
const (
	opHello     = 0x80
	opConnected = 0x81
	opData      = 0x83
	opDataFrom  = 0x84
	opEOF       = 0x85
	opError     = 0x86
	opAck       = 0x87
	opAddrs     = 0x90
	opName      = 0x91
	opAnswer    = 0x92
)

var replyOp = map[byte]byte{opLookup: opAddrs, opReverse: opName, opQuery: opAnswer}

// ERROR's codes; lookups answer 0 (found) or noName..lookupFailed.
const (
	errRefused = iota + 1
	errNetUnreach
	errHostUnreach
	errTimedOut
	errReset
	errDenied
	errTooMany
	errFailed
)

const (
	noName = iota + 1
	tryAgain
	lookupFailed
)

func errCode(err error, def byte) byte {
	var ne net.Error
	if errors.As(err, &ne) && ne.Timeout() {
		return errTimedOut
	}
	var en syscall.Errno
	if errors.As(err, &en) {
		if c, ok := errnoCodes[en]; ok {
			return c
		}
	}
	return def
}

type relayPolicy struct {
	ports          []portRange
	allowPrivate   bool
	enabled        bool
	maxSockets     int
	maxConnections int
	nameserver     string
}

// Ranges no address of which is global (Python's ipaddress is_global for IPv4).
var notGlobal = func() []netip.Prefix {
	var out []netip.Prefix
	for _, p := range []string{"0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8", "169.254.0.0/16",
		"172.16.0.0/12", "192.0.0.0/24", "192.0.2.0/24", "192.88.99.0/24", "192.168.0.0/16", "198.18.0.0/15",
		"198.51.100.0/24", "203.0.113.0/24", "224.0.0.0/4", "240.0.0.0/4", "255.255.255.255/32"} {
		out = append(out, netip.MustParsePrefix(p))
	}
	return out
}()

func (p *relayPolicy) allowed(addr uint32, port uint16) bool {
	ok := false
	for _, r := range p.ports {
		if r.lo <= int(port) && int(port) <= r.hi {
			ok = true
			break
		}
	}
	if !ok {
		return false
	}
	ip := netip.AddrFrom4([4]byte{byte(addr >> 24), byte(addr >> 16), byte(addr >> 8), byte(addr)})
	if addr == 0 || addr == 0xFFFFFFFF || ip.IsMulticast() {
		return false
	}
	if p.allowPrivate {
		return true
	}
	for _, n := range notGlobal {
		if n.Contains(ip) {
			return false
		}
	}
	return true
}

type Relay struct {
	s     *Server
	mu    sync.Mutex
	conns int
	socks int
}

func (r *Relay) counts() (int, int) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.conns, r.socks
}

func (r *Relay) take(n *int, limit int) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if *n >= limit {
		return false
	}
	*n++
	return true
}

func (r *Relay) give(n *int) {
	r.mu.Lock()
	*n--
	r.mu.Unlock()
}

func (r *Relay) policy() relayPolicy {
	st := r.s.settings().Relay
	ports, _ := parsePorts(st.Ports)
	return relayPolicy{ports, st.AllowPrivate, st.Enabled, st.MaxSockets, st.MaxConnections, st.Nameserver}
}

func closeFrame(code int, reason string) []byte {
	if len(reason) > 123 {
		reason = reason[:123]
	}
	p := binary.BigEndian.AppendUint16(nil, uint16(code))
	p = append(p, reason...)
	return append([]byte{0x88, byte(len(p))}, p...)
}

func wsFrame(payload []byte, opcode byte) []byte {
	n := len(payload)
	var head []byte
	switch {
	case n < 126:
		head = []byte{0x80 | opcode, byte(n)}
	case n < 65536:
		head = binary.BigEndian.AppendUint16([]byte{0x80 | opcode, 126}, uint16(n))
	default:
		head = binary.BigEndian.AppendUint64([]byte{0x80 | opcode, 127}, uint64(n))
	}
	return append(head, payload...)
}

// serve: the handshake, then the connection until it closes.
func (r *Relay) serve(c *ctx) error {
	key := c.r.Header.Get("Sec-WebSocket-Key")
	if !strings.EqualFold(c.r.Header.Get("Upgrade"), "websocket") || key == "" {
		return httpErr(400, "a WebSocket endpoint")
	}
	hj, ok := c.w.(http.Hijacker)
	if !ok {
		return httpErr(500, "no hijacking")
	}
	conn, rw, err := hj.Hijack()
	if err != nil {
		return nil
	}
	defer conn.Close()
	conn.SetDeadline(time.Time{})
	sum := sha1.Sum([]byte(key + wsGUID))
	fmt.Fprintf(rw, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"+
		"Sec-WebSocket-Accept: %s\r\n\r\n", base64.StdEncoding.EncodeToString(sum[:]))
	if err := rw.Flush(); err != nil {
		return nil
	}
	pol := r.policy()
	if !pol.enabled {
		conn.Write(closeFrame(1013, "no relay on this server"))
		return nil
	}
	if !r.take(&r.conns, pol.maxConnections) {
		conn.Write(closeFrame(1013, "too many relay connections"))
		return nil
	}
	defer r.give(&r.conns)
	rc := &relayConn{relay: r, pol: pol, ws: conn, br: rw.Reader, socks: map[uint32]*rsock{}, closed: make(chan struct{})}
	rc.run()
	return nil
}

type wsClosed struct {
	code   int
	reason string
}

func (e *wsClosed) Error() string { return e.reason }

type relayConn struct {
	relay   *Relay
	pol     relayPolicy
	ws      net.Conn
	br      *bufio.Reader
	wmu     sync.Mutex
	mu      sync.Mutex // socks, lookups
	socks   map[uint32]*rsock
	lookups int
	closed  chan struct{}
}

func (rc *relayConn) write(b []byte) error {
	rc.wmu.Lock()
	defer rc.wmu.Unlock()
	rc.ws.SetWriteDeadline(time.Now().Add(3 * pingEvery))
	_, err := rc.ws.Write(b)
	return err
}

func (rc *relayConn) send(op byte, id uint32, body []byte) error {
	p := make([]byte, 5, 5+len(body))
	p[0] = op
	binary.BigEndian.PutUint32(p[1:], id)
	return rc.write(wsFrame(append(p, body...), 0x2))
}

func (rc *relayConn) run() {
	hello := binary.BigEndian.AppendUint16(nil, relayVer)
	hello = binary.BigEndian.AppendUint16(hello, uint16(rc.pol.maxSockets))
	hello = binary.BigEndian.AppendUint32(hello, window)
	rc.send(opHello, 0, hello)
	go func() {
		t := time.NewTicker(pingEvery)
		defer t.Stop()
		for {
			select {
			case <-rc.closed:
				return
			case <-t.C:
				rc.write(wsFrame(nil, 0x9))
			}
		}
	}()
	err := rc.readLoop()
	close(rc.closed)
	rc.mu.Lock()
	socks := make([]*rsock, 0, len(rc.socks))
	for _, s := range rc.socks {
		socks = append(socks, s)
	}
	rc.mu.Unlock()
	for _, s := range socks {
		rc.closeSock(s, false)
	}
	var wc *wsClosed
	if errors.As(err, &wc) {
		rc.write(closeFrame(wc.code, wc.reason))
	}
}

// readLoop: RFC 6455 frames from the tab (masked) -> messages, until the
// connection ends: a *wsClosed to answer with, or the tab's side gone.
func (rc *relayConn) readLoop() error {
	var parts []byte
	var msgOp byte
	inMsg := false
	head := make([]byte, 14)
	for {
		rc.ws.SetReadDeadline(time.Now().Add(3 * pingEvery)) // nothing heard: dead
		if _, err := io.ReadFull(rc.br, head[:2]); err != nil {
			return timeoutClosed(err)
		}
		fin, opcode, masked, n := head[0]&0x80 != 0, head[0]&0x0F, head[1]&0x80 != 0, uint64(head[1]&0x7F)
		switch n {
		case 126:
			if _, err := io.ReadFull(rc.br, head[2:4]); err != nil {
				return timeoutClosed(err)
			}
			n = uint64(binary.BigEndian.Uint16(head[2:4]))
		case 127:
			if _, err := io.ReadFull(rc.br, head[2:10]); err != nil {
				return timeoutClosed(err)
			}
			n = binary.BigEndian.Uint64(head[2:10])
		}
		if !masked {
			return &wsClosed{1002, "frames from a client are masked"}
		}
		if n+uint64(len(parts)) > maxMessage {
			return &wsClosed{1009, "message too big"}
		}
		var mask [4]byte
		if _, err := io.ReadFull(rc.br, mask[:]); err != nil {
			return timeoutClosed(err)
		}
		payload := make([]byte, n)
		if _, err := io.ReadFull(rc.br, payload); err != nil {
			return timeoutClosed(err)
		}
		for i := range payload {
			payload[i] ^= mask[i%4]
		}
		if opcode >= 0x8 {
			if !fin || n > 125 {
				return &wsClosed{1002, "a fragmented or long control frame"}
			}
			switch opcode {
			case 0x8:
				return &wsClosed{1000, ""}
			case 0x9:
				rc.write(wsFrame(payload, 0xA))
			}
			continue
		}
		if opcode == 0 {
			if !inMsg {
				return &wsClosed{1002, "a continuation of nothing"}
			}
		} else if inMsg {
			return &wsClosed{1002, "a message inside a fragmented one"}
		} else {
			msgOp, inMsg = opcode, true
		}
		parts = append(parts, payload...)
		if !fin {
			continue
		}
		msg := parts
		parts, inMsg = nil, false
		switch msgOp {
		case 0x1:
			return &wsClosed{1003, "binary messages only"}
		case 0x2:
			if len(msg) < 5 {
				return &wsClosed{1008, "a short message"}
			}
			if err := rc.op(msg[0], binary.BigEndian.Uint32(msg[1:5]), msg[5:]); err != nil {
				return err
			}
		}
	}
}

func timeoutClosed(err error) error {
	var ne net.Error
	if errors.As(err, &ne) && ne.Timeout() {
		return &wsClosed{1001, "nothing heard"}
	}
	return err // gone
}

// ---- a socket ----
const (
	kindTCP = iota
	kindUDP
	kindDead
)

type rsock struct {
	id         uint32
	mu         sync.Mutex
	cond       *sync.Cond
	kind       int
	tcp        *net.TCPConn
	udp        *net.UDPConn
	cancel     context.CancelFunc
	connecting bool
	out        []byte // SEND bytes not yet written
	unacked    int    // DATA bytes the tab hasn't credited
	shutOut    bool   // SHUTDOWN asked
	peers      map[netip.AddrPort]bool
}

func (rc *relayConn) op(op byte, id uint32, body []byte) error {
	if _, ok := replyOp[op]; ok {
		rc.lookup(op, id, body)
		return nil
	}
	if op == opConnect || op == opUDP {
		return rc.open(op, id, body)
	}
	rc.mu.Lock()
	s := rc.socks[id]
	rc.mu.Unlock()
	if s == nil {
		return &wsClosed{1008, fmt.Sprintf("no socket %d", id)}
	}
	if op == opClose {
		rc.closeSock(s, len(body) > 0 && body[0]&1 != 0)
		return nil
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.kind == kindDead {
		return nil // sent before our ERROR reached the tab
	}
	switch op {
	case opSend:
		if s.kind != kindTCP || s.shutOut {
			return &wsClosed{1008, "SEND on that socket"}
		}
		if len(s.out)+len(body) > window {
			return &wsClosed{1008, "more than the window"}
		}
		s.out = append(s.out, body...)
		s.cond.Broadcast()
	case opSendTo:
		if s.kind != kindUDP || len(body) < 6 {
			return &wsClosed{1008, "SENDTO on that socket"}
		}
		addr, port := binary.BigEndian.Uint32(body), binary.BigEndian.Uint16(body[4:])
		if !rc.pol.allowed(addr, port) {
			return nil // dropped, as by a firewall
		}
		peer := netip.AddrPortFrom(netip.AddrFrom4([4]byte{byte(addr >> 24), byte(addr >> 16), byte(addr >> 8), byte(addr)}), port)
		s.peers[peer] = true
		data := body[6:]
		if len(data) > maxDatagram {
			data = data[:maxDatagram]
		}
		s.udp.WriteToUDPAddrPort(data, peer) // datagrams may be lost
	case opShutdown:
		s.shutOut = true
		s.cond.Broadcast()
	case opCredit:
		if len(body) < 4 {
			return &wsClosed{1008, "a short CREDIT"}
		}
		s.unacked = max(0, s.unacked-int(binary.BigEndian.Uint32(body)))
		s.cond.Broadcast()
	default:
		return &wsClosed{1008, fmt.Sprintf("op %#x", op)}
	}
	return nil
}

func (rc *relayConn) open(op byte, id uint32, body []byte) error {
	rc.mu.Lock()
	_, exists := rc.socks[id]
	rc.mu.Unlock()
	if exists {
		return &wsClosed{1008, fmt.Sprintf("socket %d is open", id)}
	}
	if op == opConnect && len(body) < 6 {
		return &wsClosed{1008, "a short CONNECT"}
	}
	if !rc.relay.take(&rc.relay.socks, rc.pol.maxSockets) {
		rc.send(opError, id, []byte{errTooMany})
		return nil
	}
	s := &rsock{id: id, kind: kindTCP, peers: map[netip.AddrPort]bool{}}
	s.cond = sync.NewCond(&s.mu)
	rc.mu.Lock()
	rc.socks[id] = s
	rc.mu.Unlock()
	if op == opUDP {
		s.kind = kindUDP
		u, err := net.ListenUDP("udp4", &net.UDPAddr{})
		if err != nil {
			s.mu.Lock()
			rc.fail(s, errCode(err, errFailed))
			s.mu.Unlock()
			return nil
		}
		s.udp = u
		go rc.udpReader(s)
		return nil
	}
	addr, port := binary.BigEndian.Uint32(body), binary.BigEndian.Uint16(body[4:])
	s.mu.Lock()
	defer s.mu.Unlock()
	if !rc.pol.allowed(addr, port) {
		rc.fail(s, errDenied)
		return nil
	}
	s.connecting = true
	cx, cancel := context.WithCancel(context.Background())
	s.cancel = cancel
	target := netip.AddrPortFrom(netip.AddrFrom4([4]byte{byte(addr >> 24), byte(addr >> 16), byte(addr >> 8), byte(addr)}), port)
	go rc.connect(s, cx, target.String())
	return nil
}

// fail: the socket is dead; the tab frees its id with CLOSE. Under s.mu.
func (rc *relayConn) fail(s *rsock, code byte) {
	if s.kind == kindDead {
		return
	}
	rc.send(opError, s.id, []byte{code})
	s.drop()
}

// drop closes what the socket has. Under s.mu.
func (s *rsock) drop() {
	if s.kind == kindDead {
		return
	}
	if s.cancel != nil {
		s.cancel()
	}
	if s.tcp != nil {
		s.tcp.Close()
	}
	if s.udp != nil {
		s.udp.Close()
	}
	s.kind = kindDead
	s.cond.Broadcast()
}

func (rc *relayConn) closeSock(s *rsock, abort bool) {
	s.mu.Lock()
	if abort && s.tcp != nil && s.kind == kindTCP {
		s.tcp.SetLinger(0)
	}
	s.drop()
	s.mu.Unlock()
	rc.mu.Lock()
	mine := rc.socks[s.id] == s
	if mine {
		delete(rc.socks, s.id)
	}
	rc.mu.Unlock()
	if mine {
		rc.relay.give(&rc.relay.socks)
	}
}

func (rc *relayConn) connect(s *rsock, cx context.Context, target string) {
	var d net.Dialer
	c, err := d.DialContext(cx, "tcp4", target)
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.kind == kindDead {
		if c != nil {
			c.Close()
		}
		return
	}
	if err != nil {
		rc.fail(s, errCode(err, errFailed))
		return
	}
	s.tcp = c.(*net.TCPConn)
	s.tcp.SetNoDelay(true)
	s.connecting = false
	rc.send(opConnected, s.id, nil)
	go rc.tcpWriter(s)
	go rc.tcpReader(s)
}

// tcpWriter: SEND's bytes to the peer, ACKed as written; then the FIN
// SHUTDOWN asked for.
func (rc *relayConn) tcpWriter(s *rsock) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for {
		for s.kind != kindDead && len(s.out) == 0 && !s.shutOut {
			s.cond.Wait()
		}
		if s.kind == kindDead {
			return
		}
		if len(s.out) == 0 { // shutOut, all written
			s.tcp.CloseWrite()
			return
		}
		buf := append([]byte(nil), s.out...)
		conn := s.tcp
		s.mu.Unlock()
		n, err := conn.Write(buf)
		s.mu.Lock()
		if s.kind == kindDead {
			return
		}
		s.out = s.out[n:]
		if n > 0 {
			rc.send(opAck, s.id, binary.BigEndian.AppendUint32(nil, uint32(n)))
		}
		if err != nil {
			rc.fail(s, errCode(err, errReset))
			return
		}
	}
}

// tcpReader: the peer's bytes to the tab, window at most unacknowledged.
func (rc *relayConn) tcpReader(s *rsock) {
	buf := make([]byte, 16384)
	s.mu.Lock()
	defer s.mu.Unlock()
	for {
		for s.kind != kindDead && s.unacked >= window {
			s.cond.Wait()
		}
		if s.kind == kindDead {
			return
		}
		room := min(len(buf), window-s.unacked)
		conn := s.tcp
		s.mu.Unlock()
		n, err := conn.Read(buf[:room])
		s.mu.Lock()
		if s.kind == kindDead {
			return
		}
		if n > 0 {
			s.unacked += n
			rc.send(opData, s.id, buf[:n])
		}
		if err == io.EOF {
			rc.send(opEOF, s.id, nil)
			return
		}
		if err != nil {
			rc.fail(s, errCode(err, errReset))
			return
		}
	}
}

func (rc *relayConn) udpReader(s *rsock) {
	buf := make([]byte, maxDatagram)
	for {
		n, from, err := s.udp.ReadFromUDPAddrPort(buf)
		s.mu.Lock()
		if s.kind == kindDead {
			s.mu.Unlock()
			return
		}
		from = netip.AddrPortFrom(from.Addr().Unmap(), from.Port())
		if err == nil && s.peers[from] {
			a := from.Addr().As4()
			msg := append(a[:], byte(from.Port()>>8), byte(from.Port()))
			rc.send(opDataFrom, s.id, append(msg, buf[:n]...))
		}
		s.mu.Unlock()
		if err != nil && errors.Is(err, net.ErrClosed) {
			return
		}
		// other errors: an ICMP error for an earlier send
	}
}

// ---- DNS ----
func (rc *relayConn) lookup(op byte, id uint32, body []byte) {
	rc.mu.Lock()
	busy := rc.lookups >= maxLookups
	if !busy {
		rc.lookups++
	}
	rc.mu.Unlock()
	if busy {
		rc.send(replyOp[op], id, []byte{tryAgain})
		return
	}
	body = append([]byte(nil), body...)
	go func() {
		reply := rc.resolve(op, body)
		rc.mu.Lock()
		rc.lookups--
		rc.mu.Unlock()
		select {
		case <-rc.closed:
		default:
			rc.send(replyOp[op], id, reply)
		}
	}()
}

func isASCII(b []byte) bool {
	for _, c := range b {
		if c >= 0x80 {
			return false
		}
	}
	return true
}

func dnsCode(err error) []byte {
	var de *net.DNSError
	if errors.As(err, &de) {
		switch {
		case de.IsNotFound:
			return []byte{noName}
		case de.IsTimeout, de.IsTemporary:
			return []byte{tryAgain}
		}
	}
	return []byte{lookupFailed}
}

func (rc *relayConn) resolve(op byte, body []byte) []byte {
	cx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	switch op {
	case opLookup:
		if !isASCII(body) {
			return []byte{lookupFailed}
		}
		ips, err := net.DefaultResolver.LookupNetIP(cx, "ip4", string(body))
		if err != nil {
			return dnsCode(err)
		}
		out := []byte{0}
		seen := map[netip.Addr]bool{}
		for _, ip := range ips {
			ip = ip.Unmap()
			if !ip.Is4() || seen[ip] || len(seen) >= 16 {
				continue
			}
			seen[ip] = true
			a := ip.As4()
			out = append(out, a[:]...)
		}
		return out
	case opReverse:
		if len(body) < 4 {
			return []byte{lookupFailed}
		}
		ip := netip.AddrFrom4([4]byte(body[:4]))
		names, err := net.DefaultResolver.LookupAddr(cx, ip.String())
		if err != nil {
			return dnsCode(err)
		}
		if len(names) == 0 {
			return []byte{noName}
		}
		return append([]byte{0}, strings.TrimSuffix(names[0], ".")...)
	default:
		if len(body) < 4 || !isASCII(body[4:]) {
			return []byte{lookupFailed}
		}
		cls, qtype := binary.BigEndian.Uint16(body), binary.BigEndian.Uint16(body[2:])
		answer, err := resQuery(rc.pol.nameserver, string(body[4:]), cls, qtype)
		if err != nil {
			var ne net.Error
			if errors.As(err, &ne) && ne.Timeout() {
				return []byte{tryAgain}
			}
			return []byte{lookupFailed}
		}
		return append([]byte{0}, answer...)
	}
}

// resQuery: one question to the host's name server, its whole answer
// (wire format).
func resQuery(server, name string, cls, qtype uint16) ([]byte, error) {
	qid := uint16(rand.Uint32())
	q := binary.BigEndian.AppendUint16(nil, qid)
	q = append(q, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0) // recursion desired, one question
	for _, label := range strings.Split(strings.TrimSuffix(name, "."), ".") {
		if len(label) == 0 || len(label) >= 64 {
			return nil, errors.New("a bad name")
		}
		q = append(q, byte(len(label)))
		q = append(q, label...)
	}
	q = append(q, 0)
	q = binary.BigEndian.AppendUint16(q, qtype)
	q = binary.BigEndian.AppendUint16(q, cls)
	if server == "" {
		server = nameserver()
	}
	c, err := net.Dial("udp4", net.JoinHostPort(server, "53"))
	if err != nil {
		return nil, err
	}
	defer c.Close()
	buf := make([]byte, 4096)
	var last error
	for try := 0; try < 2; try++ {
		c.Write(q)
		c.SetReadDeadline(time.Now().Add(2 * time.Second))
		for {
			n, err := c.Read(buf)
			if err != nil {
				last = err
				break
			}
			if n >= 12 && binary.BigEndian.Uint16(buf) == qid {
				return append([]byte(nil), buf[:n]...), nil
			}
		}
	}
	return nil, last
}
