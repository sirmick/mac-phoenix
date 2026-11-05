/* desk's panes section: the Appearance control panel's tab panes. A
   dialog with kDialogFlagsUseControlHierarchy (DLOG/dlgx 400, an OK
   button) and two DITLs appended as the panel appends its tabs': each a
   user pane (embedding, item 1) with a check box, a static text and a
   button inside it; DITL 401's pane invisible as the panel's are (2400:
   CNTL 2400), DITL 402's visible. */
data 'DLOG' (400) {
    $"0064 0064 0190 0230"          /* 100,100,400,560 */
    $"0001 0100 0000 0000 0000"     /* dBoxProc, visible, no go-away, refCon 0 */
    $"0190 00 00 0000"              /* DITL 400, no title, noAutoCenter */
};
data 'dlgx' (400) {
    $"0000 0000 0003"               /* kDialogFlagsUseThemeBackground + UseControlHierarchy */
};
data 'DITL' (400) {
    $"0000"                         /* 1 item */
    $"0000 0000 0118 0190 012C 01C4" /* 280,400,300,452 */
    $"04 02 4F4B"                   /* button "OK" */
};
data 'DITL' (401) {
    $"0003"                         /* 4 items */
    $"0000 0000 000A 000A 00C8 01C2" /* 10,10,200,450 */
    $"07 02 0191"                   /* control: CNTL 401 (the pane) */
    $"0000 0000 0014 0014 0028 00C8" /* 20,20,40,200 */
    $"07 02 0192"                   /* control: CNTL 402 (a check box) */
    $"0000 0000 0032 0014 0042 00C8" /* 50,20,66,200 */
    $"88 04 54657874"               /* static text "Text", disabled */
    $"0000 0000 0050 0014 0064 0064" /* 80,20,100,100 */
    $"04 02 476F"                   /* button "Go" */
};
data 'DITL' (402) {
    $"0003"
    $"0000 0000 000A 000A 00C8 01C2"
    $"07 02 0193"                   /* control: CNTL 403 (a visible pane) */
    $"0000 0000 0014 0014 0028 00C8"
    $"07 02 0192"
    $"0000 0000 0032 0014 0042 00C8"
    $"88 04 54657874"
    $"0000 0000 0050 0014 0064 0064"
    $"04 02 476F"
};
data 'CNTL' (401) {
    $"000A 000A 00C8 01C2"          /* 10,10,200,450 */
    $"0002 0000 0000 0000"          /* value 2 (kControlSupportsEmbedding), invisible */
    $"0100 0000 0000 00"            /* kControlUserPaneProc (256), no title */
};
data 'CNTL' (402) {
    $"0014 0014 0028 00C8"
    $"0000 0100 0001 0000"          /* value 0, visible, max 1, min 0 */
    $"0001 0000 0000 05 436865636B" /* checkBoxProc, "Check" */
};
data 'CNTL' (403) {
    $"000A 000A 00C8 01C2"
    $"0002 0100 0000 0000"          /* value 2, visible */
    $"0100 0000 0000 00"
};
/* a hidden pane holding a list box (ldes 400: 3 rows, 1 column, both
   scroll bars), as the Themes tab's list (DITL 2100, CNTL 2102) */
data 'DITL' (403) {
    $"0001"                         /* 2 items */
    $"0000 0000 000A 000A 00C8 01C2" /* 10,10,200,450 */
    $"07 02 0191"                   /* control: CNTL 401 (an invisible pane) */
    $"0000 0000 0014 0014 0078 00C8" /* 20,20,120,200 */
    $"07 02 0195"                   /* control: CNTL 405 (the list box) */
};
data 'CNTL' (405) {
    $"0014 0014 0078 00C8"
    $"0190 0100 0000 0000"          /* value 400 (its 'ldes'), visible */
    $"0160 0000 0000 00"            /* kControlListBoxProc (352) */
};
data 'ldes' (400) {
    $"0000 0003 0001 0000 0000"     /* version 0, 3 rows, 1 column, default cell size */
    $"0100 0100 0000 0000"          /* vertical and horizontal scroll bars, LDEF 0, no grow box */
};
