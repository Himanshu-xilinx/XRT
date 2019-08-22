#ifndef _XLNK_IOCTL_H
#define _XLNK_IOCTL_H

#include <linux/ioctl.h>

#define XLNK_IOC_MAGIC 'X'

#define XLNK_IOCRESET		_IO(XLNK_IOC_MAGIC, 0)

#define XLNK_IOCALLOCBUF	_IOWR(XLNK_IOC_MAGIC, 2, void*)
#define XLNK_IOCFREEBUF		_IOWR(XLNK_IOC_MAGIC, 3, void*)
#define XLNK_IOCADDDMABUF	_IOWR(XLNK_IOC_MAGIC, 4, void*)
#define XLNK_IOCCLEARDMABUF	_IOWR(XLNK_IOC_MAGIC, 5, void*)

#define XLNK_IOCDMAREQUEST	_IOWR(XLNK_IOC_MAGIC, 7, void*)
#define XLNK_IOCDMASUBMIT	_IOWR(XLNK_IOC_MAGIC, 8, void*)
#define XLNK_IOCDMAWAIT		_IOWR(XLNK_IOC_MAGIC, 9, void*)
#define XLNK_IOCDMARELEASE	_IOWR(XLNK_IOC_MAGIC, 10, void*)

#define XLNK_IOCDEVREGISTER	_IOWR(XLNK_IOC_MAGIC, 16, void*)
#define XLNK_IOCDMAREGISTER	_IOWR(XLNK_IOC_MAGIC, 17, void*)
#define XLNK_IOCDEVUNREGISTER	_IOWR(XLNK_IOC_MAGIC, 18, void*)
#define XLNK_IOCCDMAREQUEST	_IOWR(XLNK_IOC_MAGIC, 19, void*)
#define XLNK_IOCCDMASUBMIT	_IOWR(XLNK_IOC_MAGIC, 20, void*)
#define XLNK_IOCMCDMAREGISTER	_IOWR(XLNK_IOC_MAGIC, 23, void*)
#define XLNK_IOCCACHECTRL	_IOWR(XLNK_IOC_MAGIC, 24, void*)
#define XLNK_IOCMEMOP           _IOWR(XLNK_IOC_MAGIC, 25, void*)

#define XLNK_IOCSHUTDOWN	_IOWR(XLNK_IOC_MAGIC, 100, void*)
#define XLNK_IOCRECRES		_IOWR(XLNK_IOC_MAGIC, 101, void*)

#define XLNK_IOCCOMMANDSTREAM	_IOWR(XLNK_IOC_MAGIC, 40, void*)
#define XLNK_IOCCOMMANDWAIT     _IOWR(XLNK_IOC_MAGIC, 41, void*)

#define XLNK_IOCIRQREGISTER     _IOWR(XLNK_IOC_MAGIC, 35, void*)
#define XLNK_IOCIRQUNREGISTER   _IOWR(XLNK_IOC_MAGIC, 36, void*)
#define XLNK_IOCIRQWAIT         _IOWR(XLNK_IOC_MAGIC, 37, void*)

#define XLNK_IOCUSERISR         _IOWR(XLNK_IOC_MAGIC, 45, void*)

#define XLNK_IOCREGISTERDM _IOWR(XLNK_IOC_MAGIC, 46, void*)
#define XLNK_IOCREGISTERACCEL _IOWR(XLNK_IOC_MAGIC, 47, void*)
#define XLNK_IOCFREEDM _IOWR(XLNK_IOC_MAGIC, 48, void*)
#define XLNK_IOCFREEACCEL _IOWR(XLNK_IOC_MAGIC, 49, void*)

#define XLNK_IOC_MAXNR		101

#endif


