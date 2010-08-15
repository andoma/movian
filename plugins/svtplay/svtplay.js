(function(plugin) {

  plugin.settings = plugin.createSettings("SVT Play", "video");

  plugin.settings.createInfo("info",
			     plugin.config.path + "svtplay.png",
			     "Sveriges Television online");

  plugin.settings.createBool("enabled", "Enable SVT Play", false, function(v) {

    plugin.config.enabled = v;
    if(v) {
      plugin.service = showtime.createService("SVT Play",
					      "svtplay:title:96238", "video");
    } else {
      delete plugin.service;
    }
  });





  var opensearch = new Namespace("http://a9.com/-/spec/opensearch/1.1/");
  var svtplay    = new Namespace("http://xml.svtplay.se/ns/playrss");
  var media      = new Namespace("http://search.yahoo.com/mrss");

  // Loop over an item and return the best (highest bitrate) available media
  function bestMedia(item) {
    var bestItem;
    for each (var m in item.media::group.media::content) {
      if(showtime.canHandle(m.@url)) {
	if(!bestItem || m.@bitrate > bestItem.@bitrate) {
	  bestItem = m;
	}
      }
    }
    return bestItem;
  }



  function pageController(page, loader, populator) {
    var offset = 1;

    function paginator() {

      var num = 0;

      while(1) {
	var doc = new XML(loader(offset + num));
	page.entries = doc.channel.opensearch::totalResults;
	var c = 0;

	for each (var item in doc.channel.item) {
	  c++;
	  populator(page, item);
	}
	num += c;
	if(c == 0 || num > 50)
	  break;
      }
      offset += num;

    }

    page.type = "directory";
    paginator();
    page.loading = false;
    page.paginator = paginator;
  }

  function titlePopulator(page, item) {
    page.appendItem("svtplay:video:" + item.svtplay::titleId,
		    "directory", {
		      title: item.title});
  };



  plugin.addURI("svtplay:title:([0-9]*)", function(page, id) {
    pageController(page, function(offset) {
      return showtime.httpGet("http://xml.svtplay.se/v1/title/list/" + id, {
	start: offset
      });
    }, titlePopulator);
  });



  plugin.addURI("svtplay:video:([0-9]*)", function(page, id) {
    pageController(page, function(offset) {
      return showtime.httpGet("http://xml.svtplay.se/v1/video/list/" + id, {
	start: offset
      });
    }, function(page, item) {

      var metadata = {
	title: item.svtplay::titleName.toString() + " - " + item.title
      };
      
      var best = bestMedia(item);
      
      if(best === undefined)
	return;
      
      metadata.duration = parseInt(best.@duration);
      page.appendItem(best.@url,
		      "video", metadata);
    });
  });
  

  plugin.addSearcher(
    "SVT Play", plugin.config.path + "svtplay_icon.png",
    function(page, query) {
      
      pageController(page, function(offset) {
	return showtime.httpGet("http://xml.svtplay.se/v1/title/search/96238", {
	  start: offset,
	  q: query
	});
      }, titlePopulator);
    });


})(this);
