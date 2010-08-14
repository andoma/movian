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

  function request(path, offset) {
    var v = showtime.httpGet("http://xml.svtplay.se" + path, {
      start: offset
    });
    return v;
  }

  function svtTitle(page, url) {
    var offset = 1;

    function loader() {

      var num = 0;

      while(1) {
	var doc = new XML(request(url, offset + num));
	page.entries = doc.channel.opensearch::totalResults;
	var c = 0;

	for each (var item in doc.channel.item) {
	  c++;

	  var metadata = {
	    title: item.title
	  };

	  page.appendItem("svtplay:video:" + item.svtplay::titleId,
			  "directory", metadata);
	}
	num += c;
	if(c == 0 || num > 50)
	  break;
      }
      offset += num;

    }

    page.type = "directory";
    loader();
    page.loading = false;
    page.paginator = loader;
  }


  function svtVideo(page, url) {
    var offset = 1;

    function bestMedia(item) {
      
      var bestItem;

      for each (var m in item.media::group.media::content) {
	var proto = m.@url.split(':')[0];

	if(proto == 'rtmp' || proto == 'rtmpe' || proto == 'http') {
	  if(m.@bitrate > 1 && (!bestItem || m.@bitrate > bestItem.@bitrate)) {
	    bestItem = m;
	  }
	}
      }
      return bestItem;
    }


    function loader() {
      var doc = new XML(request(url, offset));
      page.entries = doc.channel.opensearch::totalResults;

      for each (var item in doc.channel.item) {
	offset++;

	var metadata = {
	  title: item.svtplay::titleName.toString() + " - " + item.title
	};

	var best = bestMedia(item);

	if(best === undefined)
	  continue;

	metadata.duration = parseInt(best.@duration);
	page.appendItem(best.@url,
			"video", metadata);
      }
    }

    page.type = "directory";
    loader();
    page.loading = false;
    page.paginator = loader;
  }



  plugin.addURI("svtplay:title:([0-9]*)", function(page, id) {
    svtTitle(page, "/v1/title/list/" + id);
  });

  plugin.addURI("svtplay:video:([0-9]*)", function(page, id) {
    svtVideo(page, "/v1/video/list/" + id);
  });


})(this);
