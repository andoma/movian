
#ifndef COMMOM_TYPE_H
#define COMMOM_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "libve_typedef.h"
//* define function prototype for frame buffer manage operation.
typedef Handle		(*FBM_INIT_EX)(u32 max_frame_num, u32 min_frame_num, u32 size_y[], u32 size_u[], u32 size_v[],
                                   u32 size_alpha[], _3d_mode_e out_3d_mode, pixel_format_e format, u8 ,  void* parent);
typedef Handle		(*FBM_INIT_EX_YV12)(u32 max_frame_num, u32 min_frame_num, u32 size_y[], u32 size_u[], u32 size_v[],
                                   u32 size_alpha[], _3d_mode_e out_3d_mode, pixel_format_e format, u8 ,  void* parent);
typedef Handle		(*FBM_INIT_YV32)(u32 max_frame_num, u32 min_frame_num, u32 size_y[], u32 size_u[], u32 size_v[],
                                   u32 size_alpha[], _3d_mode_e out_3d_mode, pixel_format_e format, u8 ,  void* parent);
typedef void        (*FBM_RELEASE)(Handle h, void* parent);
typedef vpicture_t* (*FBM_REQUEST_FRAME)(Handle h);
typedef void        (*FBM_RETURN_FRAME)(vpicture_t* frame, u8 valid, Handle h);
typedef void        (*FBM_SHARE_FRAME)(vpicture_t* frame, Handle h);
typedef void        (*FBM_FLUSH_FRAME)(Handle h, s64 pts);
typedef void        (*FBM_PRINT_STATUS) (Handle h);
typedef void        (*FBM_ALLOC_YV12_FRAME_BUFFER)(Handle h);


//* define function prototype for VBV bitstream manage opearation.
typedef vstream_data_t* (*VBV_REQUEST_BITSTREAM_FRAME)(Handle vbv);
typedef void            (*VBV_RETURN_BITSTREAM_FRAME)(vstream_data_t* stream, Handle vbv);
typedef void            (*VBV_FLUSH_BITSTREAM_FRAME)(vstream_data_t* vstream, Handle vbv);
typedef u8*             (*VBV_GET_BASE_ADDR)(Handle vbv);
typedef u32             (*VBV_GET_SIZE)(Handle vbv);

//*******************************************************//
//****** Functions for Frame Buffer Controlling. ********//
//*******************************************************//
typedef struct FRAME_BUFFER_MANAGE_INTERFACE
{
    FBM_RELEASE       fbm_release;        //* release frame buffer manage module;
    FBM_REQUEST_FRAME fbm_request_frame;  //* decoder request one empty frame;
    FBM_RETURN_FRAME  fbm_return_frame;   //* decoder return one valid or invalid frame;
    FBM_SHARE_FRAME   fbm_share_frame;    //* decoder using the frame, but this frame should display now.
    FBM_INIT_EX		  fbm_init_ex;		  //*
    FBM_INIT_EX_YV12  fbm_init_ex_yv12;
    FBM_INIT_YV32	  fbm_init_yv32;
    FBM_FLUSH_FRAME   fbm_flush_frame;
    FBM_PRINT_STATUS  fbm_print_status;
    FBM_ALLOC_YV12_FRAME_BUFFER   fbm_alloc_YV12_frame_buffer;
}IFBM_t;


//*******************************************************//
//********** Functions for VBV Controlling. *************//
//*******************************************************//
typedef struct BITSTREAM_FRAME_MANAGE_INTERFACE
{
    VBV_REQUEST_BITSTREAM_FRAME vbv_request_bitstream_frame;
    VBV_RETURN_BITSTREAM_FRAME  vbv_return_bitstream_frame;
    VBV_FLUSH_BITSTREAM_FRAME   vbv_flush_bitstream_frame;
    VBV_GET_BASE_ADDR           vbv_get_base_addr;
    VBV_GET_SIZE                vbv_get_size;
}IVBV_t;


extern IFBM_t       IFBM;
extern IVBV_t       IVBV;

#ifdef __cplusplus
}
#endif

#endif

