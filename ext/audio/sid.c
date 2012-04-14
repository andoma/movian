/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id: sid.c 24550 2010-02-07 18:38:47Z Buschel $
 *
 * SID Codec for rockbox based on the TinySID engine
 * 
 * Written by Tammo Hinrichs (kb) and Rainer Sinsch in 1998-1999
 * Ported to rockbox on 14 April 2006
 *
 * Modified for Showtime by Andreas Öman, 2010
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

 /*****************************
 * kb explicitly points out that this emulation sounds crappy, though
 * we decided to put it open source so everyone can enjoy sidmusic
 * on rockbox
 *
 *****************************/
 
 /*********************
 * v1.1
 * Added 16-04-2006: Rainer Sinsch
 * Removed all time critical floating point operations and
 * replaced them with quick & dirty integer calculations
 *
 * Added 17-04-2006: Rainer Sinsch
 * Improved quick & dirty integer calculations for the resonant filter
 * Improved audio quality by 4 bits
 * 
 * v1.2
 * Added 17-04-2006: Dave Chapman
 * Improved file loading
 * 
 * Added 17-04-2006: Rainer Sinsch
 * Added sample routines
 * Added cia timing routines
 * Added fast forwarding capabilities
 * Corrected bug in sid loading
 * 
 * v1.2.1
 * Added 04-05-2006: Rainer Sinsch
 * Implemented Marco Alanens suggestion for subsong selection:
 * Select the subsong by seeking: Each second represents a subsong
 * 
 **************************/

#define USE_FILTER

#include <stdint.h>
#include <string.h>
#include <stdio.h>


#define CHUNK_SIZE (1024*2)

/* This codec supports SID Files:
 * 
 */

/* Static buffer for the plain SID-File */
static unsigned char sidfile[0x10000];

static void sidPoke(int reg, unsigned char val);

#define FLAG_N 128
#define FLAG_V 64
#define FLAG_B 16
#define FLAG_D 8
#define FLAG_I 4
#define FLAG_Z 2
#define FLAG_C 1

#define imp 0
#define imm 1
#define _abs 2
#define absx 3
#define absy 4
#define zp 6
#define zpx 7
#define zpy 8
#define ind 9
#define indx 10
#define indy 11
#define acc 12
#define rel 13

enum { 
    adc, _and, asl, bcc, bcs, beq, bit, bmi, bne, bpl, brk, bvc, bvs, clc,
    cld, cli, clv, cmp, cpx, cpy, dec, dex, dey, eor, inc, inx, iny, jmp,
    jsr, lda, ldx, ldy, lsr, _nop, ora, pha, php, pla, plp, rol, ror, rti,
    rts, sbc, sec, sed, sei, sta, stx, sty, tax, tay, tsx, txa, txs, tya,
    xxx
};

/* SID register definition */
struct s6581 {
    struct sidvoice {
        unsigned short freq;
        unsigned short pulse;
        unsigned char wave;
        unsigned char ad;
        unsigned char sr;
    } v[3];
    unsigned char ffreqlo;
    unsigned char ffreqhi;
    unsigned char res_ftv;
    unsigned char ftp_vol;
};

/* internal oscillator def */
struct sidosc {
    unsigned long freq;
    unsigned long pulse;
    unsigned char wave;
    unsigned char filter;
    unsigned long attack;
    unsigned long decay;
    unsigned long sustain;
    unsigned long release;
    unsigned long counter;
    signed long   envval;
    unsigned char envphase;
    unsigned long noisepos;
    unsigned long noiseval;
    unsigned char noiseout;
};

/* internal filter def */
struct sidflt {
    int freq;
    unsigned char  l_ena;
    unsigned char  b_ena;
    unsigned char  h_ena;
    unsigned char  v3ena;
    int vol;
    int rez;
    int h;
    int b;
    int l;
};

/* ------------------------ pseudo-constants (depending on mixing freq) */
int  mixing_frequency ;
unsigned long  freqmul ;
int  filtmul ;
unsigned long  attacks [16] ;
unsigned long  releases[16] ;

/* ------------------------------------------------------------ globals */
struct s6581 sid ;
struct sidosc osc[3] ;
struct sidflt filter ;

/* ------------------------------------------------------ C64 Emu Stuff */
unsigned char bval ;
unsigned short wval ;
/* -------------------------------------------------- Register & memory */
unsigned char a,x,y,s,p ;
unsigned short pc ;

unsigned char memory[65536];

/* ----------------------------------------- Variables for sample stuff */
static int sample_active ;
static int sample_position, sample_start, sample_end, sample_repeat_start ;
static int fracPos ;  /* Fractal position of sample */
static int sample_period ;
static int sample_repeats ;
static int sample_order ;
static int sample_nibble ;

static int internal_period, internal_order, internal_start, internal_end,
           internal_add, internal_repeat_times, internal_repeat_start ;

/* ---------------------------------------------------------- constants */
static const float attackTimes[16]  =
{
    0.0022528606, 0.0080099577, 0.0157696042, 0.0237795619,
    0.0372963655, 0.0550684591, 0.0668330845, 0.0783473987,
    0.0981219818, 0.244554021,  0.489108042,  0.782472742,
    0.977715461,  2.93364701,   4.88907793,   7.82272493
};
static const float decayReleaseTimes[16]  =
{
    0.00891777693, 0.024594051, 0.0484185907, 0.0730116639, 0.114512475,
    0.169078356, 0.205199432, 0.240551975, 0.301266125, 0.750858245,
    1.50171551, 2.40243682, 3.00189298, 9.00721405, 15.010998, 24.0182111
};

static const int opcodes[256]  = {
    brk,ora,xxx,xxx,xxx,ora,asl,xxx,php,ora,asl,xxx,xxx,ora,asl,xxx,
    bpl,ora,xxx,xxx,xxx,ora,asl,xxx,clc,ora,xxx,xxx,xxx,ora,asl,xxx,
    jsr,_and,xxx,xxx,bit,_and,rol,xxx,plp,_and,rol,xxx,bit,_and,rol,xxx,
    bmi,_and,xxx,xxx,xxx,_and,rol,xxx,sec,_and,xxx,xxx,xxx,_and,rol,xxx,
    rti,eor,xxx,xxx,xxx,eor,lsr,xxx,pha,eor,lsr,xxx,jmp,eor,lsr,xxx,
    bvc,eor,xxx,xxx,xxx,eor,lsr,xxx,cli,eor,xxx,xxx,xxx,eor,lsr,xxx,
    rts,adc,xxx,xxx,xxx,adc,ror,xxx,pla,adc,ror,xxx,jmp,adc,ror,xxx,
    bvs,adc,xxx,xxx,xxx,adc,ror,xxx,sei,adc,xxx,xxx,xxx,adc,ror,xxx,
    xxx,sta,xxx,xxx,sty,sta,stx,xxx,dey,xxx,txa,xxx,sty,sta,stx,xxx,
    bcc,sta,xxx,xxx,sty,sta,stx,xxx,tya,sta,txs,xxx,xxx,sta,xxx,xxx,
    ldy,lda,ldx,xxx,ldy,lda,ldx,xxx,tay,lda,tax,xxx,ldy,lda,ldx,xxx,
    bcs,lda,xxx,xxx,ldy,lda,ldx,xxx,clv,lda,tsx,xxx,ldy,lda,ldx,xxx,
    cpy,cmp,xxx,xxx,cpy,cmp,dec,xxx,iny,cmp,dex,xxx,cpy,cmp,dec,xxx,
    bne,cmp,xxx,xxx,xxx,cmp,dec,xxx,cld,cmp,xxx,xxx,xxx,cmp,dec,xxx,
    cpx,sbc,xxx,xxx,cpx,sbc,inc,xxx,inx,sbc,_nop,xxx,cpx,sbc,inc,xxx,
    beq,sbc,xxx,xxx,xxx,sbc,inc,xxx,sed,sbc,xxx,xxx,xxx,sbc,inc,xxx
};


static const int modes[256]  = {
    imp,indx,xxx,xxx,zp,zp,zp,xxx,imp,imm,acc,xxx,_abs,_abs,_abs,xxx,
    rel,indy,xxx,xxx,xxx,zpx,zpx,xxx,imp,absy,xxx,xxx,xxx,absx,absx,xxx,
    _abs,indx,xxx,xxx,zp,zp,zp,xxx,imp,imm,acc,xxx,_abs,_abs,_abs,xxx,
    rel,indy,xxx,xxx,xxx,zpx,zpx,xxx,imp,absy,xxx,xxx,xxx,absx,absx,xxx,
    imp,indx,xxx,xxx,zp,zp,zp,xxx,imp,imm,acc,xxx,_abs,_abs,_abs,xxx,
    rel,indy,xxx,xxx,xxx,zpx,zpx,xxx,imp,absy,xxx,xxx,xxx,absx,absx,xxx,
    imp,indx,xxx,xxx,zp,zp,zp,xxx,imp,imm,acc,xxx,ind,_abs,_abs,xxx,
    rel,indy,xxx,xxx,xxx,zpx,zpx,xxx,imp,absy,xxx,xxx,xxx,absx,absx,xxx,
    imm,indx,xxx,xxx,zp,zp,zp,xxx,imp,imm,acc,xxx,_abs,_abs,_abs,xxx,
    rel,indy,xxx,xxx,zpx,zpx,zpy,xxx,imp,absy,acc,xxx,xxx,absx,absx,xxx,
    imm,indx,imm,xxx,zp,zp,zp,xxx,imp,imm,acc,xxx,_abs,_abs,_abs,xxx,
    rel,indy,xxx,xxx,zpx,zpx,zpy,xxx,imp,absy,acc,xxx,absx,absx,absy,xxx,
    imm,indx,xxx,xxx,zp,zp,zp,xxx,imp,imm,acc,xxx,_abs,_abs,_abs,xxx,
    rel,indy,xxx,xxx,zpx,zpx,zpx,xxx,imp,absy,acc,xxx,xxx,absx,absx,xxx,
    imm,indx,xxx,xxx,zp,zp,zp,xxx,imp,imm,acc,xxx,_abs,_abs,_abs,xxx,
    rel,indy,xxx,xxx,zpx,zpx,zpx,xxx,imp,absy,acc,xxx,xxx,absx,absx,xxx
};

/* Routines for quick & dirty float calculation */

static inline int quickfloat_ConvertFromInt(int i)
{
    return (i<<16);
}
static inline int quickfloat_ConvertFromFloat(float f)
{
    return (int)(f*(1<<16));
}
static inline int quickfloat_Multiply(int a, int b)
{
    return (a>>8)*(b>>8);
}
static inline int quickfloat_ConvertToInt(int i)
{
    return (i>>16);
}

/* Get the bit from an unsigned long at a specified position */
static inline unsigned char get_bit(unsigned long val, unsigned char b)
{
    return (unsigned char) ((val >> b) & 1);
}


static inline int GenerateDigi(int sIn)
{
    static int sample = 0;

    if (!sample_active) return(sIn);

    if ((sample_position < sample_end) && (sample_position >= sample_start))
    {
        sIn += sample;
        
        fracPos += 985248/sample_period;
        
        if (fracPos > mixing_frequency) 
        {
            fracPos%=mixing_frequency;

            // N�hstes Samples holen
            if (sample_order == 0) {
                sample_nibble++;                        // Nähstes Sample-Nibble
                if (sample_nibble==2) {
                    sample_nibble = 0;
                    sample_position++;
                }
            }
            else {
                sample_nibble--;
                if (sample_nibble < 0) {
                    sample_nibble=1;
                    sample_position++;
                }
            }       
            if (sample_repeats)
            {
                if  (sample_position > sample_end)
                {
                    sample_repeats--;
                    sample_position = sample_repeat_start;
                }                       
                else sample_active = 0;
            }
            
            sample = memory[sample_position&0xffff];
            if (sample_nibble==1)   // Hi-Nibble holen?     
                sample = (sample & 0xf0)>>4;
            else sample = sample & 0x0f;
            
            sample -= 7;
            sample <<= 10;  
        }
    }

    return (sIn);
}

/* ------------------------------------------------------------- synthesis
   initialize SID and frequency dependant values */
static void synth_init(unsigned long mixfrq)
{
    int i;
    mixing_frequency = mixfrq;
    fracPos = 0;
    freqmul = 15872000 / mixfrq;
    filtmul = quickfloat_ConvertFromFloat(21.5332031f)/mixfrq;
    for (i=0;i<16;i++) {
        attacks [i]=(int) (0x1000000 / (attackTimes[i]*mixfrq));
        releases[i]=(int) (0x1000000 / (decayReleaseTimes[i]*mixfrq));
    }
    memset(&sid,0,sizeof(sid));
    memset(osc,0,sizeof(osc));
    memset(&filter,0,sizeof(filter));
    osc[0].noiseval = 0xffffff;
    osc[1].noiseval = 0xffffff;
    osc[2].noiseval = 0xffffff;  
}

/* render a buffer of n samples with the actual register contents */
static void synth_render (int16_t *buffer, unsigned long len)
{
    unsigned long bp;
    /* step 1: convert the not easily processable sid registers into some
               more convenient and fast values (makes the thing much faster
              if you process more than 1 sample value at once) */
    unsigned char v;
    for (v=0;v<3;v++) {
        osc[v].pulse   = (sid.v[v].pulse & 0xfff) << 16;
        osc[v].filter  = get_bit(sid.res_ftv,v);
        osc[v].attack  = attacks[sid.v[v].ad >> 4];
        osc[v].decay   = releases[sid.v[v].ad & 0xf];
        osc[v].sustain = sid.v[v].sr & 0xf0;
        osc[v].release = releases[sid.v[v].sr & 0xf];
        osc[v].wave    = sid.v[v].wave;
        osc[v].freq    = ((unsigned long)sid.v[v].freq)*freqmul;
    }

#ifdef USE_FILTER
    filter.freq  = (16*sid.ffreqhi + (sid.ffreqlo&0x7)) * filtmul;
  
    if (filter.freq>quickfloat_ConvertFromInt(1))
        filter.freq=quickfloat_ConvertFromInt(1);
    /* the above line isnt correct at all - the problem is that the filter
       works only up to rmxfreq/4 - this is sufficient for 44KHz but isnt
       for 32KHz and lower - well, but sound quality is bad enough then to
       neglect the fact that the filter doesnt come that high ;) */
    filter.l_ena = get_bit(sid.ftp_vol,4);
    filter.b_ena = get_bit(sid.ftp_vol,5);
    filter.h_ena = get_bit(sid.ftp_vol,6);
    filter.v3ena = !get_bit(sid.ftp_vol,7);
    filter.vol   = (sid.ftp_vol & 0xf);
    filter.rez   = quickfloat_ConvertFromFloat(1.2f) - 
                    quickfloat_ConvertFromFloat(0.04f)*(sid.res_ftv >> 4);
    
    /* We precalculate part of the quick float operation, saves time in loop later */
    filter.rez>>=8;
#endif
  
  
    /* now render the buffer */
    for (bp=0;bp<len;bp++) {
#ifdef USE_FILTER
        int outo=0;
#endif
        int outf=0;
    /* step 2 : generate the two output signals (for filtered and non-
                filtered) from the osc/eg sections */
    for (v=0;v<3;v++) {
            /* update wave counter */
            osc[v].counter = (osc[v].counter+osc[v].freq) & 0xFFFFFFF;
            /* reset counter / noise generator if reset get_bit set */
            if (osc[v].wave & 0x08) {
                osc[v].counter  = 0;
                osc[v].noisepos = 0;
                osc[v].noiseval = 0xffffff;
            }
            unsigned char refosc = v?v-1:2;  /* reference oscillator for sync/ring */
            /* sync oscillator to refosc if sync bit set */
            if (osc[v].wave & 0x02)
                if (osc[refosc].counter < osc[refosc].freq)
                    osc[v].counter = osc[refosc].counter * osc[v].freq / osc[refosc].freq;
            /* generate waveforms with really simple algorithms */
            unsigned char triout = (unsigned char) (osc[v].counter>>19);
            if (osc[v].counter>>27)
                triout^=0xff;
            unsigned char sawout = (unsigned char) (osc[v].counter >> 20);
            unsigned char plsout = (unsigned char) ((osc[v].counter > osc[v].pulse)-1);

            /* generate noise waveform exactly as the SID does. */
            if (osc[v].noisepos!=(osc[v].counter>>23))
            {
                osc[v].noisepos = osc[v].counter >> 23;
                osc[v].noiseval = (osc[v].noiseval << 1) |
                        (get_bit(osc[v].noiseval,22) ^ get_bit(osc[v].noiseval,17));
                osc[v].noiseout = (get_bit(osc[v].noiseval,22) << 7) |
                        (get_bit(osc[v].noiseval,20) << 6) |
                        (get_bit(osc[v].noiseval,16) << 5) |
                        (get_bit(osc[v].noiseval,13) << 4) |
                        (get_bit(osc[v].noiseval,11) << 3) |
                        (get_bit(osc[v].noiseval, 7) << 2) |
                        (get_bit(osc[v].noiseval, 4) << 1) |
                        (get_bit(osc[v].noiseval, 2) << 0);
            }
            unsigned char nseout = osc[v].noiseout;

            /* modulate triangle wave if ringmod bit set */
            if (osc[v].wave & 0x04)
                if (osc[refosc].counter < 0x8000000)
                    triout ^= 0xff;

            /* now mix the oscillators with an AND operation as stated in
               the SID's reference manual - even if this is completely wrong.
               well, at least, the $30 and $70 waveform sounds correct and there's
               no real solution to do $50 and $60, so who cares. */

            unsigned char outv=0xFF;
            if (osc[v].wave & 0x10) outv &= triout;
            if (osc[v].wave & 0x20) outv &= sawout;
            if (osc[v].wave & 0x40) outv &= plsout;
            if (osc[v].wave & 0x80) outv &= nseout;
            
            /* so now process the volume according to the phase and adsr values */
            switch (osc[v].envphase) {
                case 0 : {                          /* Phase 0 : Attack */
                    osc[v].envval+=osc[v].attack;
                    if (osc[v].envval >= 0xFFFFFF)
                    {
                        osc[v].envval   = 0xFFFFFF;
                        osc[v].envphase = 1;
                    }
                    break;
                }
                case 1 : {                          /* Phase 1 : Decay */
                    osc[v].envval-=osc[v].decay;
                    if ((signed int) osc[v].envval <= (signed int) (osc[v].sustain<<16))
                    {
                        osc[v].envval   = osc[v].sustain<<16;
                        osc[v].envphase = 2;
                    }
                    break;
                }
                case 2 : {                          /* Phase 2 : Sustain */
                    if ((signed int) osc[v].envval != (signed int) (osc[v].sustain<<16))
                    {
                        osc[v].envphase = 1;
                    }
                    /* :) yes, thats exactly how the SID works. and maybe
                       a music routine out there supports this, so better
                       let it in, thanks :) */
                    break;
                }
                case 3 : {                          /* Phase 3 : Release */
                    osc[v].envval-=osc[v].release;
                    if (osc[v].envval < 0x40000) osc[v].envval= 0x40000;

                    /* the volume offset is because the SID does not
                       completely silence the voices when it should. most
                       emulators do so though and thats the main reason
                       why the sound of emulators is too, err... emulated :)  */
                    break;
                }
            }
  
#ifdef USE_FILTER

            /* now route the voice output to either the non-filtered or the
               filtered channel and dont forget to blank out osc3 if desired */
  
            if (v<2 || filter.v3ena)
            {
                if (osc[v].filter)
                    outf+=(((int)(outv-0x80))*osc[v].envval)>>22;
                else
                    outo+=(((int)(outv-0x80))*osc[v].envval)>>22;
            }
#endif
#ifndef USE_FILTER
            /* Don't use filters, just mix all voices together */
            outf+=((signed short)(outv-0x80)) * (osc[v].envval>>4);
#endif
        }


#ifdef USE_FILTER
        /* step 3
         * so, now theres finally time to apply the multi-mode resonant filter
         * to the signal. The easiest thing ist just modelling a real electronic
         * filter circuit instead of fiddling around with complex IIRs or even
         * FIRs ...
         * it sounds as good as them or maybe better and needs only 3 MULs and
         * 4 ADDs for EVERYTHING. SIDPlay uses this kind of filter, too, but
         * Mage messed the whole thing completely up - as the rest of the
         * emulator.
         * This filter sounds a lot like the 8580, as the low-quality, dirty
         * sound of the 6581 is uuh too hard to achieve :) */

        filter.h = quickfloat_ConvertFromInt(outf) - (filter.b>>8)*filter.rez - filter.l;
        filter.b += quickfloat_Multiply(filter.freq, filter.h);
        filter.l += quickfloat_Multiply(filter.freq, filter.b);

        outf = 0;

        if (filter.l_ena) outf+=quickfloat_ConvertToInt(filter.l);
        if (filter.b_ena) outf+=quickfloat_ConvertToInt(filter.b);
        if (filter.h_ena) outf+=quickfloat_ConvertToInt(filter.h);

        int final_sample = (filter.vol*(outo+outf));        
        *(buffer+bp)= GenerateDigi(final_sample);
#endif
#ifndef USE_FILTER
        *(buffer+bp) = GenerateDigi(outf)<<3;
#endif
    }
}



/*
* C64 Mem Routines
*/
static inline unsigned char getmem(unsigned short addr)
{    
    return memory[addr];
}

static inline void setmem(unsigned short addr, unsigned char value)
{
    if ((addr&0xfc00)==0xd400)
    {        
        sidPoke(addr&0x1f,value);    
        /* New SID-Register */
        if (addr > 0xd418)
        {                
            switch (addr)
            {
                case 0xd41f:    /* Start-Hi */
                    internal_start = (internal_start&0x00ff) | (value<<8); break;
                case 0xd41e:    /* Start-Lo */
                    internal_start = (internal_start&0xff00) | (value); break;
                case 0xd47f:    /* Repeat-Hi */
                    internal_repeat_start = (internal_repeat_start&0x00ff) | (value<<8); break;
                case 0xd47e:    /* Repeat-Lo */
                    internal_repeat_start = (internal_repeat_start&0xff00) | (value); break;
                case 0xd43e:    /* End-Hi */
                    internal_end = (internal_end&0x00ff) | (value<<8); break;
                case 0xd43d:    /* End-Lo */
                    internal_end = (internal_end&0xff00) | (value); break;
                case 0xd43f:    /* Loop-Size */
                    internal_repeat_times = value; break;
                case 0xd45e:    /* Period-Hi */
                    internal_period = (internal_period&0x00ff) | (value<<8); break;
                case 0xd45d:    /* Period-Lo */
                    internal_period = (internal_period&0xff00) | (value); break;
                case 0xd47d:    /* Sample Order */
                    internal_order = value; break;
                case 0xd45f:    /* Sample Add */
                    internal_add = value; break;
                case 0xd41d:    /* Start sampling */                
                    sample_repeats = internal_repeat_times;
                    sample_position = internal_start;
                    sample_start = internal_start; 
                    sample_end = internal_end;
                    sample_repeat_start = internal_repeat_start;
                    sample_period = internal_period;
                    sample_order = internal_order;
                    switch (value)
                    {
                        case 0xfd: sample_active = 0; break;
                        case 0xfe: 
                        case 0xff: sample_active = 1; break;
                        default: return;
                    }
                    break;
            }            
        } 
    }
    else memory[addr]=value;
}

/*
* Poke a value into the sid register
*/
static void sidPoke(int reg, unsigned char val)
{
    int voice=0;

    if ((reg >= 7) && (reg <=13)) {voice=1; reg-=7;}
    else if ((reg >= 14) && (reg <=20)) {voice=2; reg-=14;}

    switch (reg) {
        case 0: { /* Set frequency: Low byte */
            sid.v[voice].freq = (sid.v[voice].freq&0xff00)+val;
            break;
        }
        case 1: { /* Set frequency: High byte */
            sid.v[voice].freq = (sid.v[voice].freq&0xff)+(val<<8);
            break;
        }
        case 2: { /* Set pulse width: Low byte */
            sid.v[voice].pulse = (sid.v[voice].pulse&0xff00)+val;
            break;
        }
        case 3: { /* Set pulse width: High byte */
            sid.v[voice].pulse = (sid.v[voice].pulse&0xff)+(val<<8);
            break;
        }
        case 4: { sid.v[voice].wave = val; 
            /* Directly look at GATE-Bit!
             * a change may happen twice or more often during one cpujsr
             * Put the Envelope Generator into attack or release phase if desired 
            */
            if ((val & 0x01) == 0) osc[voice].envphase=3;
            else if (osc[voice].envphase==3) osc[voice].envphase=0;
            break;
        }

        case 5: { sid.v[voice].ad = val; break;}
        case 6: { sid.v[voice].sr = val; break;}

        case 21: { sid.ffreqlo = val; break; }
        case 22: { sid.ffreqhi = val; break; }
        case 23: { sid.res_ftv = val; break; }
        case 24: { sid.ftp_vol = val; break;}
    }
    return;
}

static inline unsigned char getaddr(int mode)
{
    unsigned short ad,ad2;  
    switch(mode)
    {
        case imp:
            return 0;
        case imm:
            return getmem(pc++);
        case _abs:
            ad=getmem(pc++);
            ad|=256*getmem(pc++);
            return getmem(ad);
        case absx:
            ad=getmem(pc++);
            ad|=256*getmem(pc++);
            ad2=ad+x;
            return getmem(ad2);
        case absy:
            ad=getmem(pc++);
            ad|=256*getmem(pc++);
            ad2=ad+y;                
            return getmem(ad2);
        case zp:
            ad=getmem(pc++);
            return getmem(ad);
        case zpx:
            ad=getmem(pc++);
            ad+=x;
            return getmem(ad&0xff);
        case zpy:
            ad=getmem(pc++);
            ad+=y;
            return getmem(ad&0xff);
        case indx:
            ad=getmem(pc++);
            ad+=x;
            ad2=getmem(ad&0xff);
            ad++;
            ad2|=getmem(ad&0xff)<<8;
            return getmem(ad2);
        case indy:
            ad=getmem(pc++);
            ad2=getmem(ad);
            ad2|=getmem((ad+1)&0xff)<<8;
            ad=ad2+y;                
            return getmem(ad);
        case acc:
            return a;
    }  
    return 0;
}

static inline void setaddr(int mode, unsigned char val)
{
    unsigned short ad,ad2;
    switch(mode)
    {
        case _abs:
            ad=getmem(pc-2);
            ad|=256*getmem(pc-1);
            setmem(ad,val);
            return;
        case absx:
            ad=getmem(pc-2);
            ad|=256*getmem(pc-1);
            ad2=ad+x;                
            setmem(ad2,val);
            return;
        case zp:
            ad=getmem(pc-1);
            setmem(ad,val);
            return;
        case zpx:
            ad=getmem(pc-1);
            ad+=x;
            setmem(ad&0xff,val);
            return;
        case acc:
            a=val;
            return;
    }
}


static inline void putaddr(int mode, unsigned char val)
{
    unsigned short ad,ad2;
    switch(mode)
    {
        case _abs:
            ad=getmem(pc++);
            ad|=getmem(pc++)<<8;
            setmem(ad,val);
            return;
        case absx:
            ad=getmem(pc++);
            ad|=getmem(pc++)<<8;
            ad2=ad+x;
            setmem(ad2,val);
            return;
        case absy:
            ad=getmem(pc++);
            ad|=getmem(pc++)<<8;
            ad2=ad+y;                
            setmem(ad2,val);
            return;
        case zp:
            ad=getmem(pc++);
            setmem(ad,val);
            return;
        case zpx:
            ad=getmem(pc++);
            ad+=x;
            setmem(ad&0xff,val);
            return;
        case zpy:
            ad=getmem(pc++);
            ad+=y;
            setmem(ad&0xff,val);
            return;
        case indx:
            ad=getmem(pc++);
            ad+=x;
            ad2=getmem(ad&0xff);
            ad++;
            ad2|=getmem(ad&0xff)<<8;
            setmem(ad2,val);
            return;
        case indy:      
            ad=getmem(pc++);
            ad2=getmem(ad);
            ad2|=getmem((ad+1)&0xff)<<8;
            ad=ad2+y;
            setmem(ad,val);
            return;
        case acc:
            a=val;
            return;
    }
}


static inline void setflags(int flag, int cond)
{
    if (cond) p|=flag;
    else p&=~flag;
}


static inline void push(unsigned char val)
{
    setmem(0x100+s,val);
    if (s) s--;
}

static inline unsigned char pop(void)
{
    if (s<0xff) s++;
    return getmem(0x100+s);
}

static inline void branch(int flag)
{
    signed char dist;
    dist=(signed char)getaddr(imm);
    wval=pc+dist;
    if (flag) pc=wval;
}

void cpuReset(void) ;
void cpuReset(void)
{
    a=x=y=0;
    p=0;
    s=255; 
    pc=getaddr(0xfffc);  
}

void cpuResetTo(unsigned short npc, unsigned char na) ;
void cpuResetTo(unsigned short npc, unsigned char na)
{
    a=na;
    x=0;
    y=0;
    p=0;
    s=255;
    pc=npc; 
}

static inline void cpuParse(void)
{
    unsigned char opc=getmem(pc++);
    int cmd=opcodes[opc];
    int addr=modes[opc];
    int c;  
    switch (cmd)
    {
        case adc:
            wval=(unsigned short)a+getaddr(addr)+((p&FLAG_C)?1:0);
            setflags(FLAG_C, wval&0x100);
            a=(unsigned char)wval;
            setflags(FLAG_Z, !a);
            setflags(FLAG_N, a&0x80);
            setflags(FLAG_V, (!!(p&FLAG_C)) ^ (!!(p&FLAG_N)));
            break;
        case _and:
            bval=getaddr(addr);
            a&=bval;
            setflags(FLAG_Z, !a);
            setflags(FLAG_N, a&0x80);
            break;
        case asl:
            wval=getaddr(addr);
            wval<<=1;
            setaddr(addr,(unsigned char)wval);
            setflags(FLAG_Z,!wval);
            setflags(FLAG_N,wval&0x80);
            setflags(FLAG_C,wval&0x100);
            break;
        case bcc:
            branch(!(p&FLAG_C));
            break;
        case bcs:
            branch(p&FLAG_C);
            break;
        case bne:
            branch(!(p&FLAG_Z));
            break;
        case beq:
            branch(p&FLAG_Z);
            break;
        case bpl:
            branch(!(p&FLAG_N));
            break;
        case bmi:
            branch(p&FLAG_N);
            break;
        case bvc:
            branch(!(p&FLAG_V));
            break;
        case bvs:
            branch(p&FLAG_V);
            break;
        case bit:
            bval=getaddr(addr);
            setflags(FLAG_Z,!(a&bval));
            setflags(FLAG_N,bval&0x80);
            setflags(FLAG_V,bval&0x40);
            break;
        case brk:
            pc=0;           /* Just quit the emulation */
            break;
        case clc:
            setflags(FLAG_C,0);
            break;
        case cld:
            setflags(FLAG_D,0);
            break;
        case cli:
            setflags(FLAG_I,0);
            break;
        case clv:
            setflags(FLAG_V,0);
            break;
        case cmp:
            bval=getaddr(addr);
            wval=(unsigned short)a-bval;
            setflags(FLAG_Z,!wval);
            setflags(FLAG_N,wval&0x80);
            setflags(FLAG_C,a>=bval);
            break;
        case cpx:
            bval=getaddr(addr);
            wval=(unsigned short)x-bval;
            setflags(FLAG_Z,!wval);
            setflags(FLAG_N,wval&0x80);      
            setflags(FLAG_C,x>=bval);
            break;
        case cpy:
            bval=getaddr(addr);
            wval=(unsigned short)y-bval;
            setflags(FLAG_Z,!wval);
            setflags(FLAG_N,wval&0x80);      
            setflags(FLAG_C,y>=bval);
            break;
        case dec:
            bval=getaddr(addr);
            bval--;
            setaddr(addr,bval);
            setflags(FLAG_Z,!bval);
            setflags(FLAG_N,bval&0x80);
            break;
        case dex:
            x--;
            setflags(FLAG_Z,!x);
            setflags(FLAG_N,x&0x80);
            break;
        case dey:
            y--;
            setflags(FLAG_Z,!y);
            setflags(FLAG_N,y&0x80);
            break;
        case eor:
            bval=getaddr(addr);
            a^=bval;
            setflags(FLAG_Z,!a);
            setflags(FLAG_N,a&0x80);
            break;
        case inc:
            bval=getaddr(addr);
            bval++;
            setaddr(addr,bval);
            setflags(FLAG_Z,!bval);
            setflags(FLAG_N,bval&0x80);
            break;
        case inx:
            x++;
            setflags(FLAG_Z,!x);
            setflags(FLAG_N,x&0x80);
            break;
        case iny:
            y++;
            setflags(FLAG_Z,!y);
            setflags(FLAG_N,y&0x80);
            break;
        case jmp:
            wval=getmem(pc++);
            wval|=256*getmem(pc++);
            switch (addr)
            {
                case _abs:
                    pc=wval;
                    break;
                case ind:
                    pc=getmem(wval);
                    pc|=256*getmem(wval+1);
                    break;
            }
            break;
        case jsr:
            push((pc+1)>>8);
            push((pc+1));
            wval=getmem(pc++);
            wval|=256*getmem(pc++);
            pc=wval;
            break;
        case lda:
            a=getaddr(addr);
            setflags(FLAG_Z,!a);
            setflags(FLAG_N,a&0x80);
            break;
        case ldx:
            x=getaddr(addr);
            setflags(FLAG_Z,!x);
            setflags(FLAG_N,x&0x80);
            break;
        case ldy:
            y=getaddr(addr);
            setflags(FLAG_Z,!y);
            setflags(FLAG_N,y&0x80);
            break;
        case lsr:      
            bval=getaddr(addr); wval=(unsigned char)bval;
            wval>>=1;
            setaddr(addr,(unsigned char)wval);
            setflags(FLAG_Z,!wval);
            setflags(FLAG_N,wval&0x80);
            setflags(FLAG_C,bval&1);
            break;
        case _nop:
            break;
        case ora:
            bval=getaddr(addr);
            a|=bval;
            setflags(FLAG_Z,!a);
            setflags(FLAG_N,a&0x80);
            break;
        case pha:
            push(a);
            break;
        case php:
            push(p);
            break;
        case pla:
            a=pop();
            setflags(FLAG_Z,!a);
            setflags(FLAG_N,a&0x80);
            break;
        case plp:
            p=pop();
            break;
        case rol:
            bval=getaddr(addr);
            c=!!(p&FLAG_C);
            setflags(FLAG_C,bval&0x80);
            bval<<=1;
            bval|=c;
            setaddr(addr,bval);
            setflags(FLAG_N,bval&0x80);
            setflags(FLAG_Z,!bval);
            break;
        case ror:
            bval=getaddr(addr);
            c=!!(p&FLAG_C);
            setflags(FLAG_C,bval&1);
            bval>>=1;
            bval|=128*c;
            setaddr(addr,bval);
            setflags(FLAG_N,bval&0x80);
            setflags(FLAG_Z,!bval);
            break;
        case rti:
            /* Treat RTI like RTS */
        case rts:
            wval=pop();
            wval|=pop()<<8;
            pc=wval+1;
            break;
        case sbc:      
            bval=getaddr(addr)^0xff;
            wval=(unsigned short)a+bval+((p&FLAG_C)?1:0);
            setflags(FLAG_C, wval&0x100);
            a=(unsigned char)wval;
            setflags(FLAG_Z, !a);
            setflags(FLAG_N, a>127);
            setflags(FLAG_V, (!!(p&FLAG_C)) ^ (!!(p&FLAG_N)));
            break;
        case sec:
            setflags(FLAG_C,1);
            break;
        case sed:
            setflags(FLAG_D,1);
            break;
        case sei:
            setflags(FLAG_I,1);
            break;
        case sta:
            putaddr(addr,a);
            break;
        case stx:
            putaddr(addr,x);
            break;
        case sty:
            putaddr(addr,y);
            break;
        case tax:
            x=a;
            setflags(FLAG_Z, !x);
            setflags(FLAG_N, x&0x80);
            break;
        case tay:
            y=a;
            setflags(FLAG_Z, !y);
            setflags(FLAG_N, y&0x80);
            break;
        case tsx:
            x=s;
            setflags(FLAG_Z, !x);
            setflags(FLAG_N, x&0x80);
            break;
        case txa:
            a=x;
            setflags(FLAG_Z, !a);
            setflags(FLAG_N, a&0x80);
            break;
        case txs:
            s=x;
            break;
        case tya:
            a=y;
            setflags(FLAG_Z, !a);
            setflags(FLAG_N, a&0x80);
            break;  
    }        
}

void cpuJSR(unsigned short npc, unsigned char na) ;
void cpuJSR(unsigned short npc, unsigned char na)
{  
    a=na;
    x=0;
    y=0;
    p=0;
    s=255;
    pc=npc;
    push(0);
    push(0);

    while (pc > 1)
        cpuParse();
 
}

static void c64Init(int nSampleRate)
{        
    synth_init(nSampleRate);
    memset(memory, 0, sizeof(memory));
  
    cpuReset();    
}




static unsigned short LoadSIDFromMemory(void *pSidData, unsigned short *load_addr,
                       unsigned short *init_addr, unsigned short *play_addr, unsigned char *subsongs, unsigned char *startsong, unsigned char *speed, unsigned short size)
{
    unsigned char *pData;
    unsigned char data_file_offset;

    pData = (unsigned char*)pSidData;
    data_file_offset = pData[7];

    *load_addr = pData[8]<<8;
    *load_addr|= pData[9];

    *init_addr = pData[10]<<8;
    *init_addr|= pData[11];

    *play_addr = pData[12]<<8;
    *play_addr|= pData[13];

    *subsongs = pData[0xf]-1;
    *startsong = pData[0x11]-1;

    *load_addr = pData[data_file_offset];
    *load_addr|= pData[data_file_offset+1]<<8;
    
    *speed = pData[0x15];
    
    memset(memory, 0, sizeof(memory));
    memcpy(&memory[*load_addr], &pData[data_file_offset+2], size-(data_file_offset+2));
    
    if (*play_addr == 0)
    {
        cpuJSR(*init_addr, 0);
        *play_addr = (memory[0x0315]<<8)+memory[0x0314];
    }

    return *load_addr;
}


#include "backend/backend.h"
#include "media.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"

/**
 *
 */
static int
be_sidplayer_canhandle(const char *url)
{
  return !strncmp(url, "sidplayer:", strlen("sidplayer:"));
}


/**
 * Play given track.
 *
 * We only expect this to be called from the playqueue system.
 */
static event_t *
be_sidplayer_play(const char *url0, media_pipe_t *mp, 
		  char *errbuf, size_t errlen, int hold, const char *memetype)
{
  media_queue_t *mq = &mp->mp_audio;
  char *url, *p;
  void *fh;
  size_t fsize;
  unsigned short load_addr, init_addr, play_addr;
  unsigned char subSongsMax, subSong, song_speed;
  int sample = 0;
  media_buf_t *mb = NULL;
  event_t *e;
  int nSamplesRendered = 0;
  int nSamplesPerCall = 882;  /* This is PAL SID single speed (44100/50Hz) */
  int nSamplesToRender = 0;
  int registered_play = 0;

  
  url0 += strlen("sidplayer:");

  url = mystrdupa(url0);
  p = strrchr(url, '/');
  if(p == NULL) {
    snprintf(errbuf, errlen, "Invalid filename");
    return NULL;
  }

  *p++= 0;
  subSong = atoi(p) - 1;

  if((fh = fa_open(url, errbuf, errlen)) == NULL)
    return NULL;

  fsize = fa_read(fh, sidfile, sizeof(sidfile));
  fa_close(fh);
  if(fsize < 1) {
    snprintf(errbuf, errlen, "Unable to read file");
    return NULL;
  }

  c64Init(44100);
  LoadSIDFromMemory(sidfile, &load_addr, &init_addr, &play_addr,
		    &subSongsMax, &subSong, &song_speed, fsize);
  sidPoke(24, 15);                /* Turn on full volume */
  cpuJSR(init_addr, subSong);     /* Start the song initialize */

  mp_set_playstatus_by_hold(mp, hold, NULL);
  mp->mp_audio.mq_stream = 0;
  mp_configure(mp, MP_PLAY_CAPS_PAUSE, MP_BUFFER_NONE);
  mp_become_primary(mp);


  while(1) {

    if(mb == NULL) {

      mb = media_buf_alloc_unlocked(mp, sizeof(int16_t) * CHUNK_SIZE * 1);
      mb->mb_data_type = MB_AUDIO;
      mb->mb_channels = 1;
      mb->mb_rate = 44100;

      mb->mb_time = sample * 1000000LL / mb->mb_rate;

      if(!registered_play && mb->mb_time > METADB_AUDIO_PLAY_THRESHOLD) {
	registered_play = 1;
	metadb_register_play(url0, 1, CONTENT_AUDIO);
      }

      sample += CHUNK_SIZE;

      int16_t *samples = mb->mb_data;

      nSamplesRendered = 0;

      while (nSamplesRendered < CHUNK_SIZE)
	{
	  if (nSamplesToRender == 0)
	    {
	      cpuJSR(play_addr, 0);
	      
	      /* Find out if cia timing is used and how many samples
		 have to be calculated for each cpujsr */
	      int nRefreshCIA = (int)(20000*(memory[0xdc04]|(memory[0xdc05]<<8))/0x4c00); 
	      if ((nRefreshCIA==0) || (song_speed == 0)) 
		nRefreshCIA = 20000;
	      nSamplesPerCall = mixing_frequency*nRefreshCIA/1000000;
          
	      nSamplesToRender = nSamplesPerCall;
	    }
	  if (nSamplesRendered + nSamplesToRender > CHUNK_SIZE)
	    {
	      synth_render(samples+nSamplesRendered, CHUNK_SIZE-nSamplesRendered);
	      nSamplesToRender -= CHUNK_SIZE-nSamplesRendered;
	      nSamplesRendered = CHUNK_SIZE;
	    }
	  else
	    {
	      synth_render(samples+nSamplesRendered, nSamplesToRender);
	      nSamplesRendered += nSamplesToRender;
	      nSamplesToRender = 0;
	    } 
	}
      

    }

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {
      mp_flush(mp, 0);
      break;
    } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	      event_is_action(e, ACTION_PLAY) ||
	      event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_set_playstatus_by_hold(mp, hold, NULL);

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_action(e, ACTION_PREV_TRACK) ||
	      event_is_action(e, ACTION_NEXT_TRACK) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp, 0);
      break;
    }
    event_release(e);
  }  

  return e;
}


/**
 *
 */
static backend_t be_sidplayer = {
  .be_canhandle = be_sidplayer_canhandle,
  .be_play_audio = be_sidplayer_play,
};

BE_REGISTER(sidplayer);
