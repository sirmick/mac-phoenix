#!/usr/bin/perl
#
# MacKeyboardTest.pl — guest-side keystroke receiver.
#
# Roundtrip test: the host launches this script, waits for the "ready"
# marker, then sends a known keystroke sequence via /api/keypress. The
# script reads one line from STDIN (Sioux's input buffer, fed by ADB
# keyboard events from the front-app context) and writes the received
# ASCII byte values to a result file. The host then asserts the bytes
# match what it sent.
#
# This proves the full keyboard path: browser/HTTP → /api/keypress →
# server validation → ADBKeyDown/Up → key matrix → guest event queue →
# Sioux input area → Perl <STDIN>.
#
# MacPerl 5.x compatible (no 'use strict').

# Tell the host we're alive and listening. Two-step "ready" handshake
# (create marker, then block on STDIN) lets the host avoid sending keys
# before MacPerl finishes launching, which would land them in the
# Finder instead.
unless (open(MARKER, ">Host:test_keyboard_ready.txt")) {
    die "cannot create ready marker: $!";
}
print MARKER "ready\r";
close(MARKER);

# Read bytes via sysread in a polling loop. Two things ruled out the
# obvious `<STDIN>` approach: (a) Sioux's line discipline holds the
# buffer until full EOF — not just until Return arrives — so a line
# read blocks indefinitely; (b) MacPerl's `read STDIN, $buf, $n`
# inherits the same buffering. sysread bypasses it. We loop until we
# see a Return or hit a wall-clock budget.
my $line   = '';
my $start  = time();
my $budget = 15;

while ((time() - $start) < $budget) {
    my $chunk;
    my $n = sysread(STDIN, $chunk, 64);
    if (defined($n) && $n > 0) {
        $line .= $chunk;
        last if $line =~ /[\r\n]/;
    }
    # Crude busy-wait — MacPerl's Time::HiRes is unreliable; native
    # `sleep` would round to whole seconds and slow the test. ~10ms.
    for (my $i = 0; $i < 50000; $i++) { }
}

# MacPerl returns lines with \r at end; some setups also normalize to \n.
$line =~ s/[\r\n]+$//;

# Encode received bytes as decimal-comma-separated so the host doesn't
# have to deal with classic-Mac line endings or charset issues when
# parsing.
my @ords = map { ord($_) } split(//, $line);
my $encoded = join(",", @ords);

unless (open(RESULT, ">Host:test_keyboard_recv.txt")) {
    die "cannot open result: $!";
}
print RESULT $encoded, "\r";
close(RESULT);

print "Received ", scalar(@ords), " byte(s): $encoded\n";
