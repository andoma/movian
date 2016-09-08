
/*

 This is not a full plugin, you can test it with

   build.linux/movian -d --ecmascript plugin_examples/videoscrobbling/videoscrobbling_example.js

 ---------------------

 Example of using the videoscrobbler API

 A scrobbler have four callbacks, as seen below.

 The 'data' is an object with various metadata for the sesion:

  id - Unique ID for a single playback session. Should be used to
       tie events to each other.

  canonical_url - Movian's canonical URL.

  title - Title as figured out by the metadata system
  season, episode - If set it's a TV series episode
  year - Set if we think it's a movie

  framerate - Video fraemrate of primary video track
  width/height - Video dimensions of primary video track

  duration - Length of video in seconds. Not set if live



 resumeposition - Set to position where user resumed watching (seconds)

 stopposition - Only set during onstop() and will also be cleared if user
                finished watching the film. (seconds)


 In addition to this the full media pipe property tree is available as
 the second argument to the callbacks.

 This can be used to query current position, etc during pause/resume

 Also the origin item (the item that the video was started from, on the
 "previous page" so to say) is also available as a third argument


*/

var videoscrobbler = require('movian/videoscrobbler');

var vs = new videoscrobbler.VideoScrobbler();

vs.onstart = function(data, prop, origin) {
  print("Started playback session: " + data.id + " at position " + prop.currenttime + " (seconds)");
  print(JSON.stringify(data, null, 4));
  print("The filename is: " + origin.filename);
  require('movian/prop').print(origin);
}

vs.onstop = function(data, prop, origin) {
  print("Stopped playback session: " + data.id + " at position " + prop.currenttime + " (seconds)");
  print(JSON.stringify(data, null, 4));
}

vs.onpause = function(data, prop, origin) {
  print("Paused playback session: " + data.id + " at position " + prop.currenttime + " (seconds)");
  print(JSON.stringify(data, null, 4));
}

vs.onresume = function(data, prop, origin) {
  print("Resumed playback session: " + data.id + " at position " + prop.currenttime + " (seconds)");
  print(JSON.stringify(data, null, 4));
}
