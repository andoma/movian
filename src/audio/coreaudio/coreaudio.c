/*
 *  CoreAudio output
 *  Copyright (C) 2009 Mattias Wadman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * NOTES:
 * Buffers delivered from audio decoder is in native endian.
 *
 * Only supports stereo PCM. Multi channel PCM, AC3 and DTS not supported yet.
 *
 * For PCM coreaudio always wants 32 bit float samples.
 */


#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFString.h>
#include <CoreAudio/CoreAudio.h>

#include <libavutil/avutil.h>

#include "showtime.h"
#include "audio/audio_defs.h"


#define CATRACE(level, fmt...) TRACE(level, "CoreAudio", fmt)

#define PROPERRID(id, func, prop, s) \
  CATRACE(TRACE_DEBUG, \
    "id %u %s " #prop " failed (%x '%4.4s')", id, func, s, (char *)&s)

#define PROPERR(func, prop, s) \
  CATRACE(TRACE_DEBUG, \
          "%s " #prop " failed (%x '%4.4s')", func, s, (char *)&s)
          
#define DEVPROP(id, prop, size, var) \
  do { \
    int _s; \
    _s = AudioDeviceGetPropertyInfo(id, 0, FALSE, prop, &size, NULL); \
    if(_s != kAudioHardwareNoError) { \
      PROPERRID(id, "AudioDeviceGetPropertyInfo", #prop, _s); \
      return; \
    } \
    var = malloc(size); \
    _s = AudioDeviceGetProperty(id, 0, FALSE, prop, &size, var); \
    if(_s != kAudioHardwareNoError) { \
      PROPERRID(id, "AudioDeviceGetProperty", #prop, _s); \
      return; \
    } \
  } while(0)

#define DEVPROPLIST(id, prop, count, list) \
  do { \
    DEVPROP(id, prop, count, list); \
    count /= sizeof(list[0]); \
  } while(0)

#define STREAMPROP(id, prop, size, var) \
  do { \
    int _s; \
    _s = AudioStreamGetPropertyInfo(id, 0, prop, &size, NULL); \
    if(_s != kAudioHardwareNoError) { \
      PROPERRID(id, "AudioStreamGetPropertyInfo", #prop, _s); \
      return; \
  } \
  var = malloc(size); \
    _s = AudioStreamGetProperty(id, 0, prop, &size, var); \
    if(_s != kAudioHardwareNoError) { \
      PROPERRID(id, "AudioStreamGetProperty", #prop, _s); \
      return; \
    } \
  } while(0)

#define STREAMPROPLIST(id, prop, count, list) \
  do { \
    STREAMPROP(id, prop, count, list); \
    count /= sizeof(list[0]); \
  } while(0)

#define HWPROP(prop, size, var) \
  do { \
    int _s; \
    _s = AudioHardwareGetPropertyInfo(prop, &size, NULL); \
    if(_s != kAudioHardwareNoError) { \
      PROPERR("AudioHardwareGetPropertyInfo", #prop, _s); \
      return; \
    } \
    var = malloc(size); \
    _s = AudioHardwareGetProperty(prop, &size, var); \
    if(_s != kAudioHardwareNoError) { \
      PROPERR("AudioHardwareGetProperty", #prop, _s); \
      return; \
    } \
  } while(0)

#define HWPROPLIST(prop, count, list) \
  do { \
    HWPROP(prop, count, list); \
    count /= sizeof(list[0]); \
  } while(0)

typedef struct coreaudio_audio_mode {
  audio_mode_t cam_head;

  audio_mode_t *cam_am;
  audio_fifo_t *cam_af;
  
  AudioStreamBasicDescription cam_asbd;
  CFRunLoopSourceRef cam_noop_source;
  AudioDeviceID cam_device_id;
    
  int cam_cur_rate;
  int cam_cur_format;
  
  float cam_master_volume;
  int cam_master_mute;
  prop_sub_t *cam_sub_master_volume;
  prop_sub_t *cam_sub_master_mute;
  int cam_run;
} coreaudio_audio_mode_t;


static hts_mutex_t coreaudio_mutex;

static int coreaudio_open(coreaudio_audio_mode_t *cam);
static void
coreaudio_change_format(coreaudio_audio_mode_t *cam, int format, int rate);


/*
static void
coreaudio_format_dump(AudioStreamBasicDescription *d) {
  CATRACE(TRACE_DEBUG,
          "SR=%f ID=%x (%4.4s) Flags=%x (%s%s%s%s%s%s%s) bpp=%ld "
          "fpp=%ld bpf=%ld cpf=%ld bpc=%ld"
          ,
          d->mSampleRate,
          d->mFormatID,
          (char*)&d->mFormatID,
          d->mFormatFlags,
          d->mFormatFlags & kAudioFormatFlagIsFloat ? "Float," : "",
          d->mFormatFlags & kAudioFormatFlagIsBigEndian ? "BigEndian," : "",
          d->mFormatFlags & kAudioFormatFlagIsSignedInteger ? "SignedInteger," : "",
          d->mFormatFlags & kAudioFormatFlagIsPacked ? "Packed," : "",
          d->mFormatFlags & kAudioFormatFlagIsAlignedHigh ? "AlignedHigh," : "",
          d->mFormatFlags & kAudioFormatFlagIsNonInterleaved ? "NonInterleaved," : "",
          d->mFormatFlags & kAudioFormatFlagIsNonMixable ? "NonMixable," : "",
          d->mBytesPerPacket,
          d->mFramesPerPacket,
          d->mBytesPerFrame,
          d->mChannelsPerFrame,
          d->mBitsPerChannel
          );
}
*/

static OSStatus
audioDeviceIOProc(AudioDeviceID inDevice,
                  const AudioTimeStamp* inNow,
                  const AudioBufferList* inInputData,
                  const AudioTimeStamp* inInputTime,
                  AudioBufferList* outOutputData,
                  const AudioTimeStamp* inOutputTime,
                  void* inClientData)
{
  audio_mode_t *am = inClientData;
  coreaudio_audio_mode_t *cam = inClientData;
  int i, j, k;
  int outframes;
  audio_buf_t *ab;
  float vol = cam->cam_master_volume;
  
  ab = af_deq2(cam->cam_af, 0 /* no wait */, am);
  if(ab == AF_EXIT) {
    cam->cam_run = 0;
    return 0;
  }

  if(ab == NULL) {
    /* outOutputData is zeroed out by default */
    return 0;
  }
  
  if(ab->ab_format != cam->cam_cur_format ||
     ab->ab_samplerate != cam->cam_cur_rate) {
    coreaudio_change_format(cam, ab->ab_format, ab->ab_samplerate);
    cam->cam_cur_format = ab->ab_format;
    cam->cam_cur_rate = ab->ab_samplerate;
  }
    
  if(ab->ab_pts != AV_NOPTS_VALUE) {
    media_pipe_t *mp = ab->ab_mp;
    
    hts_mutex_lock(&mp->mp_clock_mutex);
    /* TODO: inOutputTime->mRateScalar? */
    mp->mp_audio_clock = ab->ab_pts + am->am_audio_delay * 1000;
    mp->mp_audio_clock_realtime = showtime_get_ts();
    mp->mp_audio_clock_epoch = ab->ab_epoch;
    hts_mutex_unlock(&mp->mp_clock_mutex);
  }
  
  if(!cam->cam_master_mute) {
    if(ab->ab_isfloat) {
      const float *flt = (const float *)ab->ab_data;

      for(i = 0; i < outOutputData->mNumberBuffers; i++) {
	Float32 *samples = outOutputData->mBuffers[i].mData;

	outframes = 
	  outOutputData->mBuffers[i].mDataByteSize / 
	  cam->cam_asbd.mBytesPerFrame;

	for(j = 0; j < outframes; j++) {
	  for(k = 0; k < outOutputData->mBuffers[i].mNumberChannels; k++) {
	    *samples++ = flt[j*2+k] * vol;
	  }
	}
      }


    } else {
      const SInt16 *in16 = (const SInt16 *)ab->ab_data;

      for(i = 0; i < outOutputData->mNumberBuffers; i++) {
	Float32 *samples = outOutputData->mBuffers[i].mData;

	outframes = 
	  outOutputData->mBuffers[i].mDataByteSize / 
	  cam->cam_asbd.mBytesPerFrame;

	for(j = 0; j < outframes; j++) {
	  for(k = 0; k < outOutputData->mBuffers[i].mNumberChannels; k++) {
	    *samples++ = ((Float32)in16[j*2+k] / INT16_MAX) * vol;
	  }
	}
      }
    }
  }
  
  ab_free(ab);
  
  return 0;
}

static void
coreaudio_change_format(coreaudio_audio_mode_t *cam, int format, int rate)
{
  OSStatus s;
  UInt32 size;
  AudioStreamBasicDescription asbd;
    
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mSampleRate = rate;
  asbd.mFormatFlags = 
    kAudioFormatFlagIsFloat | 
    kAudioFormatFlagIsPacked |
    kAudioFormatFlagsNativeEndian;
  asbd.mBytesPerPacket = 8;
  asbd.mFramesPerPacket = 1;
  asbd.mBytesPerFrame = 8;
  asbd.mChannelsPerFrame = 2;
  asbd.mBitsPerChannel = 32;
  
  size = sizeof(cam->cam_asbd);
  s = AudioDeviceGetProperty(cam->cam_device_id, 0, false, 
			     kAudioDevicePropertyStreamFormat, 
			     &size, &cam->cam_asbd);
  if(s != kAudioHardwareNoError)
    PROPERR("AudioDeviceGetProperty",
            "kAudioDevicePropertyStreamFormat", s);
   
  s = AudioDeviceSetProperty(cam->cam_device_id, NULL, 0, false,
			     kAudioDevicePropertyStreamFormat, 
			     sizeof(asbd), &asbd);
  if(s != kAudioHardwareNoError)
    PROPERR("AudioDeviceSetProperty",
            "kAudioDevicePropertyStreamFormat", s);
  
  size = sizeof(cam->cam_asbd);
  s = AudioDeviceGetProperty(cam->cam_device_id, 0, false, 
			     kAudioDevicePropertyStreamFormat, 
			     &size, &cam->cam_asbd);
  if(s != kAudioHardwareNoError)
    PROPERR("AudioDeviceGetProperty",
            "kAudioDevicePropertyStreamFormat", s);
}

static void
coreaudio_close(coreaudio_audio_mode_t *cam)
{
  OSStatus s;

  CATRACE(TRACE_DEBUG, "Closing device");
  
  s = AudioDeviceStop(cam->cam_device_id, audioDeviceIOProc);
  if(s != kAudioHardwareNoError) {
    PROPERRID(cam->cam_device_id, "AudioDeviceStop", audioDeviceIOProc, s);
  }

  s = AudioDeviceRemoveIOProc(cam->cam_device_id, audioDeviceIOProc);
  if(s != kAudioHardwareNoError) {
    PROPERRID(cam->cam_device_id, "AudioDeviceRemoveIOProc",
              audioDeviceIOProc, s);
  }
}

static int
coreaudio_open(coreaudio_audio_mode_t *cam)
{
  OSStatus s;
  
  CATRACE(TRACE_DEBUG, "%s: Open device", cam->cam_head.am_title);
  
  s = AudioDeviceAddIOProc(cam->cam_device_id,
                           audioDeviceIOProc,
                           cam);
  if(s != kAudioHardwareNoError) {
    PROPERRID(cam->cam_device_id, "AudioDeviceAddIOProc", audioDeviceIOProc, s);
    return 0;
  }
  
  s = AudioDeviceStart(cam->cam_device_id, audioDeviceIOProc);
  if(s != kAudioHardwareNoError) {
    PROPERRID(cam->cam_device_id, "AudioDeviceStart", audioDeviceIOProc, s);
    return 0;
  }

  return 1;
}

/* dummy, never called */
static void
coreaudio_noop(void *data)
{
}

static void
coreaudio_set_master_volume(void *opaque, float value)
{
  coreaudio_audio_mode_t *cam = opaque;
  
  /* decibel to scalar */
  cam->cam_master_volume = pow(10, (value / 20));
}

static void
coreaudio_set_master_mute(void *opaque, int value)
{
  coreaudio_audio_mode_t *cam = opaque;

  cam->cam_master_mute = value;
}

static int
coreaudio_start(audio_mode_t *am, audio_fifo_t *af)
{
  coreaudio_audio_mode_t *cam = (void *)am; 
  CFRunLoopSourceContext context = {};

  CATRACE(TRACE_DEBUG, "Starting");

  cam->cam_am = am;
  cam->cam_af = af;
  
  /* Subscribe to updates of master volume */
  cam->cam_sub_master_volume = 
  prop_subscribe(PROP_SUB_DIRECT_UPDATE,
                 PROP_TAG_CALLBACK_FLOAT, coreaudio_set_master_volume, cam,
                 PROP_TAG_ROOT, prop_mastervol,
                 PROP_TAG_MUTEX, &coreaudio_mutex,
                 NULL);
  
  /* Subscribe to updates of master volume mute */
  cam->cam_sub_master_mute = 
  prop_subscribe(PROP_SUB_DIRECT_UPDATE,
                 PROP_TAG_CALLBACK_INT, coreaudio_set_master_mute, cam,
                 PROP_TAG_ROOT, prop_mastermute,
                 PROP_TAG_MUTEX, &coreaudio_mutex,
                 NULL);
 
  /* add dummy source that never signals, makes CFRunLoop block instead of
     returning immeditly when nothing is playing */
  context.version = 0;
  context.perform = coreaudio_noop;
  cam->cam_noop_source = CFRunLoopSourceCreate(nil, 0, &context);
  CFRunLoopAddSource(CFRunLoopGetCurrent(), cam->cam_noop_source,
                     kCFRunLoopDefaultMode);

  if(coreaudio_open(cam)) {
    /* TODO: needed when NSApplicationMain? */
    
    cam->cam_run = 1;
    while(cam->cam_run)
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, false);
    
    CATRACE(TRACE_DEBUG, "Stopping");

    coreaudio_close(cam);
  }
  
  CFRunLoopRemoveSource(CFRunLoopGetCurrent(), cam->cam_noop_source,
                        kCFRunLoopDefaultMode);
  CFRelease(cam->cam_noop_source);
  
  hts_mutex_lock(&coreaudio_mutex);
  prop_unsubscribe(cam->cam_sub_master_volume);
  prop_unsubscribe(cam->cam_sub_master_mute);
  hts_mutex_unlock(&coreaudio_mutex);
  
  return 0;
}


static void
coreaudio_probe_device(AudioDeviceID id)
{
  int i, j;
  int formats = 0, rates = 0;
  UInt32 frame_size;
  UInt32 count;
  UInt32 outputs;
  char uid[200], name[200];  
  
  CATRACE(TRACE_DEBUG, "Probing device id %u", id);
  
  /* probe number of output streams */
  AudioBufferList *abl;
  DEVPROP(id, kAudioDevicePropertyStreamConfiguration, count, abl);
  outputs = 0;
  for(i = 0; i < abl->mNumberBuffers; i++)
    outputs += abl->mBuffers[i].mNumberChannels;
  free(abl);

  CFStringRef *cfuid;
  DEVPROP(id, kAudioDevicePropertyDeviceUID, count, cfuid);
  CFStringGetCString(*cfuid, uid, sizeof(uid), kCFStringEncodingUTF8);
  CFRelease(*cfuid);
  free(cfuid);
  
  CFStringRef *cfname;
  DEVPROP(id, kAudioDevicePropertyDeviceNameCFString, count, cfname);
  CFStringGetCString(*cfname, name, sizeof(name), kCFStringEncodingUTF8);
  CFRelease(*cfname);
  free(cfname);
  
  CATRACE(TRACE_DEBUG,
          "%s: UID %s with %u output channels", name, uid, outputs);
  if(outputs == 0) {
    CATRACE(TRACE_DEBUG, "%s: No outputs, skipping", name);
    return;
  }
    
  UInt32 *frame_sizep = &frame_size;
  DEVPROP(id, kAudioDevicePropertyBufferFrameSize, count, frame_sizep);
  frame_size = *frame_sizep;
  free(frame_sizep);
  
  AudioChannelLayout *layout;
  DEVPROP(id, kAudioDevicePropertyPreferredChannelLayout, count, layout);
  CATRACE(TRACE_DEBUG, "%d: tag=%u numdesc=%u",
	  i, layout->mChannelLayoutTag,
	  layout->mNumberChannelDescriptions);
  for(i = 0; i < layout->mNumberChannelDescriptions; i++) {
    CATRACE(TRACE_DEBUG, "desc %u: label=%x flags=%x",
	    i,
	    layout->mChannelDescriptions[i].mChannelLabel,
	    layout->mChannelDescriptions[i].mChannelFlags);
  }
  if(layout->mNumberChannelDescriptions == 2)
    formats |= AM_FORMAT_PCM_STEREO;
  free(layout);
  
  if(!formats) {
    CATRACE(TRACE_DEBUG, "%s: No usable channel configurations",
            name);
    return;
  }
  
  rates = 0;
  AudioStreamID *streamids;
  DEVPROPLIST(id, kAudioDevicePropertyStreams, count, streamids);
  CATRACE(TRACE_DEBUG, "%u streams found for device", count);
  for(i = 0; i < count; i++) {    
    AudioStreamBasicDescription *fmts;
    UInt32 fmtCount;
    
    STREAMPROPLIST(streamids[i],
                   kAudioStreamPropertyPhysicalFormats, fmtCount, fmts);
    CATRACE(TRACE_DEBUG, "Stream %u has %u formats", streamids[i], fmtCount);
    
    for(j = 0; j < fmtCount; j++) {
      if(fmts[j].mFormatID != kAudioFormatLinearPCM) {
        CATRACE(TRACE_DEBUG, "Skipping non-PCM format");
        continue;
      }
      
      rates |= audio_rateflag_from_rate((int)fmts[j].mSampleRate);      
    }
    
    free(fmts);    
  }
  free(streamids);
  if(!(rates & AM_SR_48000)) {
    CATRACE(TRACE_DEBUG, "%s: No 48kHz support, ignoring device",
            name);
    return;
  }
  
  coreaudio_audio_mode_t *cam;
  cam = calloc(1, sizeof(coreaudio_audio_mode_t));
  cam->cam_device_id = id;
  cam->cam_head.am_formats = formats;
  cam->cam_head.am_sample_rates = rates;
  cam->cam_head.am_preferred_size = frame_size;
  cam->cam_head.am_title = strdup(name);
  cam->cam_head.am_id = strdup(uid);
  cam->cam_head.am_entry = coreaudio_start;
  cam->cam_head.am_float = 1;
  audio_mode_register(&cam->cam_head);
}

static void
coreaudio_probe_devices(AudioDeviceID skipid)
{
  UInt32 count;
  int i;
  
  AudioDeviceID *devices;
  HWPROPLIST(kAudioHardwarePropertyDevices, count, devices);
  for(i = 0; i < count; i++) {    
    if(devices[i] == skipid) {
      CATRACE(TRACE_DEBUG, "Skipping default device id %u", skipid);
      continue;
    }

    coreaudio_probe_device(devices[i]);
  }
  
  free(devices);
}

static AudioDeviceID
coreaudio_default_output_device(void)
{
  OSStatus s;
  UInt32 size;
  AudioDeviceID id;
  
  size = sizeof(id);
  s = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice,
			       &size, &id);
  if(s != kAudioHardwareNoError) {
    PROPERR("AudioHardwareGetProperty",
            "kAudioHardwarePropertyDefaultOutputDevice", s);
    return kAudioDeviceUnknown;
  }

  CATRACE(TRACE_DEBUG, "Default output device id %u", id);  
  
  /* id can be kAudioDeviceUnknown here */
  
  return id;
}

void
audio_coreaudio_init(void)
{
  AudioDeviceID id;
  
  hts_mutex_init(&coreaudio_mutex);
  
  /* probe default device first, first added is used as default */
  id = coreaudio_default_output_device();
  if(id != kAudioDeviceUnknown)
    coreaudio_probe_device(id);

  /* probe and skip default device id */
  coreaudio_probe_devices(id);
}
