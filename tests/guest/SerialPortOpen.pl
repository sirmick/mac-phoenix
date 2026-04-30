open(F, ">Host:serial_open.txt") or die;
print F "step1\r"; close(F);

# Dev:Pseudo: is in @INC and recognized by GUSI; .AOut returns EPERM
# under "+<" mode. Try the right unidirectional modes.

@attempts = (
    [ '>',  'Dev:Pseudo:.AOut' ],   # write-only — output channel
    [ '<',  'Dev:Pseudo:.AIn'  ],   # read-only  — input channel
    [ '>',  'Dev:Pseudo:.BOut' ],
    [ '<',  'Dev:Pseudo:.BIn'  ],
    [ '+<', 'Dev:Pseudo:Tty'   ],   # known pseudo-device per MacPerl docs
    [ '+<', 'Dev:Pseudo:Console' ],
    [ '+<', 'Dev:Pseudo:Null'  ],
);

foreach $a (@attempts) {
    $mode = $$a[0]; $path = $$a[1];
    $rv = open(SP, "$mode$path");
    $err = $!;
    if ($rv) {
        open(F, ">>Host:serial_open.txt"); print F "OPEN_OK '$mode' $path\r"; close(F);
        if ($mode eq '>') {
            $n = syswrite(SP, "PHX-WRITE-A\r");
            open(F, ">>Host:serial_open.txt"); print F "  wrote n=$n err=$!\r"; close(F);
        }
        close(SP);
    } else {
        open(F, ">>Host:serial_open.txt"); print F "open_fail '$mode' $path err=$err\r"; close(F);
    }
}

# Also list what's in Dev:Pseudo: to see what GUSI exposes.
if (opendir(D, 'Dev:Pseudo:')) {
    @files = readdir(D);
    closedir(D);
    open(F, ">>Host:serial_open.txt"); print F "Dev:Pseudo: contents: @files\r"; close(F);
} else {
    open(F, ">>Host:serial_open.txt"); print F "Dev:Pseudo: opendir err=$!\r"; close(F);
}

open(F, ">>Host:serial_open.txt"); print F "done\r"; close(F);
