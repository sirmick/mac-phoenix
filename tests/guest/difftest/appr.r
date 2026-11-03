/* appr's dialogs section: a DLOG without (200) and with (201) a 'dlgx'
   asking for the theme background, an ALRT without (300) and with (301)
   an 'alrx' asking the same; one OK button each (DITL 200) */
data 'DITL' (200) {
    $"0000"                         /* 1 item */
    $"0000 0000 0046 0082 005A 00BE" /* placeholder, rect 70,130,90,190 */
    $"04 02 4F4B 00"                /* button "OK" */
};
data 'DLOG' (200) {
    $"0064 0064 00C8 012C"          /* 100,100,200,300 */
    $"0001 0100 0000 0000 0000"     /* dBoxProc, visible, no go-away, refCon 0 */
    $"00C8 00 00 0000"              /* DITL 200, no title, noAutoCenter */
};
data 'DLOG' (201) {
    $"0064 0064 00C8 012C"
    $"0001 0100 0000 0000 0000"
    $"00C8 00 00 0000"
};
data 'dlgx' (201) {
    $"0000 0000 0001"               /* version 0, kDialogFlagsUseThemeBackground */
};
data 'ALRT' (300) {
    $"0064 0064 00C8 012C"
    $"00C8 5555 0000"               /* DITL 200, stages, noAutoCenter */
};
data 'ALRT' (301) {
    $"0064 0064 00C8 012C"
    $"00C8 5555 0000"
};
data 'alrx' (301) {
    $"0000 0000 0001"               /* version 0, kAlertFlagsUseThemeBackground */
};
/* dlgitems: a dialog with kDialogFlagsUseControlHierarchy */
data 'DLOG' (202) {
    $"0064 0064 0140 01C2"          /* 100,100,320,450 */
    $"0001 0100 0000 0000 0000"
    $"00CA 00 00 0000"              /* DITL 202 */
};
data 'dlgx' (202) {
    $"0000 0000 0003"               /* kDialogFlagsUseThemeBackground + UseControlHierarchy */
};
data 'DITL' (202) {
    $"0005"                         /* 6 items */
    $"0000 0000 00B4 010E 00C8 0154" /* 180,270,200,340 */
    $"04 04 53617665"               /* button "Save" */
    $"0000 0000 000A 000A 001A 0042" /* 10,10,26,66 */
    $"88 05 4E616D653A 00"          /* static text "Name:", disabled */
    $"0000 0000 000A 0048 001A 011E" /* 10,72,26,286 */
    $"10 04 61626364"               /* edit text "abcd" */
    $"0000 0000 0028 000A 0064 0064" /* 40,10,100,100 */
    $"80 00"                        /* user item, disabled */
    $"0000 0000 0028 0078 0048 0098" /* 40,120,72,152 */
    $"20 02 0000"                   /* icon 0 */
    $"0000 0000 0050 0078 0060 0118" /* 80,120,96,280 */
    $"07 02 00CA"                   /* control: CNTL 202 */
};
data 'CNTL' (202) {
    $"0050 0078 0060 0118"          /* 80,120,96,280 */
    $"0000 0100 0000 0000"          /* value 0, visible, max 0, min 0 */
    $"0110 0000 0000 00"            /* kControlEditTextProc (272), refCon 0, no title */
};
