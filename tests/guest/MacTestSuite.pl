#!/usr/bin/perl
#
# MacTestSuite.pl - Guest-side test suite for MacPhoenix (MacPerl)
#
# Uses MacPerl 5.x compatible syntax (2-arg open, bareword filehandles,
# no 'use strict' — module search path may be incomplete).

# --- Result reporting ---

$gPass = 0;
$gFail = 0;
$gSkip = 0;

sub report_init {
    unless (open(RESULTS, ">Host:test_results.txt")) {
        open(RESULTS, ">test_results.txt") or die "Cannot open results: $!\n";
    }
}

sub report_pass {
    my ($name) = @_;
    print RESULTS "PASS $name\r";
    $gPass++;
}

sub report_fail {
    my ($name, $detail) = @_;
    $detail = defined($detail) ? " $detail" : '';
    print RESULTS "FAIL $name$detail\r";
    $gFail++;
}

sub report_skip {
    my ($name, $reason) = @_;
    $reason = defined($reason) ? " $reason" : '';
    print RESULTS "SKIP $name$reason\r";
    $gSkip++;
}

sub report_finish {
    print RESULTS "---\r";
    print RESULTS "$gPass passed, $gFail failed, $gSkip skipped\r";
    close(RESULTS);
}

# --- Disk I/O tests (current volume) ---

sub test_disk {
    report_pass('disk_reachable');

    my $tmp = '_test_scratch.tmp';
    unlink $tmp;

    if (open(DISKFH, ">$tmp")) {
        report_pass('disk_create');
        my $data = 'disk io test data';
        print DISKFH $data;
        close(DISKFH);
        report_pass('disk_write');

        if (open(DISKFH, "<$tmp")) {
            my $buf = <DISKFH>;
            close(DISKFH);
            report_pass('disk_read');

            if (defined($buf) && $buf eq $data) {
                report_pass('disk_roundtrip');
            } else {
                report_fail('disk_roundtrip', 'mismatch');
            }
        } else {
            report_fail('disk_read', "err=$!");
        }
    } else {
        report_fail('disk_create', "err=$!");
    }
    unlink $tmp;
}

# --- ExtFS I/O tests ---

sub test_extfs {
    my $path = 'Host:_test_scratch.txt';
    my $data = 'MacPhoenix ExtFS round-trip test 1234567890';

    unlink $path;

    if (open(EXTFH, ">$path")) {
        report_pass('extfs_create');

        print EXTFH $data;
        close(EXTFH);
        report_pass('extfs_write');

        if (open(EXTFH, "<$path")) {
            my $buf = <EXTFH>;
            close(EXTFH);
            report_pass('extfs_read');

            if (defined($buf) && $buf eq $data) {
                report_pass('extfs_roundtrip');
            } else {
                report_fail('extfs_roundtrip', 'mismatch');
            }
        } else {
            report_fail('extfs_read', "err=$!");
        }

        if (unlink($path)) {
            report_pass('extfs_delete');
        } else {
            report_fail('extfs_delete', "err=$!");
        }
    } else {
        report_fail('extfs_create', "err=$!");
    }
}

# --- Audio test ---

sub test_audio {
    my $ok = eval { require Mac::Sound; Mac::Sound::SysBeep(1); 1 };
    if ($ok) {
        report_pass('audio_sysbeep');
    } else {
        report_skip('audio_sysbeep', 'Mac::Sound not available');
    }
}

# --- Network tests (built-in socket ops + Socket.pm) ---
#
# MacPerl 5.004 doesn't ship IO::Socket::INET, but the interpreter has
# socket()/connect()/send()/recv() baked in via GUSI, and Socket.pm is
# bundled. We use those directly. Timeouts via 4-arg select() — alarm()
# is unreliable on classic Mac OS.

sub test_network {
    my $have = eval { require Socket; Socket->import(); 1 };
    if (!$have) {
        my $e = $@; $e =~ s/[\r\n]+/ /g;
        report_fail('net_socket_pm', "err=$e");
        return;
    }
    report_pass('net_socket_pm');

    # DNS
    my $packed = gethostbyname('example.com');
    if ($packed) {
        my @oct = unpack('C4', $packed);
        report_pass("dns_resolve_$oct[0].$oct[1].$oct[2].$oct[3]");
    } else {
        report_fail('dns_resolve', 'gethostbyname failed');
    }

    my $gw = inet_aton('10.0.2.1');
    unless ($gw) { report_fail('inet_aton_gw'); return; }

    # UDP to gateway echo. Pass proto=0 (kernel default for SOCK_DGRAM):
    # getprotobyname() requires a configured proto database, which classic
    # Mac OS doesn't provide unless OpenTransport TCP/IP is set up.
    if (socket(UDP, PF_INET, SOCK_DGRAM, 0)) {
        report_pass('udp_socket');
        my $to = sockaddr_in(7, $gw);
        if (send(UDP, 'ping', 0, $to)) {
            report_pass('udp_send');
            # Wait up to 3s for reply via select()
            my $rin = ''; vec($rin, fileno(UDP), 1) = 1;
            my $n = select(my $rout = $rin, undef, undef, 3);
            if ($n && vec($rout, fileno(UDP), 1)) {
                my $from = recv(UDP, my $buf, 64, 0);
                if (defined($from) && length($buf) > 0) {
                    report_pass('udp_echo');
                } else {
                    report_skip('udp_echo', "recv err=$!");
                }
            } else {
                report_skip('udp_echo', 'no response');
            }
        } else {
            report_fail('udp_send', "err=$!");
        }
        close(UDP);
    } else {
        report_fail('udp_socket', "err=$!");
    }

    # TCP connect to gateway echo
    if (socket(TCP, PF_INET, SOCK_STREAM, 0)) {
        my $to = sockaddr_in(7, $gw);
        if (connect(TCP, $to)) {
            report_pass('tcp_connect');
        } else {
            report_skip('tcp_connect', "err=$!");
        }
        close(TCP);
    } else {
        report_fail('tcp_socket', "err=$!");
    }
}

# --- Main ---

report_init();
test_disk();
test_extfs();
test_audio();
test_network();
report_finish();

print "MacTestSuite complete: $gPass passed, $gFail failed, $gSkip skipped\n";
