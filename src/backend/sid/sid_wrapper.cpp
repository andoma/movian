#include <stdio.h>
#include <string.h>

#include <sidplay/sidplay2.h>
#include <sidplay/builders/resid.h>


class sidwrap {
public:
  sidwrap() : st(0) {};
  SidTune st;
  sidplay2 p;

  sid2_config_t conf;

};



extern "C" {



  void *sidcxx_load(const void *data, int length, int subsong, char *errbuf, 
		    size_t errlen)
  {
    struct sidwrap *sw = new sidwrap();

    memset(&sw->conf, 0, sizeof(sid2_config_t));

    if(!sw->st.read((const uint_least8_t *)data, length)) {
      delete sw;
      snprintf(errbuf, errlen, "Unable to read data");
      return NULL;
    }

    sw->p.debug(true, stderr);

    sw->st.selectSong(subsong);
    if(sw->p.load(&sw->st)) {
      snprintf(errbuf, errlen, "Unable to load");
      return NULL;
    }



    ReSIDBuilder *rs = new ReSIDBuilder("ReSID");
    if(!rs || !*rs) {
      snprintf(errbuf, errlen, "Unable to create SID emulator");
      return NULL;
    }
    rs->create(sw->p.info().maxsids);
    if(!*rs) {
      snprintf(errbuf, errlen, "Unable to config SID emulator");
      return NULL;
    }
    rs->filter(false);
    if(!*rs) {
      snprintf(errbuf, errlen, "Unable to config SID emulator (filter)");
      return NULL;
    }
    rs->sampling(44100);
    if(!*rs) {
      snprintf(errbuf, errlen, "Unable to config SID emulator (samplerate)");
      return NULL;
    }

    sw->conf = sw->p.config();
    sw->conf.frequency    = 44100;
    sw->conf.precision    = 16;
    sw->conf.playback     = sid2_mono;
    sw->conf.sampleFormat = SID2_LITTLE_SIGNED;

    /* These should be configurable ... */
    sw->conf.clockSpeed    = SID2_CLOCK_CORRECT;
    sw->conf.clockForced   = true;
    sw->conf.sidModel      = SID2_MODEL_CORRECT;
    sw->conf.optimisation  = SID2_DEFAULT_OPTIMISATION;
    sw->conf.sidSamples    = true;
    sw->conf.clockDefault  = SID2_CLOCK_PAL;
    sw->conf.sidDefault    = SID2_MOS6581;

    sw->conf.sidEmulation = rs;

    int r = sw->p.config(sw->conf);
    if(r) {
      snprintf(errbuf, errlen, "Unable to config SID player");
      return NULL;
    }
    return sw;
  }


  int sidcxx_play(void *W, void *out, int len)
  {
    struct sidwrap *sw = (struct sidwrap *)W;

    return sw->p.play(out, len);
  }


  void sidcxx_stop(void *W)
  {
    struct sidwrap *sw = (struct sidwrap *)W;
    delete sw;
  }

}
