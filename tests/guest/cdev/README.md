# CdevTest

A classic control panel ('cdev' -4064, 68k code resource) that writes each
message it gets to `Host:cdevtest.log`; nine variants (`cdevtest.r`) differ in
their 'mach' masks, a 'CURS' -4064 and the error each answers. Built with
`tools/mpw/mpw build tests/guest/cdev -o tests/guest/cdev`; driven by
`tests/test_cdev_shell.py` (ctest `cdev_shell_executor`), whose reference
`expected.txt` is the G3's 9.0.4 (`--backend kpx --record`).
