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

  var PREFIX = "exampleitemhook:";

  plugin.addItemHook({
    title: "Detailed info",
    itemtype: "video",
    handler: function(obj, nav) {
      // Serialize all metadata for the item into the URL
      // Wicked, but works just fine
      nav.openURL(PREFIX + "detailedinfo:" + showtime.JSONEncode(obj));
    }
  });

  plugin.addURI(PREFIX + "detailedinfo:(.*)", function(page, jsonstr) {
    // Deserialize all metadata back into the page's metadata model
    page.metadata.info = showtime.JSONDecode(jsonstr);
    // Take a look!
    page.dump();

    // Load a special view
    page.metadata.glwview = plugin.path + "details.view";
    page.type = 'raw';

    page.loading = false;
  });


})(this);
