name	olecli
id	20
length	43
## 954 is too large for now
##length	954

#1 WEP
#2 OLEDELETE
#3 OLESAVETOSTREAM
#4 OLELOADFROMSTREAM
#6 OLECLONE
#7 OLECOPYFROMLINK
#8 OLEEQUAL
#9 OLEQUERYLINKFROMCLIP
#10 OLEQUERYCREATEFROMCLIP
#11 OLECREATELINKFROMCLIP
#12 OLECREATEFROMCLIP
#13 OLECOPYTOCLIPBOARD
#14 OLEQUERYTYPE
#15 OLESETHOSTNAMES
#16 OLESETTARGETDEVICE
#17 OLESETBOUNDS
#18 OLEQUERYBOUNDS
#19 OLEDRAW
#20 OLEQUERYOPEN
#21 OLEACTIVATE
#22 OLEUPDATE
#23 OLERECONNECT
#24 OLEGETLINKUPDATEOPTIONS
#25 OLESETLINKUPDATEOPTIONS
#26 OLEENUMFORMATS
#27 OLECLOSE
#28 OLEGETDATA
#29 OLESETDATA
#30 OLEQUERYPROTOCOL
#31 OLEQUERYOUTOFDATE
#32 OLEOBJECTCONVERT
#33 OLECREATEFROMTEMPLATE
#34 OLECREATE
#35 OLEQUERYRELEASESTATUS
#36 OLEQUERYRELEASEERROR
#37 OLEQUERYRELEASEMETHOD
#38 OLECREATEFROMFILE
#39 OLECREATELINKFROMFILE
#40 OLERELEASE
41  pascal OleRegisterClientDoc(ptr ptr long ptr) OleRegisterClientDoc
42  pascal OleRevokeClientDoc(long) OleRevokeClientDoc
43  pascal OleRenameClientDoc(long ptr) OleRenameClientDoc
#44 OLEREVERTCLIENTDOC
#45 OLESAVEDCLIENTDOC
#46 OLERENAME
#47 OLEENUMOBJECTS
#48 OLEQUERYNAME
#49 OLESETCOLORSCHEME
#50 OLEREQUESTDATA
#54 OLELOCKSERVER
#55 OLEUNLOCKSERVER
#56 OLEQUERYSIZE
#57 OLEEXECUTE
#58 OLECREATEINVISIBLE
#59 OLEQUERYCLIENTVERSION
#60 OLEISDCMETA
#100 DOCWNDPROC
#101 SRVRWNDPROC
#102 MFCALLBACKFUNC
#110 DEFLOADFROMSTREAM
#111 DEFCREATEFROMCLIP
#112 DEFCREATELINKFROMCLIP
#113 DEFCREATEFROMTEMPLATE
#114 DEFCREATE
#115 DEFCREATEFROMFILE
#116 DEFCREATELINKFROMFILE
#117 DEFCREATEINVISIBLE
#200 LERELEASE
#201 LESHOW
#202 LEGETDATA
#203 LESETDATA
#204 LESETHOSTNAMES
#205 LESETTARGETDEVICE
#206 LESETBOUNDS
#207 LESAVETOSTREAM
#208 LECLONE
#209 LECOPYFROMLINK
#210 LEEQUAL
#211 LECOPY
#212 LEQUERYTYPE
#213 LEQUERYBOUNDS
#214 LEDRAW
#215 LEQUERYOPEN
#216 LEACTIVATE
#218 LEUPDATE
#219 LERECONNECT
#220 LEENUMFORMAT
#221 LEQUERYPROTOCOL
#222 LEQUERYOUTOFDATE
#223 LEOBJECTCONVERT
#224 LECHANGEDATA
#225 LECLOSE
#226 LEGETUPDATEOPTIONS
#227 LESETUPDATEOPTIONS
#228 LEEXECUTE
#229 LEOBJECTLONG
#230 LECREATEINVISIBLE
#300 MFRELEASE
#301 MFGETDATA
#302 MFSAVETOSTREAM
#303 MFCLONE
#304 MFEQUAL
#305 MFCOPY
#307 MFQUERYBOUNDS
#308 MFDRAW
#309 MFENUMFORMAT
#310 MFCHANGEDATA
#400 BMRELEASE
#401 BMGETDATA
#402 BMSAVETOSTREAM
#403 BMCLONE
#404 BMEQUAL
#405 BMCOPY
#407 BMQUERYBOUNDS
#408 BMDRAW
#409 BMENUMFORMAT
#410 BMCHANGEDATA
#500 DIBRELEASE
#501 DIBGETDATA
#502 DIBSAVETOSTREAM
#503 DIBCLONE
#504 DIBEQUAL
#505 DIBCOPY
#507 DIBQUERYBOUNDS
#508 DIBDRAW
#509 DIBENUMFORMAT
#510 DIBCHANGEDATA
#600 GENRELEASE
#601 GENGETDATA
#602 GENSETDATA
#603 GENSAVETOSTREAM
#604 GENCLONE
#605 GENEQUAL
#606 GENCOPY
#608 GENQUERYBOUNDS
#609 GENDRAW
#610 GENENUMFORMAT
#611 GENCHANGEDATA
#701 ERRSHOW
#702 ERRSETDATA
#703 ERRSETHOSTNAMES
#704 ERRSETTARGETDEVICE
#705 ERRSETBOUNDS
#706 ERRCOPYFROMLINK
#707 ERRQUERYOPEN
#708 ERRACTIVATE
#709 ERRCLOSE
#710 ERRUPDATE
#711 ERRRECONNECT
#712 ERRQUERYPROTOCOL
#713 ERRQUERYOUTOFDATE
#714 ERROBJECTCONVERT
#715 ERRGETUPDATEOPTIONS
#716 ERRSETUPDATEOPTIONS
#717 ERREXECUTE
#718 ERROBJECTLONG
#800 PBLOADFROMSTREAM
#801 PBCREATEFROMCLIP
#802 PBCREATELINKFROMCLIP
#803 PBCREATEFROMTEMPLATE
#804 PBCREATE
#805 PBDRAW
#806 PBQUERYBOUNDS
#807 PBCOPYTOCLIPBOARD
#808 PBCREATEFROMFILE
#809 PBCREATELINKFROMFILE
#810 PBENUMFORMATS
#811 PBGETDATA
#812 PBCREATEINVISIBLE
#910 OBJQUERYNAME
#911 OBJRENAME
#912 OBJQUERYTYPE
#913 OBJQUERYSIZE
#950 CONNECTDLGPROC
#951 SETNETNAME
#952 CHECKNETDRIVE
#953 SETNEXTNETDRIVE
#954 GETTASKVISIBLEWINDOW
