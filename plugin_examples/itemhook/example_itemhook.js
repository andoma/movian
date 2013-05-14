/**
 *
 */

(function(plugin) {

  plugin.addItemHook({
    title: "Show URL",
    handler: function(obj) {
      showtime.message('The URL is: ' + obj.url, true, false);
    }
  });

  plugin.addItemHook({
    title: "Show track duration",
    itemtype: "audio",
    handler: function(obj) {
      showtime.message('The duration is: ' + obj.metadata.duration, true, false);
    }
  });

})(this);
