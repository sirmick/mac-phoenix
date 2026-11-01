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
