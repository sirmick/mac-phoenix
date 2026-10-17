#!/usr/bin/perl
#
# MacPerlSmoke.pl - MacPerl itself, as a third-party application: it
# launches, takes a do-script Apple Event (/api/script, 'misc'/'dosc'),
# runs Perl, reaches the Host volume and AppleScript. The guest test suite
# proper is MacPhoenixSuite (tests/guest/suite).
#
# MacPerl 5.x compatible (2-arg open, no 'use strict').

$gPass = 0; $gFail = 0;
open(RESULTS, ">Host:macperl_results.txt") or die "results: $!\n";
sub pass { print RESULTS "PASS $_[0]\r"; $gPass++ }
sub fail { print RESULTS "FAIL $_[0] $_[1]\r"; $gFail++ }

pass('macperl_runs');

my $data = "MacPerl round trip 0123456789";
if (open(F, ">Host:_macperl_scratch.txt")) {
    print F $data; close(F);
    open(F, "<Host:_macperl_scratch.txt"); my $back = <F>; close(F);
    unlink "Host:_macperl_scratch.txt";
    $back eq $data ? pass('host_roundtrip') : fail('host_roundtrip', "got '$back'");
} else {
    fail('host_roundtrip', "err=$!");
}

my $r = eval { MacPerl::DoAppleScript('return 6 * 7') };
(defined($r) && $r eq '42') ? pass('applescript') : fail('applescript', "got '$r' ($@)");

print RESULTS "---\r$gPass passed, $gFail failed, 0 skipped\r";
close(RESULTS);
