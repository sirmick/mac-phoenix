#!/usr/bin/perl
#
# SslMitmTest.pl - Guest-side smoke test for the host SSL downgrade MITM.
#
# Dispatched by the host once net-bridge is running with --mitm-tls. Opens
# a raw TCP connection to an arbitrary public :443, sends a hand-built
# SSLv3 ClientHello advertising the classic-Mac weak-RSA cipher suites,
# reads the first ServerHello record back, and verifies:
#
#   - record type is Handshake (0x16)
#   - record version is 0x0300 (SSLv3) or 0x0301 (TLS1.0)
#   - handshake type is ServerHello (0x02)
#   - selected cipher is one of {RC4-MD5, RC4-SHA, 3DES-SHA, DES-SHA,
#     export RC4-40-MD5, export DES40-SHA}
#
# If those hold, the host MITM intercepted the :443 connect, minted a
# leaf cert for the SNI, and negotiated a classic-Mac-compatible
# cipher. Without the MITM, a modern public server will reply with a
# TLS1.2/1.3 ServerHello or a protocol_version alert, which this
# script flags as FAIL so regressions are visible.
#
# MacPerl 5.x syntax: bareword filehandles, 2-arg open, no use strict.

$gPass = 0;
$gFail = 0;
$gSkip = 0;

sub report_init {
    unless (open(RESULTS, '>Host:ssl_mitm_results.txt')) {
        open(RESULTS, '>ssl_mitm_results.txt') or die "Cannot open results: $!\n";
    }
}
sub report_pass { my ($n) = @_; print RESULTS "PASS $n\r"; $gPass++ }
sub report_fail { my ($n, $d) = @_; $d = defined($d) ? " $d" : ''; print RESULTS "FAIL $n$d\r"; $gFail++ }
sub report_skip { my ($n, $d) = @_; $d = defined($d) ? " $d" : ''; print RESULTS "SKIP $n$d\r"; $gSkip++ }
sub report_finish {
    print RESULTS "---\r$gPass passed, $gFail failed, $gSkip skipped\r";
    close(RESULTS);
}

# Cipher suites we expect the MITM listener to pick. IDs per SSLv3 spec.
%WEAK_CIPHERS = (
    0x0003 => 'RSA_EXPORT_WITH_RC4_40_MD5',
    0x0004 => 'RSA_WITH_RC4_128_MD5',
    0x0005 => 'RSA_WITH_RC4_128_SHA',
    0x0008 => 'RSA_EXPORT_WITH_DES40_CBC_SHA',
    0x0009 => 'RSA_WITH_DES_CBC_SHA',
    0x000A => 'RSA_WITH_3DES_EDE_CBC_SHA',
);

sub build_sslv3_client_hello {
    # ClientHello body.
    my $random = pack('N', time());
    for (1..28) { $random .= pack('C', int(rand(256))); }
    my @ciphers = sort keys %WEAK_CIPHERS;
    my $cs = pack('n*', @ciphers);
    my $body = pack('CC', 0x03, 0x00)           # client_version = SSL 3.0
             . $random                          # random[32]
             . pack('C', 0)                     # session_id_length = 0
             . pack('n', length($cs)) . $cs     # cipher_suites
             . pack('CC', 1, 0);                # compression_methods: NULL
    # Handshake header: 1-byte type + 3-byte length.
    my $hl = length($body);
    my $hs = pack('C', 0x01)                    # type: ClientHello
           . pack('C', ($hl >> 16) & 0xff)
           . pack('n', $hl & 0xffff)
           . $body;
    # Record header: content_type(1) + version(2) + length(2).
    my $rec = pack('C', 0x16)                   # type: Handshake
            . pack('CC', 0x03, 0x00)            # SSLv3
            . pack('n', length($hs))
            . $hs;
    return $rec;
}

sub test_ssl_mitm {
    my $AF_INET     = 2;
    my $SOCK_STREAM = 1;
    my $IPPROTO_TCP = 6;

    my $host = 'example.com';
    my $packed = gethostbyname($host);
    unless ($packed) {
        report_fail('ssl_mitm_dns', "gethostbyname($host) failed");
        return;
    }
    my @oct = unpack('C4', $packed);
    report_pass("ssl_mitm_dns_$oct[0].$oct[1].$oct[2].$oct[3]");

    my $to = pack('n n a4 x8', $AF_INET, 443, $packed);
    unless (socket(SSL, $AF_INET, $SOCK_STREAM, $IPPROTO_TCP)) {
        report_fail('ssl_mitm_socket', "err=$!");
        return;
    }
    unless (connect(SSL, $to)) {
        report_fail('ssl_mitm_connect', "err=$!");
        close(SSL);
        return;
    }
    report_pass('ssl_mitm_connect');

    my $hello = build_sslv3_client_hello();
    my $w = syswrite(SSL, $hello, length($hello));
    unless (defined($w) && $w == length($hello)) {
        report_fail('ssl_mitm_write', "wrote=" . (defined($w) ? $w : 'undef'));
        close(SSL);
        return;
    }
    report_pass('ssl_mitm_write');

    # Accumulate at least the full first record. 5s timeout, a few reads.
    my $buf = '';
    my $deadline = time() + 5;
    while (length($buf) < 256 && time() < $deadline) {
        my $rin = ''; vec($rin, fileno(SSL), 1) = 1;
        my $remaining = $deadline - time();
        $remaining = 0 if $remaining < 0;
        my $n = select(my $rout = $rin, undef, undef, $remaining);
        last unless $n && vec($rout, fileno(SSL), 1);
        my $chunk = '';
        my $g = sysread(SSL, $chunk, 256);
        last unless defined($g) && $g > 0;
        $buf .= $chunk;
        last if length($buf) >= 9 + 2 + 32 + 1;  # enough for header + sid_len
    }
    close(SSL);

    if (length($buf) < 9) {
        report_fail('ssl_mitm_read', 'short ' . length($buf));
        return;
    }
    report_pass('ssl_mitm_read');

    my ($rt, $vmaj, $vmin, $rlen, $ht) = unpack('C C C n x1 C', substr($buf, 0, 11));
    # Clarify: record[0..4] = rt,vmaj,vmin,rlen(2);  record[5] = handshake type
    $ht = unpack('C', substr($buf, 5, 1));

    unless ($rt == 0x16) {
        report_fail('ssl_mitm_record_type', sprintf('got=0x%02x', $rt));
        return;
    }
    report_pass('ssl_mitm_record_handshake');

    unless ($vmaj == 0x03 && ($vmin == 0x00 || $vmin == 0x01)) {
        report_fail('ssl_mitm_version',
            sprintf('expected SSLv3/TLS1.0 got=%d.%d', $vmaj, $vmin));
        return;
    }
    report_pass($vmin == 0 ? 'ssl_mitm_sslv3' : 'ssl_mitm_tls10');

    unless ($ht == 0x02) {
        # 0x15 alert? distinguish for easier debugging.
        if ($rt == 0x15) {
            report_fail('ssl_mitm_server_hello', 'got TLS Alert');
        } else {
            report_fail('ssl_mitm_server_hello', sprintf('hs_type=0x%02x', $ht));
        }
        return;
    }
    report_pass('ssl_mitm_server_hello');

    # ServerHello body starts at offset 9 (after record hdr + 4-byte hs hdr):
    #   version(2) + random(32) + sid_len(1) + sid(sid_len) + cipher(2) + comp(1)
    my $need = 9 + 2 + 32 + 1;
    if (length($buf) < $need) {
        report_skip('ssl_mitm_cipher', 'body too short for sid_len');
        return;
    }
    my $sid_len = unpack('C', substr($buf, 9 + 2 + 32, 1));
    my $cipher_off = 9 + 2 + 32 + 1 + $sid_len;
    if (length($buf) < $cipher_off + 2) {
        report_skip('ssl_mitm_cipher', 'body truncated before cipher');
        return;
    }
    my $cipher = unpack('n', substr($buf, $cipher_off, 2));
    if (exists $WEAK_CIPHERS{$cipher}) {
        report_pass(sprintf('ssl_mitm_cipher_%s', $WEAK_CIPHERS{$cipher}));
    } else {
        report_fail('ssl_mitm_cipher', sprintf('unexpected 0x%04x', $cipher));
    }
}

report_init();
test_ssl_mitm();
report_finish();

print "SslMitmTest complete: $gPass passed, $gFail failed, $gSkip skipped\n";
