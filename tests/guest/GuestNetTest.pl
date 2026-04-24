#!/usr/bin/perl
#
# GuestNetTest.pl - Deeper guest-side stress of the net-bridge NAT.
#
# Existing MacTestSuite.pl::test_network only exercises gateway:7 echo
# — a reachability check. This suite targets the data paths that the
# NAT implementation actually stresses: external TCP with real data,
# sequential connect churn, external UDP, dead-host timing.
#
# Results go to Host:net_results.txt in the PASS/FAIL/SKIP format.
#
# MacPerl 5.x syntax: bareword filehandles, 2-arg open, no strict.
# Socket constants are hardcoded (MacPerl/GUSI doesn't export PF_INET etc.).

$AF_INET     = 2;
$SOCK_STREAM = 1;
$SOCK_DGRAM  = 2;
$IPPROTO_TCP = 6;
$IPPROTO_UDP = 17;

$HTTP_HOST = 'example.com';
$HTTP_PORT = 80;

# Gateway host + the bridge's in-process bulk-data server on port 8.
# The server streams 64 KB of predictable pattern (byte i = i & 0xff)
# then closes. Used to verify the NAT's flow control: a broken flow
# control pushes past the Mac's receive window, and the guest sees a
# truncated transfer with the first wrong byte at a specific offset.
$GW_IP     = '10.0.2.1';
$BULK_PORT = 8;
$BULK_BYTES = 65536;

# MacPerl's text-mode I/O translates \n to CR (Mac native newline). If we
# write "\r\n" literals in HTTP requests we get CR+CR on the wire, which
# modern HTTP servers reject as malformed. Use explicit octal byte codes:
# \015 = CR (0x0D), \012 = LF (0x0A). Do NOT write \r or \n in request
# strings.
$HTTP_CRLF = "\015\012";
$DNS_SERVER = '8.8.8.8';
$DEAD_HOST = '192.0.2.1';      # TEST-NET-1 — guaranteed non-routable

$gPass = 0;
$gFail = 0;
$gSkip = 0;

sub report_init {
    unless (open(RESULTS, '>Host:net_results.txt')) {
        open(RESULTS, '>net_results.txt') or die "results: $!\n";
    }
}
sub report_pass { my ($n) = @_; print RESULTS "PASS $n\r"; $gPass++ }
sub report_fail { my ($n,$d) = @_; $d = defined($d) ? " $d" : ''; print RESULTS "FAIL $n$d\r"; $gFail++ }
sub report_skip { my ($n,$d) = @_; $d = defined($d) ? " $d" : ''; print RESULTS "SKIP $n$d\r"; $gSkip++ }
sub report_finish {
    print RESULTS "---\r$gPass passed, $gFail failed, $gSkip skipped\r";
    close(RESULTS);
}

# sockaddr_in built by hand: family(2) + port(2) + addr(4) + pad(8).
sub pack_sin {
    my ($port, $packed_ip) = @_;
    return pack('n n a4 x8', $AF_INET, $port, $packed_ip);
}

# Read up to $max bytes with $timeout seconds, returning ($data, $err).
sub read_with_timeout {
    my ($fh, $max, $timeout) = @_;
    my $got = '';
    my $deadline = time() + $timeout;
    while (length($got) < $max) {
        my $remaining = $deadline - time();
        last if $remaining <= 0;
        my $rin = ''; vec($rin, fileno($fh), 1) = 1;
        my $n = select(my $rout = $rin, undef, undef, $remaining);
        last unless $n && vec($rout, fileno($fh), 1);
        my $chunk = '';
        my $r = sysread($fh, $chunk, $max - length($got));
        last unless defined($r);
        last if $r == 0;      # EOF
        $got .= $chunk;
    }
    return $got;
}

# -----------------------------------------------------------------------
# 1. Single HTTP GET with Content-Length integrity check.
#    Proves sequence-number handling on multi-segment receive — if seq
#    desyncs, the server's Content-Length header won't match the body
#    we receive.
# -----------------------------------------------------------------------
sub test_http_get_integrity {
    my $packed = gethostbyname($HTTP_HOST);
    unless ($packed) {
        report_fail('http_dns', "gethostbyname($HTTP_HOST)");
        return;
    }
    report_pass('http_dns');

    unless (socket(HTTP, $AF_INET, $SOCK_STREAM, $IPPROTO_TCP)) {
        report_fail('http_socket', "err=$!"); return;
    }
    my $to = pack_sin($HTTP_PORT, $packed);
    unless (connect(HTTP, $to)) {
        report_fail('http_connect', "err=$!"); close(HTTP); return;
    }
    report_pass('http_connect');

    my $req = "GET / HTTP/1.0${HTTP_CRLF}Host: ${HTTP_HOST}${HTTP_CRLF}"
            . "Connection: close${HTTP_CRLF}${HTTP_CRLF}";
    unless (syswrite(HTTP, $req, length($req))) {
        report_fail('http_write', "err=$!"); close(HTTP); return;
    }

    # Read until EOF or 64K — whichever comes first.
    my $resp = read_with_timeout(\*HTTP, 65536, 15);
    close(HTTP);

    my $got = length($resp);
    if ($got == 0) {
        report_fail('http_read', 'no data');
        return;
    }
    report_pass("http_read_${got}_bytes");

    # Dump the tail so we can see WHERE the stream cut off (hex, in case
    # binary). Limit to last 60 bytes to fit in a PASS line.
    my $tail = substr($resp, ($got > 60 ? $got - 60 : 0));
    my $hex = join('', map { sprintf('%02x', ord($_)) } split(//, $tail));
    report_pass("http_tail_hex_$hex");

    # Split off headers. Parse with explicit octal CR/LF for robustness
    # against MacPerl text-mode newline translation.
    my $blank = "\015\012\015\012";
    my $sep = index($resp, $blank);
    if ($sep < 0) {
        report_fail('http_headers', 'no blank line separator');
        return;
    }
    my $headers = substr($resp, 0, $sep);
    my $body    = substr($resp, $sep + 4);
    my $body_len = length($body);

    # Content-Length is optional (may be absent when Transfer-Encoding:
    # chunked, or when server uses Connection: close delimiter).
    my $cl;
    foreach my $line (split(/\015\012/, $headers)) {
        if ($line =~ /^Content-Length:\s*(\d+)/i) {
            $cl = $1 + 0;
            last;
        }
    }

    if (!defined($cl)) {
        report_skip('http_content_length', 'header absent (chunked or close)');
    } elsif ($cl == $body_len) {
        report_pass("http_content_length_${cl}");
    } else {
        report_fail('http_content_length',
            "header=$cl actual=$body_len delta=" . ($body_len - $cl));
    }

    # Sanity: status line looks like HTTP 2xx/3xx.
    if ($headers =~ /^HTTP\/1\.[01] (\d+)/) {
        my $status = $1 + 0;
        if ($status >= 200 && $status < 400) {
            report_pass("http_status_$status");
        } else {
            report_fail('http_status', "got $status");
        }
    } else {
        report_fail('http_status', 'no status line');
    }

    # Also pass if we got a non-trivial body — that's the real integrity
    # signal when Content-Length is absent (chunked / close-delimited).
    if ($body_len >= 200) {
        report_pass("http_body_${body_len}_bytes");
    } else {
        report_fail('http_body_short', "only $body_len bytes");
    }
}

# -----------------------------------------------------------------------
# 2. Many sequential TCP connects. Exposes fd leak, NAT conn-table leak,
#    and source-port reuse on the duplicate-SYN path.
# -----------------------------------------------------------------------
sub test_sequential_connects {
    my $packed = gethostbyname($HTTP_HOST);
    unless ($packed) { report_skip('seq_connects', 'no DNS'); return; }

    my $N = 20;
    my $ok = 0;
    my $first_fail;
    my $t0 = time();
    for (my $i = 0; $i < $N; $i++) {
        my $fh = \do { local *FH };
        unless (socket($fh, $AF_INET, $SOCK_STREAM, $IPPROTO_TCP)) {
            $first_fail = "#$i socket=$!" unless defined($first_fail);
            next;
        }
        my $to = pack_sin($HTTP_PORT, $packed);
        unless (connect($fh, $to)) {
            $first_fail = "#$i connect=$!" unless defined($first_fail);
            close($fh);
            next;
        }
        my $req = "GET / HTTP/1.0${HTTP_CRLF}Host: ${HTTP_HOST}${HTTP_CRLF}"
            . "Connection: close${HTTP_CRLF}${HTTP_CRLF}";
        unless (syswrite($fh, $req, length($req))) {
            $first_fail = "#$i write=$!" unless defined($first_fail);
            close($fh);
            next;
        }
        # Only read enough to confirm the server spoke back.
        my $head = read_with_timeout($fh, 64, 5);
        close($fh);
        if ($head =~ /^HTTP\//) {
            $ok++;
        } else {
            $first_fail = "#$i short-response" unless defined($first_fail);
        }
    }
    my $elapsed = time() - $t0;

    if ($ok == $N) {
        report_pass("seq_connects_${N}_of_${N}_in_${elapsed}s");
    } elsif ($ok > 0) {
        report_fail('seq_connects',
            "only $ok/$N ok in ${elapsed}s (first=$first_fail)");
    } else {
        report_fail('seq_connects',
            "0/$N ok in ${elapsed}s (first=$first_fail)");
    }
}

# -----------------------------------------------------------------------
# 3. External UDP: craft a minimal DNS query to 8.8.8.8, verify response.
#    The existing suite only tests gateway echo — this proves the UDP
#    NAT path reaches the real internet.
# -----------------------------------------------------------------------
sub test_external_udp_dns {
    my $dns_ip = pack('C4', split(/\./, $DNS_SERVER));

    unless (socket(DNS, $AF_INET, $SOCK_DGRAM, $IPPROTO_UDP)) {
        report_fail('udp_ext_socket', "err=$!"); return;
    }

    # DNS query for A record of example.com.
    my $txid = 0x1234;
    my $query = pack('n n n n n n', $txid, 0x0100, 1, 0, 0, 0);  # RD=1
    # QNAME: "example" "com" 0
    $query .= pack('C', 7) . 'example';
    $query .= pack('C', 3) . 'com';
    $query .= pack('C', 0);
    $query .= pack('n n', 1, 1);   # QTYPE=A, QCLASS=IN

    my $to = pack_sin(53, $dns_ip);
    unless (send(DNS, $query, 0, $to)) {
        report_fail('udp_ext_send', "err=$!"); close(DNS); return;
    }
    report_pass('udp_ext_send');

    my $rin = ''; vec($rin, fileno(DNS), 1) = 1;
    my $n = select(my $rout = $rin, undef, undef, 5);
    unless ($n && vec($rout, fileno(DNS), 1)) {
        report_fail('udp_ext_recv', 'no reply within 5s'); close(DNS); return;
    }

    my $from = recv(DNS, my $buf, 512, 0);
    close(DNS);
    unless (defined($from) && length($buf) >= 12) {
        report_fail('udp_ext_recv', 'short or no reply'); return;
    }
    report_pass("udp_ext_recv_" . length($buf) . "_bytes");

    # Parse header: same txid? answer count > 0?
    my ($rtxid, $flags, $qd, $an, $ns, $ar) = unpack('n n n n n n', substr($buf, 0, 12));
    if ($rtxid != $txid) {
        report_fail('udp_ext_txid', sprintf('sent=0x%x got=0x%x', $txid, $rtxid));
        return;
    }
    report_pass('udp_ext_txid');

    if ($an > 0) {
        report_pass("udp_ext_answers_$an");
    } else {
        my $rcode = $flags & 0xf;
        report_fail('udp_ext_answers', "none (rcode=$rcode qd=$qd)");
    }
}

# -----------------------------------------------------------------------
# 4. Dead-host connect timing. Connects to TEST-NET-1 (non-routable).
#    The connect is expected to FAIL, but *how* matters:
#      - <2s fail: host routing returned unreachable fast (healthy)
#      - 2-10s fail: tolerable
#      - >15s or hang: NAT blocked hot path (the bug we suspect)
# -----------------------------------------------------------------------
sub test_dead_host_timing {
    my $packed = pack('C4', split(/\./, $DEAD_HOST));

    unless (socket(DEAD, $AF_INET, $SOCK_STREAM, $IPPROTO_TCP)) {
        report_fail('dead_socket', "err=$!"); return;
    }
    my $to = pack_sin(80, $packed);

    my $t0 = time();
    my $ok = connect(DEAD, $to);
    my $elapsed = time() - $t0;
    close(DEAD);

    if ($ok) {
        # Shouldn't happen. 192.0.2.1 is reserved; a successful connect
        # means something in the path (transparent proxy?) is lying.
        report_fail('dead_unexpected_success', "connected in ${elapsed}s");
        return;
    }

    # Connect failed, as expected. Characterize how long it took.
    if ($elapsed <= 2) {
        report_pass("dead_connect_fast_${elapsed}s");
    } elsif ($elapsed <= 10) {
        report_pass("dead_connect_ok_${elapsed}s");
    } elsif ($elapsed <= 15) {
        report_fail('dead_connect_slow', "${elapsed}s — close to NAT's 5s bound");
    } else {
        report_fail('dead_connect_hung', "${elapsed}s — NAT likely wedged");
    }
}

# -----------------------------------------------------------------------
# 5. Large TCP transfer from the bridge's in-process bulk server on
#    gateway:8. Streams 64 KB of known-pattern data (byte i = i & 0xff).
#    Truncated result or first wrong byte pinpoints the NAT flow-control
#    bug — when we push past the Mac's advertised receive window, bytes
#    are silently dropped on the guest side.
# -----------------------------------------------------------------------
sub test_bulk_tcp_transfer {
    my $gw = pack('C4', split(/\./, $GW_IP));
    my $to = pack_sin($BULK_PORT, $gw);

    unless (socket(BULK, $AF_INET, $SOCK_STREAM, $IPPROTO_TCP)) {
        report_fail('bulk_socket', "err=$!"); return;
    }
    unless (connect(BULK, $to)) {
        report_fail('bulk_connect', "err=$!"); close(BULK); return;
    }
    report_pass('bulk_connect');

    # Drain to EOF with a generous timeout. read_with_timeout stops at
    # EOF, so we'll get exactly the bytes the server sent — no more,
    # no less. Then byte-by-byte verify against the known pattern.
    my $data = read_with_timeout(\*BULK, $BULK_BYTES + 4096, 30);
    close(BULK);

    my $got = length($data);
    if ($got == 0) {
        report_fail('bulk_read', 'no data'); return;
    }
    report_pass("bulk_received_${got}_bytes");

    if ($got != $BULK_BYTES) {
        report_fail('bulk_length',
            "expected $BULK_BYTES got $got (delta=" . ($BULK_BYTES - $got) . ")");
    } else {
        report_pass("bulk_length_exactly_${BULK_BYTES}");
    }

    # Pattern check: byte at offset i must be (i & 0xff). Find the first
    # mismatch so we can tell WHERE the flow broke.
    my $first_bad = -1;
    for (my $i = 0; $i < $got; $i++) {
        my $expected = $i & 0xff;
        my $actual = ord(substr($data, $i, 1));
        if ($actual != $expected) {
            $first_bad = $i;
            last;
        }
    }

    if ($first_bad < 0) {
        report_pass('bulk_pattern_intact');
    } else {
        report_fail('bulk_pattern',
            "first mismatch at offset $first_bad (expected " .
            ($first_bad & 0xff) . ", got " .
            ord(substr($data, $first_bad, 1)) . ")");
    }
}

# -----------------------------------------------------------------------
# 6. Post-stall liveness. Immediately after the dead-host connect, can we
#    still reach a real host? If the blocking-connect-on-hot-path bug
#    left the NAT in a bad state, this will fail.
# -----------------------------------------------------------------------
sub test_post_stall_liveness {
    my $packed = gethostbyname($HTTP_HOST);
    unless ($packed) { report_skip('post_stall', 'no DNS'); return; }

    unless (socket(LIVE, $AF_INET, $SOCK_STREAM, $IPPROTO_TCP)) {
        report_fail('post_stall_socket', "err=$!"); return;
    }
    my $to = pack_sin($HTTP_PORT, $packed);
    my $t0 = time();
    my $ok = connect(LIVE, $to);
    my $elapsed = time() - $t0;
    unless ($ok) {
        report_fail('post_stall_connect', "err=$! after ${elapsed}s");
        close(LIVE); return;
    }
    close(LIVE);
    report_pass("post_stall_connect_${elapsed}s");
}

# -----------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------

report_init();
test_http_get_integrity();
test_sequential_connects();
test_external_udp_dns();
test_bulk_tcp_transfer();
test_dead_host_timing();
test_post_stall_liveness();
report_finish();

print "GuestNetTest: $gPass passed, $gFail failed, $gSkip skipped\n";
