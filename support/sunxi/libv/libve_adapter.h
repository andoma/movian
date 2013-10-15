
#ifndef LIBVE_ADAPTER_H
#define LIBVE_ADAPTER_H

#include "libve_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

    //* define function prototype for VE control interface.
    typedef void        (*VE_RESET_HARDWARE)(void);
    typedef void        (*VE_ENABLE_CLOCK)(u8 enable, u32 frequency);
    typedef void        (*VE_ENABLE_INTR)(u8 enable);
    typedef s32         (*VE_WAIT_INTR)(void);
    typedef u32         (*VE_GET_REG_BASE_ADDR)(void);
    typedef memtype_e   (*VE_GET_MEMTYPE)(void);
    
    
    //* define function prototype for system functions.
    typedef void* (*MEM_ALLOC)(u32 size);
    typedef void  (*MEM_FREE)(void* p);
    typedef void* (*MEM_PALLOC)(u32 size, u32 align);
    typedef void  (*MEM_PFREE)(void* p);
    typedef void  (*MEM_SET)(void* mem, u32 value, u32 size);
    typedef void  (*MEM_CPY)(void* dst, void* src, u32 size);
    typedef void  (*MEM_FLUSH_CACHE)(u8* mem, u32 size);
    typedef u32   (*MEM_GET_PHY_ADDR)(u32 virtual_addr);
    typedef s32   (*SYS_PRINT)(u8* func, u32 line, ...);
    typedef void  (*SYS_SLEEP)(u32 ms);


    //* define function prototype for frame buffer manage operation.
    typedef Handle      (*FBM_INIT)(u32 num_frames, u32 min_frame_num, u32 size_y, u32 size_u, u32 size_v, u32 size_alpha, pixel_format_e format);
    typedef Handle		(*FBM_INIT_EX)(u32 max_frame_num, u32 min_frame_num, u32 size_y[], u32 size_u[], u32 size_v[], u32 size_alpha[], _3d_mode_e out_3d_mode, pixel_format_e format);
    typedef void        (*FBM_RELEASE)(Handle h);
    typedef vpicture_t* (*FBM_REQUEST_FRAME)(Handle h);
    typedef void        (*FBM_RETURN_FRAME)(vpicture_t* frame, u8 valid, Handle h);
    typedef void        (*FBM_SHARE_FRAME)(vpicture_t* frame, Handle h);
    
    
    //* define function prototype for VBV bitstream manage opearation.
    typedef vstream_data_t* (*VBV_REQUEST_BITSTREAM_FRAME)(Handle vbv);
    typedef void            (*VBV_RETURN_BITSTREAM_FRAME)(vstream_data_t* stream, Handle vbv);
    typedef void            (*VBV_FLUSH_BITSTREAM_FRAME)(vstream_data_t* vstream, Handle vbv);
    typedef u8*             (*VBV_GET_BASE_ADDR)(Handle vbv);
    typedef u32             (*VBV_GET_SIZE)(Handle vbv);
    

    //*******************************************************//
    //********** Functions for VE Controlling. **************//
    //*******************************************************//
    typedef struct VE_CONTROL_INTERFACE
    {
        VE_RESET_HARDWARE    ve_reset_hardware;    //* reset VE module through CCMU VE control bits;
        VE_ENABLE_CLOCK      ve_enable_clock;      //* enable or disable VE clock;
        VE_ENABLE_INTR       ve_enable_intr;       //* enable or disable VE interrupt;
        VE_WAIT_INTR         ve_wait_intr;         //* VEBSP use this function to wait VE interrupt coming;
        VE_GET_REG_BASE_ADDR ve_get_reg_base_addr; //* return the address of the first VE register;
        VE_GET_MEMTYPE       ve_get_memtype;       //* return the dram memory type, such as DDR-1-32bits, DDR-2-16bits, DDR-2-32bits;
    }IVEControl_t;
    
    
    //*******************************************************//
    //******* Functions for OS dependent operations. ********//
    //*******************************************************//
    typedef struct OS_INTERFACE
    {
        //* Heap operations.
        MEM_ALLOC           mem_alloc;
        MEM_FREE            mem_free;
        MEM_PALLOC          mem_palloc;
        MEM_PFREE           mem_pfree;
        
        //* Memory operations.
        MEM_SET             mem_set;
        MEM_CPY             mem_cpy;
        MEM_FLUSH_CACHE     mem_flush_cache;
        MEM_GET_PHY_ADDR    mem_get_phy_addr;
        
        //* Misc functions.
        SYS_PRINT           sys_print;
        SYS_SLEEP           sys_sleep;
    }IOS_t;
    
    
    //*******************************************************//
    //****** Functions for Frame Buffer Controlling. ********//
    //*******************************************************//
    typedef struct FRAME_BUFFER_MANAGE_INTERFACE
    {
        FBM_INIT          fbm_init;           //* initialize frame buffer manager;
        FBM_RELEASE       fbm_release;        //* release frame buffer manage module;
        FBM_REQUEST_FRAME fbm_request_frame;  //* decoder request one empty frame;
        FBM_RETURN_FRAME  fbm_return_frame;   //* decoder return one valid or invalid frame;
        FBM_SHARE_FRAME   fbm_share_frame;    //* decoder using the frame, but this frame should display now.
        FBM_INIT_EX		  fbm_init_ex;		  //*
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


    extern IVEControl_t IVE;
    extern IOS_t        IOS;
    extern IFBM_t       IFBM;
    extern IVBV_t       IVBV;


#ifdef __cplusplus
}
#endif

#endif

