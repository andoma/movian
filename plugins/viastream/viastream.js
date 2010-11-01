/**
 * Channels: TV3, TV6, TV8, and all of the sports channels
 * Based on the tvse script for xot-uzg: http://code.google.com/p/xot-uzg/downloads/list
 */

(function(plugin) {

  plugin.settings = plugin.createSettings("Viasat Play", "video");

  plugin.settings.createInfo("info",
			     plugin.config.path + "viasatimage.jpg",
			     "Viasat Play");

  plugin.settings.createBool("enabled", "Enable Viasat Play", false, function(v) {

    plugin.config.enabled = v;
    if(v) {
      plugin.service = showtime.createService("Viasat Play",
					      "viastream:start", "tv",
					      plugin.config.path + "viasatimage.jpg");

    } else {
      delete plugin.service;
    }
  });

  function extractGeoblockUrl(url) {
      var doc = new XML(showtime.httpGet(url));
      return doc.Url;
  }

  function getVerifiableVideoUrl(url) {
      return url + " swfurl=" + "http://flvplayer.viastream.viasat.tv/flvplayer/play/swf/player100920.swf swfvfy=true";
  }

  function startPage(page) {      
	  /**
	   * Sporting channels: 1se
	   * TV3: 2se
	   * TV6: 3se
	   * TV8: 4se	   
	   * Viasat Sport: sesport (TODO: add this channel too)
	   */
	  page.appendItem("viastream:sitemapdata:2se:0", "directory", {
		  title: "TV3",
		      icon: plugin.config.path + "2selarge.png"
		      });

	  page.appendItem("viastream:sitemapdata:3se:0", "directory", {
		  title: "TV6",
		      icon: plugin.config.path + "3selarge.png"
		      });

	  page.appendItem("viastream:sitemapdata:4se:0", "directory", {
		  title: "TV8",
		      icon: plugin.config.path + "4selarge.png"
		      });
	  
	  page.appendItem("viastream:sitemapdata:1se:0", "directory", {
		  title: "Sport",
		      icon: plugin.config.path + "sesportlarge.png"
		      });
	  

    page.type = "directory";
    page.contents = "items";
    page.loading = false;

    page.metadata.logo = plugin.config.path + "viasatimage.jpg";
    page.metadata.title = "Viasat Play"; 
  }

  plugin.addURI("viastream:products:([0-9,]*)", function(page, productsId) {
	  page.contents = "video";
	  page.type = "directory";

	  var doc = new XML(showtime.httpGet("http://viastream.viasat.tv/Products/Category/" + productsId));

	  for each (var productNode in doc.Product) {
		  //fetch info for each episode - one request per episode!
		  
		  var productDoc = new XML(showtime.httpGet("http://viastream.viasat.tv/Products/" + productNode.ProductId));

		  var metadata = {
		      title: productNode.Title,
		      description: productDoc.Product.LongDescription,
		      icon: productDoc.Product.Images.ImageMedia.Url
		  };
		  
		  var url = productDoc.Product.Videos.Video.Url;
		  
		  var geoBlock = productDoc.Product.Geoblock;

		  if(geoBlock == "true") {
		      showtime.print("Extracting geoblock");
		      url = extractGeoblockUrl(url);
		  }

		  url = getVerifiableVideoUrl(url);

		  page.appendItem(url,"video", metadata); 
	      }
	  page.loading = false;
      });
    
  plugin.addURI("viastream:sitemapdata:([a-z0-9,]*):([0-9,]*)", function(page, channelId, id) {
	  
    var doc = new XML(showtime.httpGet("http://viastream.viasat.tv/siteMapData/se/" + channelId + "/" + id));

    for each (var siteMapNode in doc.siteMapNode) {
	    if(siteMapNode.@children == "true") {
		page.appendItem("viastream:sitemapdata:" + channelId + ":" + siteMapNode.@id,
				"directory", {
				    title: siteMapNode.@title
					})
		    }
	    else {
		//go to show product
		page.appendItem("viastream:products:" + siteMapNode.@id,
				"directory", {
				    title: siteMapNode.@title
					})
				}
	    }
      				
    page.type = "directory";
    page.contents = "items";
    page.loading = false;

    var logoPath = plugin.config.path + channelId + "large.png";
    page.metadata.logo = logoPath
  });



  plugin.addURI("viastream:start", startPage);
})(this);
