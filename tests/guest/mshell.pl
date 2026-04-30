#!/usr/bin/perl
#
# mshell.pl — minimal telnet-style shell for classic Mac OS via MacPerl/GUSI.
#
# Listens on a TCP port, reads line-delimited commands, dispatches to Mac
# primitives + AppleScript. Single client at a time (no fork on classic Mac).
# MacPerl 5.004 compatible: 2-arg open, bareword filehandles, no 'use strict'.
#
#   guest:  perl mshell.pl [port]   (default 2323)
#   host:   nc <guest-ip> 2323

require Socket; Socket->import();

my $AF_INET     = 2;
my $SOCK_STREAM = 1;
my $IPPROTO_TCP = 6;
my $port        = $ARGV[0] || 2323;

socket(SRV, $AF_INET, $SOCK_STREAM, $IPPROTO_TCP) or die "socket: $!\n";
my $sin = pack('n n a4 x8', $AF_INET, $port, "\0\0\0\0");
bind(SRV, $sin) or die "bind :$port: $!\n";
listen(SRV, 1)  or die "listen: $!\n";
print "mshell listening on :$port\n";

my $buf = '';   # shared between serve() and do_script() so multi-line input works

while (1) {
    my $peer = accept(CLI, SRV);
    next unless $peer;
    binmode(CLI);   # we manage CR/LF ourselves; don't let GUSI rewrite \n
    serve();
    close(CLI);
}

# --- Per-connection loop ---

sub serve {
    $buf = '';
    send_line("MacPerl shell. 'help' for commands, 'quit' to disconnect.");
    prompt();
    while (defined(my $n = sysread(CLI, my $chunk, 1024))) {
        last if $n == 0;
        $chunk =~ s/\xff[\xfb-\xfe]./ /g;   # swallow telnet IAC negotiation
        $buf .= $chunk;
        while ($buf =~ s/^([^\r\n]*)[\r\n]+//) {
            my $line = $1;
            $line =~ s/^\s+|\s+$//g;
            next if $line eq '';
            return if $line eq 'quit' || $line eq 'exit';
            handle($line);
            prompt();
        }
    }
}

sub send_line { syswrite(CLI, $_[0] . "\015\012") }
sub send_raw  { syswrite(CLI, $_[0]) }

sub prompt {
    my $cwd = eval { require Cwd; Cwd::cwd() }; $cwd = ':' unless $cwd;
    syswrite(CLI, "$cwd> ");
}

# --- Dispatch ---

sub handle {
    my $line = shift;
    my ($cmd, $rest) = split(/\s+/, $line, 2);
    $rest = '' unless defined $rest;

    if    ($cmd eq 'help')   { do_help() }
    elsif ($cmd eq 'pwd')    { do_pwd() }
    elsif ($cmd eq 'cd')     { do_cd($rest) }
    elsif ($cmd eq 'ls')     { do_ls($rest) }
    elsif ($cmd eq 'cat')    { do_cat($rest) }
    elsif ($cmd eq 'rm')     { do_rm($rest) }
    elsif ($cmd eq 'mkdir')  { do_mkdir($rest) }
    elsif ($cmd eq 'stat')   { do_stat($rest) }
    elsif ($cmd eq 'beep')   { ascript('beep') }
    elsif ($cmd eq 'launch') { do_launch($rest) }
    elsif ($cmd eq 'open')   { do_open($rest) }
    elsif ($cmd eq 'apps')   { do_apps() }
    elsif ($cmd eq 'tell')   { ascript($line) }              # pass-through: "tell <app> to <stuff>"
    elsif ($cmd eq 'script') { do_script() }                 # multi-line AppleScript, end with '.'
    elsif ($cmd eq 'eval')   { do_eval($rest) }              # raw Perl
    else  { send_line("?: $cmd  (try 'help')") }
}

sub do_help {
    send_line("file:    pwd | cd <p> | ls [p] | cat <p> | rm <p> | mkdir <p> | stat <p>");
    send_line("system:  beep | apps | launch <app> | open <hfs-path>");
    send_line("script:  tell <app> to <verb...>      (one line)");
    send_line("         script                       (multi-line, end with '.')");
    send_line("         eval <perl>");
    send_line("         quit | exit");
}

# --- File commands ---

sub do_pwd {
    my $cwd = eval { require Cwd; Cwd::cwd() }; $cwd = ':' unless $cwd;
    send_line($cwd);
}

sub do_cd {
    my $p = $_[0]; $p = ':' unless length $p;
    chdir($p) or send_line("cd: $!");
}

sub do_ls {
    my $dir = $_[0]; $dir = ':' unless length $dir;
    unless (opendir(D, $dir)) { send_line("ls: $!"); return }
    my @entries = sort grep { $_ ne '.' && $_ ne '..' } readdir(D);
    closedir(D);
    for my $f (@entries) {
        my $full = ($dir =~ /:$/) ? "$dir$f" : "$dir:$f";
        my $isdir = -d $full;
        my $tag   = $isdir ? '/' : '';
        my $sz    = $isdir ? '' : (-s $full);
        my ($cr, $ty) = eval { MacPerl::GetFileInfo($full) };
        $cr = '----' unless defined $cr;
        $ty = '----' unless defined $ty;
        send_line(sprintf("%-32s %8s  %s/%s", "$f$tag", $sz, $ty, $cr));
    }
}

sub do_cat {
    my $p = $_[0]; return send_line("cat: missing path") unless length $p;
    unless (open(F, "<$p")) { send_line("cat: $!"); return }
    local $/;
    my $data = <F>;
    close(F);
    $data =~ s/\015\012|\012|\015/\015\012/g;   # normalize Mac/Unix/DOS → CRLF for the wire
    send_raw($data);
    send_line('') unless $data =~ /\015\012$/;
}

sub do_rm    { unlink($_[0])      or send_line("rm: $!") }
sub do_mkdir { mkdir($_[0], 0777) or send_line("mkdir: $!") }

sub do_stat {
    my $p = $_[0]; return send_line("stat: missing path") unless length $p;
    my @s = stat($p); return send_line("stat: $!") unless @s;
    my ($cr, $ty) = eval { MacPerl::GetFileInfo($p) };
    send_line("path:    $p");
    send_line("size:    $s[7]");
    send_line("mtime:   " . scalar(localtime($s[9])));
    send_line("type:    " . (defined $ty ? $ty : '-'));
    send_line("creator: " . (defined $cr ? $cr : '-'));
}

# --- AppleScript bridge ---

sub ascript {
    my $src = shift;
    my $out = eval { MacPerl::DoAppleScript($src) };
    if ($@) { my $e = $@; $e =~ s/[\r\n]+/ /g; send_line("script err: $e"); return }
    return unless defined($out) && length($out);
    $out =~ s/\015\012|\012|\015/\015\012/g;
    send_raw($out);
    send_line('') unless $out =~ /\015\012$/;
}

sub do_launch {
    my $app = $_[0]; return send_line("launch: missing app") unless length $app;
    ascript(qq{tell application "$app" to activate});
}

sub do_open {
    # HFS path (e.g. "HD:Stuff:doc.txt") — Finder routes to the creator app
    my $p = $_[0]; return send_line("open: missing path") unless length $p;
    ascript(qq{tell application "Finder" to open file "$p"});
}

sub do_apps {
    ascript('tell application "Finder" to get name of every process');
}

sub do_script {
    send_line("(end with '.' on its own line)");
    my $src = '';
    while (1) {
        while ($buf =~ s/^([^\r\n]*)[\r\n]+//) {
            if ($1 eq '.') { return ascript($src) }
            $src .= "$1\n";
        }
        my $n = sysread(CLI, my $chunk, 1024);
        return unless defined($n) && $n > 0;
        $buf .= $chunk;
    }
}

sub do_eval {
    my $code = shift;
    my @out = eval $code;
    if ($@) { my $e = $@; $e =~ s/[\r\n]+/ /g; send_line("eval err: $e"); return }
    send_line(join(" ", map { defined($_) ? $_ : 'undef' } @out));
}
