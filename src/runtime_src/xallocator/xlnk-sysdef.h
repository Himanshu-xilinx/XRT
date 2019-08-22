#ifndef XLNK_SYSDEF_H
#define XLNK_SYSDEF_H

#if __SIZEOF_POINTER__  == 4
	#define XLNK_SYS_BIT_WIDTH 32
#elif __SIZEOF_POINTER__  == 8
	#define XLNK_SYS_BIT_WIDTH 64
#endif

#include <linux/types.h>
#include <stdint.h>

#if XLNK_SYS_BIT_WIDTH == 32

	typedef uint32_t xlnk_intptr_type;
	typedef int32_t xlnk_int_type;
	typedef uint32_t xlnk_uint_type;
	typedef uint8_t xlnk_byte_type;
	typedef int8_t xlnk_char_type;
	#define xlnk_enum_type int32_t

#elif XLNK_SYS_BIT_WIDTH == 64

	typedef uint64_t xlnk_intptr_type;
	typedef int32_t xlnk_int_type;
	typedef uint32_t xlnk_uint_type;
	typedef uint8_t xlnk_byte_type;
	typedef int8_t xlnk_char_type;
	#define xlnk_enum_type int32_t

#else
	#error "Please define application bit width and system bit width"
#endif

#endif
