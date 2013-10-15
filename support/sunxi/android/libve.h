
#ifndef LIBVE_H
#define LIBVE_H

#include "libve_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

    //*******************************************************//
    //********* Define VE LIB Function Return Value. ********//
    //*******************************************************//
    typedef enum LIBVE_RETURN_VALUE
    {
        VRESULT_OK                      = 0x0,      //* operation success;
        VRESULT_FRAME_DECODED           = 0x1,      //* decode operation decodes one frame;
        VRESULT_KEYFRAME_DECODED        = 0x3,      //* decode operation decodes one key frame;
        VRESULT_NO_FRAME_BUFFER         = 0x4,      //* fail when try to get an empty frame buffer;
        VRESULT_NO_BITSTREAM            = 0x5,      //* fail when try to get bitstream frame;
        
        VRESULT_ERR_FAIL                = -1,       //* operation fail;
        VRESULT_ERR_INVALID_PARAM       = -2,       //* failure caused by invalid function parameter;
        VRESULT_ERR_INVALID_STREAM      = -3,       //* failure caused by invalid video stream data;
        VRESULT_ERR_NO_MEMORY           = -4,       //* failure caused by memory allocation fail;
        VRESULT_ERR_UNSUPPORTED         = -5,       //* failure caused by not supported stream content;
        VRESULT_ERR_LIBRARY_NOT_OPEN    = -6        //* library not open yet;
    }vresult_e;

     typedef enum LIBVE_IO_CONTROL_CMD
     {
        LIBVE_COMMAND_GET_SEQUENCE_INF  = 0x01,
        LIBVE_COMMAND_SET_SEQUENCE_INF  = 0x02,
        LIBVE_COMMAND_SET_ANAGLAGH_TYPE = 0x3,
     }libve_io_ctrl_cmd;

    //*******************************************************//
    //***** Define VE LIB Configuration Info Structure. *****//
    //*******************************************************//
    typedef struct LIBVE_CONFIG_INFO
    {
        u32     max_video_width;            //* maximum supported video picture width;
        u32     max_video_height;           //* maximum supported video picture height;
        u32     max_output_width;           //* maximum output picture width, if exceed, decoder should use scale down function;
        u32     max_output_height;          //* maximum output picture height, if exceed, decoder should use scale down function;
        u8      scale_down_enable;          //* whether use scale down to decode small pictures;
        u8      horizon_scale_ratio;        //* specifies the horizon scale ratio,  0: 1/1; 1: 1/2; 2: 1/4; 3: 1/8;
        u8      vertical_scale_ratio;       //* specifies the vertical scale ratio, 0: 1/1; 1: 1/2; 2: 1/4; 3: 1/8;
        u8      rotate_enable;              //* whether use the rotate function to decode rotated pictures;
        u8      rotate_angle;               //* specifies the clockwise rotate angle, 0: no rotate; 1: 90 degree; 2: 180 degree; 3: 270 degree; 4: horizon flip; 5: vertical flip;
        u8      use_maf;                    //* whether use MAF to help deblocking;
        u32     max_memory_available;       //* tell the CSP how much memory space is available to the CSP, if this field is '0', the CSP take no care of memory usage;
        u32		multi_channel;				//* multi-channel decoding or decoding with encoding.
        u32		vbv_num;
    }vconfig_t;


    //*******************************************************//
    //******* Define VE LIB Initialize Info Structure. ******//
    //*******************************************************//
    typedef struct VIDEO_STREAM_INFO
    {
        stream_format_e 	format;
        stream_sub_format_e sub_format;
        container_format_e  container_format;
        u32             	video_width;        //* video picture width, if unknown please set it to 0;
        u32             	video_height;       //* video picture height, if unknown please set it to 0;
        u32             	frame_rate;         //* frame rate, multiplied by 1000, ex. 29.970 frames per second then frame_rate = 29970;
        u32					frame_duration;		//* frame duration in us;
        u32             	aspec_ratio;        //* pixel width to pixel height ratio, multiplied by 1000;
        u32             	init_data_len;      //* data length of the initial data for decoder;
        u8*             	init_data;          //* some decoders may need initial data to start up;
        u32                 is_pts_correct;     //* used for h.264 pts calc

        u32					_3d_enable;
        _3d_mode_e			source_3d_mode;
        _3d_mode_e			output_3d_mode;
        _anaglagh_e			anaglagh_type;
    }vstream_info_t;
    

    //*******************************************************//
    //**************** Define BSP Operations. ***************//
    //*******************************************************//
    Handle    libve_open(vconfig_t* config, vstream_info_t* stream_info);

    vresult_e libve_close(u8 flush_pictures, Handle libve);

    vresult_e libve_reset(u8 flush_pictures, Handle libve);
    
    vresult_e libve_flush(u8 flush_pictures);
    

    vresult_e libve_set_vbv(Handle vbv, Handle libve);
    vresult_e libve_set_minor_vbv(Handle vbv_minor, Handle libve);

    Handle    libve_get_fbm(Handle h);

    vresult_e libve_decode(u8 keyframe_only, u8 skip_bframe, u64 cur_time, Handle libve);

    vresult_e libve_get_stream_info(vstream_info_t* vstream_info, Handle libve);

    u8*       libve_get_version(Handle libve);       //* get a text description of the library's version;

    u8*       libve_get_last_error(Handle libve);    //* get a text description of last happened error;

    vresult_e libve_io_ctrl(u32 cmd, u32 param);
    u32 libve_get_fbm_num(Handle libve);
    Handle libve_get_minor_fbm(Handle libve);
    Handle libve_get_fbm(Handle libve);

#ifdef __cplusplus
}
#endif

#endif

