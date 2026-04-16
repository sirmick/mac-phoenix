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

# --- Network tests (IO::Socket over MacTCP/OT) ---

sub test_network {
    # Dump @INC once for diagnostics — helps fix module path
    if (open(DIAG, ">Host:perl_inc.txt")) {
        print DIAG "Perl version: $]\r";
        print DIAG "\@INC:\r";
        for my $p (@INC) { print DIAG "  $p\r"; }
        close(DIAG);
    }

    my $have_socket = eval { require IO::Socket::INET; 1 };
    if (!$have_socket) {
        my $e = $@; $e =~ s/[\r\n]+/ /g;
        report_fail('net_io_socket', "err=$e");
        return;
    }
    report_pass('net_io_socket');

    # DNS
    my $packed = gethostbyname('example.com');
    if ($packed) {
        my @oct = unpack('C4', $packed);
        report_pass("dns_resolve_$oct[0].$oct[1].$oct[2].$oct[3]");
    } else {
        report_fail('dns_resolve', 'gethostbyname failed');
    }

    # UDP to gateway
    my $udp = IO::Socket::INET->new(
        Proto    => 'udp',
        PeerAddr => '10.0.2.1',
        PeerPort => 7,
    );
    if ($udp) {
        report_pass('udp_socket');
        $udp->send('ping');
        report_pass('udp_send');

        my $buf = '';
        eval {
            local $SIG{ALRM} = sub { die "timeout\n" };
            alarm(3);
            $udp->recv($buf, 64);
            alarm(0);
        };
        if (length($buf) > 0) {
            report_pass('udp_echo');
        } else {
            report_skip('udp_echo', 'no response');
        }
        close($udp);
    } else {
        report_fail('udp_socket', "err=$!");
    }

    # TCP connect to gateway
    my $tcp = IO::Socket::INET->new(
        Proto    => 'tcp',
        PeerAddr => '10.0.2.1',
        PeerPort => 7,
        Timeout  => 5,
    );
    if ($tcp) {
        report_pass('tcp_connect');
        close($tcp);
    } else {
        report_skip('tcp_connect', "err=$!");
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
