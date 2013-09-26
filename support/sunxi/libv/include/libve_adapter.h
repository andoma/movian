
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
    
    extern IVEControl_t IVE;
    extern IOS_t        IOS;


#ifdef __cplusplus
}
#endif

#endif

