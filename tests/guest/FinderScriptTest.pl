#!/usr/bin/perl
#
# FinderScriptTest.pl - Finder test set, driven by AppleScript.
#
# Runs inside the guest under MacPerl (dispatched by test_guest_suite.sh
# --script FinderScriptTest.pl --results finder_results.txt). Each step
# asks the Finder to do something through its scripting dictionary
# (Finder Scripting Extension + AppleScript, via MacPerl::DoAppleScript)
# and then checks the result independently, from Perl, on the disk:
# the Finder must not only say it did it, the files must show it.
#
# Works in a scratch folder "FinderTest" at the root of the startup disk,
# removed first and at the end. Same report format as MacTestSuite.pl.
#
# MacPerl 5.x compatible (2-arg open, no 'use strict').

$gPass = 0;
$gFail = 0;
$gSkip = 0;

sub report_init {
    open(RESULTS, ">Host:finder_results.txt") or die "Cannot open results: $!\n";
    select((select(RESULTS), $| = 1)[0]);
}
sub report_pass { my ($n) = @_; print RESULTS "PASS $n\r"; $gPass++; }
sub report_fail {
    my ($n, $d) = @_;
    $d = defined($d) ? " $d" : '';
    $d =~ s/[\r\n]+/ /g;
    print RESULTS "FAIL $n$d\r";
    $gFail++;
}
sub report_skip { my ($n, $r) = @_; print RESULTS "SKIP $n $r\r"; $gSkip++; }
sub report_finish {
    print RESULTS "---\r";
    print RESULTS "$gPass passed, $gFail failed, $gSkip skipped\r";
    close(RESULTS);
}

# Run AppleScript; returns (ok, result-or-error).
sub as {
    my ($src) = @_;
    my $r = eval { MacPerl::DoAppleScript($src) };
    return (0, $@ || 'error') if $@ || !defined($r);
    return (1, $r);
}

# Tell the Finder; returns (ok, result-or-error).
sub finder {
    my ($cmd) = @_;
    return as(qq{tell application "Finder" to $cmd});
}

# Check: Finder command succeeds and a Perl test holds afterwards.
sub check {
    my ($name, $cmd, $test, $what) = @_;
    my ($ok, $r) = finder($cmd);
    unless ($ok) { report_fail($name, "finder: $r"); return 0; }
    if ($test->()) { report_pass($name); return 1; }
    report_fail($name, "$what (finder said: $r)");
    return 0;
}

sub write_file {
    my ($path, $data) = @_;
    open(OUT, ">$path") or return 0;
    print OUT $data;
    close(OUT);
    return 1;
}

sub read_file {
    my ($path) = @_;
    open(IN, "<$path") or return undef;
    local $/;
    my $d = <IN>;
    close(IN);
    return $d;
}

report_init();

# --- The Finder answers ---

my ($ok, $disk) = finder('get name of startup disk');
$disk =~ s/^"(.*)"$/$1/ if $ok;
if (!$ok || $disk eq '') {
    report_fail('finder_scriptable', $disk);
    report_finish();
    die "Finder is not scriptable: $disk\n";
}
report_pass('finder_scriptable');

my $root = "$disk:FinderTest";
my $F = qq{folder "FinderTest" of startup disk};

# Start clean (a crashed earlier run may have left the folder).
finder(qq{delete $F}) if -d $root;
finder('empty trash');

# --- Folders ---

check('make_folder', qq{make new folder at startup disk with properties {name:"FinderTest"}},
      sub { -d $root }, "$root missing");
check('make_subfolder', qq{make new folder at $F with properties {name:"Sub"}},
      sub { -d "$root:Sub" }, "Sub missing");
check('rename_folder', qq{set name of folder "Sub" of $F to "Renamed"},
      sub { -d "$root:Renamed" && !-e "$root:Sub" }, "rename not on disk");

# --- Files (made by Perl, handled by the Finder) ---

my $data = "Finder test data 0123456789\n" x 20;
if (write_file("$root:a.txt", $data)) {
    report_pass('perl_create_file');
} else {
    report_fail('perl_create_file', "err=$!");
}

my ($ok2, $n) = finder(qq{count items of $F});
($ok2 && $n == 2) ? report_pass('count_items')
                  : report_fail('count_items', "got '$n' want 2");

my ($ok3, $kind) = finder(qq{get kind of folder "Renamed" of $F});
($ok3 && $kind =~ /folder/) ? report_pass('kind_folder')
                            : report_fail('kind_folder', "got '$kind'");

check('rename_file', qq{set name of file "a.txt" of $F to "b.txt"},
      sub { -f "$root:b.txt" && !-e "$root:a.txt" }, "b.txt not on disk");

check('duplicate_file', qq{duplicate file "b.txt" of $F},
      sub { -f "$root:b.txt copy" }, "'b.txt copy' missing");
my $copy = read_file("$root:b.txt copy");
(defined($copy) && $copy eq $data) ? report_pass('duplicate_contents')
                                   : report_fail('duplicate_contents', 'copy differs');

# "Dragging" an item into a folder: move.
check('move_file', qq{move file "b.txt copy" of $F to folder "Renamed" of $F},
      sub { -f "$root:Renamed:b.txt copy" && !-e "$root:b.txt copy" }, "not moved");

my ($ok4, $ex) = finder(qq{exists file "b.txt copy" of folder "Renamed" of $F});
($ok4 && $ex eq 'true') ? report_pass('exists_after_move')
                        : report_fail('exists_after_move', "got '$ex'");

# Option-drag: a copy into another folder.
check('duplicate_to_folder', qq{duplicate file "b.txt" of $F to folder "Renamed" of $F with replacing},
      sub { -f "$root:Renamed:b.txt" && -f "$root:b.txt" }, "not copied");

# Across volumes: copy to the Host shared folder.
unlink "Host:FinderTest b.txt";
check('rename_for_host', qq{set name of file "b.txt" of folder "Renamed" of $F to "FinderTest b.txt"},
      sub { -f "$root:Renamed:FinderTest b.txt" }, "rename failed");
check('copy_to_other_volume', qq{duplicate file "FinderTest b.txt" of folder "Renamed" of $F to disk "Host"},
      sub { -f "Host:FinderTest b.txt" }, "not on Host:");
my $hcopy = read_file("Host:FinderTest b.txt");
(defined($hcopy) && $hcopy eq $data) ? report_pass('other_volume_contents')
                                     : report_fail('other_volume_contents', 'copy differs');

# --- Windows ---

check('open_folder', qq{open $F}, sub { 1 }, '');
my ($ok5, $wn) = finder('get name of window 1');
($ok5 && $wn =~ /FinderTest/) ? report_pass('window_name')
                              : report_fail('window_name', "got '$wn'");
finder('set position of window 1 to {40, 60}');
my ($ok6, $pos) = finder('get position of window 1');
($ok6 && $pos =~ /40,\s*60/) ? report_pass('window_position')
                             : report_fail('window_position', "got '$pos'");
my ($ok7, $cw) = finder('close window 1');
$ok7 ? report_pass('close_window') : report_fail('close_window', $cw);

# --- Trash ---

check('delete_to_trash', qq{delete file "b.txt" of $F},
      sub { !-e "$root:b.txt" }, "still in folder");
my ($ok8, $tn) = finder('count items of trash');
($ok8 && $tn >= 1) ? report_pass('trash_has_item')
                   : report_fail('trash_has_item', "got '$tn'");
my ($ok9, $et) = finder('empty trash');
my ($ok10, $tn2) = finder('count items of trash');
($ok9 && $ok10 && $tn2 == 0) ? report_pass('empty_trash')
                             : report_fail('empty_trash', "count after: '$tn2' ($et)");

# --- Clean up ---

check('delete_folder', qq{delete $F}, sub { !-e $root }, "$root still there");
finder('empty trash');
unlink "Host:FinderTest b.txt";

report_finish();
print "FinderScriptTest complete: $gPass passed, $gFail failed, $gSkip skipped\n";
