/*
 * win_res.h - constants shared between win_res.rc2 and the C code.
 */

#ifndef PUTTY_WIN_RES_H
#define PUTTY_WIN_RES_H

#define IDI_MAINICON     200

#ifdef PERSOPORT

#ifdef FDJ
#define NB_ICONES	4
#else
#define NB_ICONES	45
#endif

#define IDI_MAINICON_0   1
#define IDI_MAINICON_1   2
#define IDI_MAINICON_2   3
#define IDI_MAINICON_3   4
#define IDI_MAINICON_4   5
#define IDI_MAINICON_5   6
#define IDI_MAINICON_6   7
#define IDI_MAINICON_7   8
#define IDI_MAINICON_8   9
#define IDI_MAINICON_9   10
#define IDI_MAINICON_10   11
#define IDI_MAINICON_11   12
#define IDI_MAINICON_12   13
#define IDI_MAINICON_13   14
#define IDI_MAINICON_14   15
#define IDI_MAINICON_15   16
#define IDI_MAINICON_16   17
#define IDI_MAINICON_17   18
#define IDI_MAINICON_18   19
#define IDI_MAINICON_19   20
#define IDI_MAINICON_20   21
#define IDI_MAINICON_21   22
#define IDI_MAINICON_22   23
#define IDI_MAINICON_23   24
#define IDI_MAINICON_24   25
#define IDI_MAINICON_25   26
#define IDI_MAINICON_26   27
#define IDI_MAINICON_27   28
#define IDI_MAINICON_28   29
#define IDI_MAINICON_29   30
#define IDI_MAINICON_30   31
#define IDI_MAINICON_31   32
#define IDI_MAINICON_32   33
#define IDI_MAINICON_33   34
#define IDI_MAINICON_34   35
#define IDI_MAINICON_35   36
#define IDI_MAINICON_36   37
#define IDI_MAINICON_37   38
#define IDI_MAINICON_38   39
#define IDI_MAINICON_39   40
#define IDI_MAINICON_40   41
#define IDI_MAINICON_41   42
#define IDI_MAINICON_42   43
#define IDI_MAINICON_43   44
#define IDI_MAINICON_44   45


#define IDC_HOVER 360


#define IDI_PUTTY_LAUNCH 9901
#define IDI_BLACKBALL 9902
#define IDI_EDITICON 9903

#define IDI_FILEASSOC 9904
#define IDI_NOCON 9905
#define IDI_NUCLEAR 9906

//#define IDI_PUTTY_LAUNCH IDI_MAINICON_0

#define IDD_KITTYABOUT 117
#define IDC_WEBPAGE	401
#define IDC_EMAIL 	402
#define IDC_BAN 	403

#define IDC_RESULT 1008

#define IDA_DON         1007

#endif

#define IDI_CFGICON      201

#define IDD_MAINBOX      102
#define IDD_LOGBOX       110
#define IDD_ABOUTBOX     111
#define IDD_RECONF       112
#define IDD_LICENCEBOX   113

#define IDN_LIST        1001
#define IDN_COPY        1002

#define IDA_ICON        1001
#define IDA_TEXT        1002
#define IDA_LICENCE     1003
#define IDA_WEB         1004

#ifdef PERSOPORT
#define IDA_TEXT2        1005
#define IDA_VERSION      1006
#endif

#ifdef RUTTYPORT
#define IDA_WEB2        1007
#endif

#define IDC_TAB         1001
#define IDC_TABSTATIC1  1002
#define IDC_TABSTATIC2  1003
#define IDC_TABLIST     1004
#define IDC_HELPBTN     1005
#define IDC_ABOUT       1006

#endif
