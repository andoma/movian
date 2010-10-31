/**
 * SVT play plugin using xml.svtplay.se API
 *
 * The hardcoded URLs has been extracted from 
 *     http://svtplay.se/mobil/deviceconfiguration.xml
 *
 *  Copyright (C) 2010 Andreas Öman
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

(function(plugin) {

  plugin.settings = plugin.createSettings("SVT Play", "video");

  plugin.settings.createInfo("info",
			     plugin.config.path + "svtplay.png",
			     "Sveriges Television online");

  plugin.settings.createBool("enabled", "Enable SVT Play", false, function(v) {

    plugin.config.enabled = v;
    if(v) {
      plugin.service = showtime.createService("SVT Play",
					      "svtplay:start", "tv",
					      plugin.config.path + "svtplay_square.png");

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

      if(m.@type == 'application/vnd.apple.mpegurl')
	continue;
      if(showtime.canHandle(m.@url)) {
	if(!bestItem || parseFloat(m.@bitrate) > parseFloat(bestItem.@bitrate)) {
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
	page.loading = false;
	num += c;
	if(c == 0 || num > 20)
	  break;
      }
      offset += num;
      return offset < page.entries;
    }

    page.type = "directory";
    paginator();
    page.paginator = paginator;
  }

  function titlePopulator(page, item) {
    page.appendItem("svtplay:video:" + item.svtplay::titleId,
		    "directory", {
		      title: item.title,
		      icon: item.media::thumbnail.@url
		    });
  };


  function videoPopulator(page, item) {

      var metadata = {
	title: item.svtplay::titleName.toString() + " - " + item.title
      };
      
      var best = bestMedia(item);
      
      if(best === undefined)
	return;

      metadata.icon = item.media::thumbnail.@url;
      metadata.description = item.description;

      var duration =  parseInt(best.@duration);

      if(duration > 0)
	metadata.duration = duration;

      page.appendItem(best.@url,
		      "video", metadata);
  };



  plugin.addURI("svtplay:title:([0-9,]*)", function(page, id) {
    page.contents = "items";
    pageController(page, function(offset) {
      return showtime.httpGet("http://xml.svtplay.se/v1/title/list/" + id, {
	start: offset
      });
    }, titlePopulator);
  });



  plugin.addURI("svtplay:video:([0-9,]*)", function(page, id) {
    pageController(page, function(offset) {
      return showtime.httpGet("http://xml.svtplay.se/v1/video/list/" + id, {
	start: offset,
	image: "poster"
      });
    }, videoPopulator);
  });

  plugin.addURI("svtplay:senaste", function(page) {
    pageController(page, function(offset) {
      page.metadata.title = "Senaste program från SVT Play";
      return showtime.httpGet("http://xml.svtplay.se/v1/video/list/96241,96242,96243,96245,96246,96247,96248", {
	start: offset,
	expression: "full",
	image: "poster"
      });
    }, videoPopulator);
  });

    


  plugin.addURI("svtplay:start", function(page) {

    var svtplay = new Namespace("http://xml.svtplay.se/ns/playopml");

    var doc = new XML(showtime.httpGet("http://svtplay.se/mobil/deviceconfiguration.xml"));

    page.appendItem("svtplay:senaste",
		    "directory", {
		      title: "Senaste program"
		    });

    for each (var o in doc.body.outline) {


      if(o.@text == "Kategorier") {
	for each (var k in o.outline) {
	  page.appendItem("svtplay:title:" + k.@svtplay::contentNodeIds,
			  "directory", {
			    title: k.@text,
			    icon: k.@svtplay::thumbnail
			  });
	}
      }
    }
    page.type = "directory";
    page.contents = "items";
    page.loading = false;

    page.metadata.logo = plugin.config.path + "svtplay.png";
    page.metadata.title = "SVT Play";

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
