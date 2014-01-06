
#ifndef LIBVE_TYPEDEF_H
#define LIBVE_TYPEDEF_H

#ifdef MELIS
#include "ePDK.h"
#else
#include <stdarg.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

    //*******************************************************//
    //*************** Basic type definition. ****************//
    //*******************************************************//
    #ifndef u8
        typedef unsigned char u8;    
    #endif
    #ifndef u16
        typedef unsigned short u16;    
    #endif
    #ifndef u32
        typedef unsigned int u32;    
    #endif
    #ifndef u64
        #ifdef COMPILER_ARMCC
        	typedef unsigned __int64 u64;
        #else
        	typedef unsigned long long u64;
        #endif
    #endif
    #ifndef s8
        typedef signed char s8;    
    #endif
    #ifndef s16
        typedef signed short s16;    
    #endif
    #ifndef s32
        typedef signed int s32;    
    #endif
    #ifndef s64
        #ifdef COMPILER_ARMCC
        	typedef signed __int64 s64;
        #else
        	typedef signed long long s64;
        #endif
    #endif
    #ifndef Handle
        typedef void* Handle;
    #endif
    #ifndef NULL
        #define NULL ((void*)0)
    #endif
    
    //*******************************************************//
    //************ Define Stream Coding Formats. ************//
    //*******************************************************//
    typedef enum STREAM_FORMAT
    {
        STREAM_FORMAT_UNKNOW,
        STREAM_FORMAT_MPEG2,
        STREAM_FORMAT_MPEG4,
        STREAM_FORMAT_REALVIDEO,
        STREAM_FORMAT_H264,
        STREAM_FORMAT_VC1,
        STREAM_FORMAT_AVS,
        STREAM_FORMAT_MJPEG,
        STREAM_FORMAT_VP8
    }stream_format_e;
    
    typedef enum STREAM_SUB_FORMAT
    {
        STREAM_SUB_FORMAT_UNKNOW = 0,
        MPEG2_SUB_FORMAT_MPEG1,
        MPEG2_SUB_FORMAT_MPEG2,
        MPEG4_SUB_FORMAT_XVID,
        MPEG4_SUB_FORMAT_DIVX3,
        MPEG4_SUB_FORMAT_DIVX4,
        MPEG4_SUB_FORMAT_DIVX5,
        MPEG4_SUB_FORMAT_SORENSSON_H263,
        MPEG4_SUB_FORMAT_H263,
        MPEG4_SUB_FORMAT_RMG2,		//* H263 coded video stream muxed in '.rm' file.
        MPEG4_SUB_FORMAT_VP6,
        MPEG4_SUB_FORMAT_WMV1,
        MPEG4_SUB_FORMAT_WMV2,
        MPEG4_SUB_FORMAT_DIVX2,		//MSMPEGV2
        MPEG4_SUB_FORMAT_DIVX1		//MSMPEGV1
    }stream_sub_format_e;
    
    typedef enum CONTAINER_FORMAT
    {
    	CONTAINER_FORMAT_UNKNOW,
    	CONTAINER_FORMAT_AVI,
    	CONTAINER_FORMAT_ASF,
    	CONTAINER_FORMAT_DAT,
    	CONTAINER_FORMAT_FLV,
    	CONTAINER_FORMAT_MKV,
    	CONTAINER_FORMAT_MOV,
    	CONTAINER_FORMAT_MPG,
    	CONTAINER_FORMAT_PMP,
    	CONTAINER_FORMAT_RM,
    	CONTAINER_FORMAT_TS,
    	CONTAINER_FORMAT_VOB,
    	CONTAINER_FORMAT_WEBM,
    	CONTAINER_FORMAT_OGM,
    	CONTAINER_FORMAT_RAW,
    }container_format_e;

    //*******************************************************//
    //**************** Define Pixel Formats. ****************//
    //*******************************************************//
    typedef enum PIXEL_FORMAT
    {
        PIXEL_FORMAT_1BPP       = 0x0,
        PIXEL_FORMAT_2BPP       = 0x1,
        PIXEL_FORMAT_4BPP       = 0x2,
        PIXEL_FORMAT_8BPP       = 0x3,
        PIXEL_FORMAT_RGB655     = 0x4,
        PIXEL_FORMAT_RGB565     = 0x5,
        PIXEL_FORMAT_RGB556     = 0x6,
        PIXEL_FORMAT_ARGB1555   = 0x7,
        PIXEL_FORMAT_RGBA5551   = 0x8,
        PIXEL_FORMAT_RGB888     = 0x9,
        PIXEL_FORMAT_ARGB8888   = 0xa,
        PIXEL_FORMAT_YUV444     = 0xb,
        PIXEL_FORMAT_YUV422     = 0xc,
        PIXEL_FORMAT_YUV420     = 0xd,
        PIXEL_FORMAT_YUV411     = 0xe,
        PIXEL_FORMAT_CSIRGB     = 0xf,

        PIXEL_FORMAT_AW_YUV420  = 0x10,
        PIXEL_FORMAT_AW_YUV422	= 0x11,
        PIXEL_FORMAT_AW_YUV411  = 0x12,
        PIXEL_FORMAT_PLANNER_YUV420        = 0x13,
        PIXEL_FORMAT_PLANNER_YVU420        = 0x14
    }pixel_format_e;
    

	//*******************************************************//
	//************** Define DRAM Memory Type. ***************//
	//*******************************************************//
	typedef enum MEMORY_TYPE
	{
		MEMTYPE_DDR1_16BITS,
		MEMTYPE_DDR1_32BITS,
		MEMTYPE_DDR2_16BITS,
		MEMTYPE_DDR2_32BITS,
		MEMTYPE_DDR3_16BITS,
		MEMTYPE_DDR3_32BITS
	}memtype_e;

	typedef enum LIBVE_3D_MODE
	{
		//* for 2D pictures.
		_3D_MODE_NONE 				= 0,

		//* for double stream video like MVC and MJPEG.
		_3D_MODE_DOUBLE_STREAM,

		//* for single stream video.
		_3D_MODE_SIDE_BY_SIDE,
		_3D_MODE_TOP_TO_BOTTOM,
		_3D_MODE_LINE_INTERLEAVE,
		_3D_MODE_COLUME_INTERLEAVE,

		_3D_MODE_MAX

	}_3d_mode_e;

	typedef enum LIBVE_ANAGLAGH_TRANSFORM_MODE
	{
		ANAGLAGH_RED_BLUE		= 0,
		ANAGLAGH_RED_GREEN,
		ANAGLAGH_RED_CYAN,
		ANAGLAGH_COLOR,
		ANAGLAGH_HALF_COLOR,
		ANAGLAGH_OPTIMIZED,
		ANAGLAGH_YELLOW_BLUE,
		ANAGLAGH_NONE,
	}anaglath_trans_mode_e;

#define CEDARV_PICT_PROP_NO_SYNC    0x1
#define CEDARV_FLAG_DECODE_NO_DELAY 0x40000000

	//*******************************************************//
	//************ Define Video Frame Structure. ************//
	//*******************************************************//
	typedef struct VIDEO_PICTURE
	{
		u32             		id;                     //* picture id assigned by outside, decoder do not use this field;

		u32						width;					//* width of picture content;
		u32						height;					//* height of picture content;
        u32             		store_width;            //* stored picture width;
        u32             		store_height;           //* stored picture height;
        u32             		top_offset;				//* display region top offset;
        u32             		left_offset;			//* display region left offset;
        u32             		display_width;			//* display region width;
        u32             		display_height;			//* display region height;
        
        u8              		rotate_angle;           //* how this picture has been rotated, 0: no rotate, 1: 90 degree (clock wise), 2: 180, 3: 270, 4: horizon flip, 5: vertical flig;
        u8              		horizontal_scale_ratio; //* what ratio this picture has been scaled down at horizon size, 0: 1/1, 1: 1/2, 2: 1/4, 3: 1/8;
        u8              		vertical_scale_ratio;   //* what ratio this picture has been scaled down at vetical size, 0: 1/1, 1: 1/2, 2: 1/4, 3: 1/8;
        
        u32             		frame_rate;             //* frame_rate, multiplied by 1000;
        u32             		aspect_ratio;           //* pixel width to pixel height ratio, multiplied by 1000;
        u32                     pict_prop;              //* picture property flags
        u8              		is_progressive;         //* progressive or interlace picture;
        u8              		top_field_first;        //* display top field first;
        u8              		repeat_top_field;       //* if interlace picture, whether repeat the top field when display;
        u8              		repeat_bottom_field;    //* if interlace picture, whether repeat the bottom field when display;
        pixel_format_e  		pixel_format;
        u64             		pts;                    //* presentation time stamp, in unit of milli-second;
        u64             		pcr;                    //* program clock reference;

		_3d_mode_e   			_3d_mode;
		anaglath_trans_mode_e	anaglath_transform_mode;
		u32             		size_y;
		u32             		size_u;
		u32             		size_v;
		u32             		size_alpha;

		u8*             		y;                      //* pixel data, it is interpreted based on pixel_format;
		u8*             		u;                      //* pixel data, it is interpreted based on pixel_format;
		u8*             		v;                      //* pixel data, it is interpreted based on pixel_format;
		u8*             		alpha;                  //* pixel data, it is interpreted based on pixel_format;

		u32             		size_y2;
		u32             		size_u2;
		u32             		size_v2;
		u32             		size_alpha2;

		u8*             		y2;                      //* pixel data, it is interpreted based on pixel_format;
		u8*             		u2;                      //* pixel data, it is interpreted based on pixel_format;
		u8*             		v2;                      //* pixel data, it is interpreted based on pixel_format;
		u8*             		alpha2;                  //* pixel data, it is interpreted based on pixel_format;
		u32   					flag_addr;//dit maf flag address
	    u32						flag_stride;//dit maf flag line stride
	    u8						maf_valid;
	    u8						pre_frame_valid;
	}vpicture_t;

	//*******************************************************//
	//********** Define Bitstream Frame Structure. **********//
	//*******************************************************//

	//* define stream type for double stream video, such as MVC.
	//* in that case two video streams is send into libve.
	typedef enum CEDARV_STREAM_TYPE
	{
		CEDARV_STREAM_TYPE_MAJOR,
		CEDARV_STREAM_TYPE_MINOR,
	}cedarv_stream_type_e;

	typedef struct VIDEO_STREAM_DATA
	{
		u8* 					data;       	//* stream data start address;
		u32 					length;     	//* stream length in unit of byte;
		u64 					pts;        	//* presentation time stamp, in unit of milli-second;
		u64 					pcr;        	//* program clock reference;
		u8  					valid;      	//* whether this stream frame is valid;
		u32						id;				//* stream frame identification.
		cedarv_stream_type_e 	stream_type;	//* major or minor stream in MVC.
		u32                     pict_prop;
	}vstream_data_t;

#ifdef __cplusplus
}
#endif

#endif

