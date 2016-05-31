var hook = require('native/hook');
var P = require('movian/prop');

exports.VideoScrobbler = function() {

  this.paused = false;
  this.hook = hook.register('videoscrobble', function(event, data, prop) {
    prop = P.makeProp(prop);

    this.paused = prop.playstatus == 'pause';

    switch(event) {
    case 'start':
      if(typeof(this.onstart) === 'function')
        this.onstart(data, prop);

      P.subscribeValue(prop.playstatus, function(val) {
        var paused = val === 'pause';
        if(paused === this.paused)
          return;

        this.paused = paused;
        if(paused) {
          if(typeof(this.onpause) === 'function')
            this.onpause(data, prop);
        } else {
          if(typeof(this.onresume) === 'function')
            this.onresume(data, prop);
        }
      }.bind(this), {
        autoDestroy: true
      });
      break;

    case 'stop':
      if(typeof(this.onstop) === 'function')
        this.onstop(data, prop);
      break;
    }

  }.bind(this));
}


exports.VideoScrobbler.prototype.destroy = function() {
  Core.resourceDestroy(this.hook);
}


