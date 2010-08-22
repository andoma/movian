(function(plugin) {
  var PREFIX = "headweb:"

  plugin.settings = plugin.createSettings("Headweb", "video");

  plugin.settings.createInfo("info",
		      plugin.config.path + "headweb_logo.png",
		      "Headweb is a Swedish online video store.\n"+
		      "For more information, visit http://www.headweb.se\n\n"+
		      "The Showtime implemetation is still very much beta.\n");

  plugin.settings.createBool("enabled", "Enable headweb", false, function(v) {

    plugin.config.enabled = v;
    if(v) {
      plugin.service = showtime.createService("Headweb genres",
					      PREFIX + "genres", "video");
    } else {
      delete plugin.service;
    }
  });

  /**
     * Headweb API KEY.
     * Please don't steal Showtime's key..
     * Send an email to api@headweb.com and you'll get your own for free,
     * documentation can be found at http://opensource.headweb.com/api
     */
  var APIKEY = "2d6461dd322b4b84b5bac8c654ee6195";

  function request(path, offset, limit) {
    var v = showtime.httpGet("http://api.headweb.com/v4" + path, {
      apikey: APIKEY,
      offset: offset,
      limit: limit
    });
    return v;
  }

  function requestContents(page, url) {
    var offset = 0;

    function findStream(content) {
      return content.stream;
    }

    function bestCover(content) {
      var best = null;
      var bestArea = 0;
      for each (var c in content.cover) {
	var a = c.@width * c.@height;
	if(a > bestArea) {
	  best = c;
	  bestArea = a;
	}
      }
      return best;
    }
    

    function loader() {
      var doc = new XML(request(url, offset, 50));
      page.entries = doc.list.@items;

      for each (var c in doc.list.content) {
	offset++;
	var stream = findStream(c);

	var metadata = {
	  title: c.name,
	  icon: bestCover(c),
	  description: new showtime.RichText(c.plot),
	  rating: parseFloat(c.rating) / 5.0
	};

	var runtime = parseInt(stream.runtime);
	if(runtime > 0) 
	  metadata.runtime = runtime;
	
	page.appendItem(PREFIX + "stream:" + stream.@id,
			"video", metadata);
      }
    }

    page.type = "directory";
    loader();
    page.loading = false;
    page.paginator = loader;
  }

  // List all genres
  plugin.addURI(PREFIX + "genres", function(page) {
    page.title = "Genres";
    page.type = "directory";

    var doc = new XML(request("/genre/filter(-adult,stream)"));

    for each (var genre in doc.list.genre) {
      page.appendItem(PREFIX + "genre:" + genre.@id + ":" + genre,
		      "directory", {
			title:genre
		      });
    }
    page.loading = false;
  });


  // Browse a genre
  plugin.addURI(PREFIX + "genre:([0-9]*):(.*)", function(page, id, name) {
    page.title = name;
    requestContents(page, "/genre/" + id);
  });


  // Play a stream
  plugin.addURI(PREFIX + "stream:([0-9]*)", function(page, id) {

    var doc = new XML(request("/stream/" + id));
    var params = showtime.queryStringSplit(doc.auth.playerparams);

    page.loading = false;
    page.url = params["cfg.stream.auth.url"] + "/" +
      params["cfg.stream.auth.streamid"];
    page.type = "video";
  });

  // Search hook
  plugin.addSearcher(
    "Headweb movies", plugin.config.path + "headweb_icon.png",
    function(page, query) {
      requestContents(page, "/search/" + 
		      showtime.httpEscape(query));
    });

})(this);
