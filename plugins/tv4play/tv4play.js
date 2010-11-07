/**
 */

(function(plugin) {

    var maxBandwidth = 10000; //probably more than tv4 will ever provide with. 1500, 800 and 300 have been spotted.

  plugin.settings = plugin.createSettings("TV4 Play", "video");

  plugin.settings.createInfo("info",
			     plugin.config.path + "tv4play.jpg",
			     "TV4 Play");

  plugin.settings.createBool("enabled", "Enable TV4 Play", false, function(v) {

    plugin.config.enabled = v;
    if(v) {
	var startURL = "http://wwwb.tv4play.se/?view=xml";

      plugin.service = showtime.createService("TV4 Play",
					      "tv4play:categorylist:0:" + startURL + ":Tv4play", "tv",
					      plugin.config.path + "tv4play.jpg");

    } else {
      delete plugin.service;
    }
  });


  var tv4ci = new Namespace("http://www.tv4.se/xml/contentinfo");
  var tv4va = new Namespace("http://www.tv4.se/xml/videoapi");

  function getVerifiableVideoUrl(url) {
      var swfUrl="http://wwwb.tv4play.se/polopoly_fs/1.939636.1281635185\!approot/tv4video.swf?"; //TODO: find out how to get the actual url, in case it changes
      return url + " swfurl=" + swfUrl + " swfvfy=true";
  }

  function getVideoURL(xmlDoc) {
      var metaNodes = xmlDoc.head.meta;
      var baseURL = "";

      for(var i=0; i<metaNodes.length(); i++) {
	  var base = metaNodes[i].@base;
	  
	  if(base != undefined) {
	      baseURL = base;
	      break;
	  }	  
      }

      if(baseURL == "")
	  throw new Error("Could not find base URL");

      var bestClip;

      for each (var v in xmlDoc.body.switch.video) {
	      if(!bestClip) { //first round.
		  bestClip = v;
		  continue;
	      }
		  
	      var vBitrate = parseInt(v.attribute('system-bitrate'));
	      var bestClipBitrate = parseInt(bestClip.attribute('system-bitrate'));
	      
	      if(vBitrate > bestClipBitrate)
		  bestClip = v;
	  }
      
      var videoUri = bestClip.@src;
      
      var slashIndex = videoUri.indexOf("/"); //example uri: mp4:/.....
      var videoUri = videoUri.substr(slashIndex);
      
      return(baseURL + "" + videoUri);      
  }

  plugin.addURI("tv4play:categorylist:([0-9]):(.*):(.*)", function(page, level, url, categoryName) {
	  page.metadata.title = categoryName; 

	  level = parseInt(level);
	  
	  var doc = new XML(showtime.httpGet(url).toString());

	  var nodes = doc.tv4va::category;

	  var currentLevel = 0;
	  while(currentLevel < level) {
	      nodes = nodes.tv4va::subcategories.tv4va::category;
	      currentLevel = parseInt(nodes[0].@level); //only look at first node to decide level
	  }
	  
	  /*
	      showtime.message(e, true, false);
	      page.loading = false;
	      page.type = "openerror";
	      page.error = e;
	  */

	  for each (var categoryNode in nodes) {
		  if(categoryNode.@name != categoryName) 
		      continue;

		  for each (var viewNode in categoryNode.tv4va::views.tv4va::view) {
			  var newURL = viewNode.tv4va::url;
			  if(newURL == undefined) //for example can there be empty views, such as http://wwwb.tv4play.se/1.1045414?view=xml
			      continue;
			  /* //the categorylist is only a repetition of what we've already got - no need to show
			  if(viewNode.@kind == "categorylist") {
			      var uri = "tv4play:categorylist:" + (newURL == url ? level+1 : 0);
			      page.appendItem(uri + ":" + newURL + ":" + viewNode.@title , "directory", {title: viewNode.@title});
			  } else
			  */
			  if(viewNode.@kind == "cliplist") {
			      page.appendItem("tv4play:cliplist:" + newURL, "directory", {title: viewNode.@title});
			  }
		      }		  
		  
		  for each (var subcategoryNode in categoryNode.tv4va::subcategories.tv4va::category) {
			  if(subcategoryNode.tv4va::views.tv4va::view.length() == 1) //the categorylist will always contain a link to another categorylist per show. if there are no cliplist nodes, there will be no clips in the category
			      continue;
			  page.appendItem("tv4play:categorylist:" + (level+1) + ":" + url + ":" + subcategoryNode.@name, "directory", {title: subcategoryNode.@name});
		      }
		

	      }

	  page.type = "directory";
	  page.loading = false;
      });

  plugin.addURI("tv4play:cliplist:(.*)", function(page, clipListURL) {
	  page.type = "directory";

	  var doc = new XML(showtime.httpGet(clipListURL).toString());

	  /*
	  var numberOfHits = doc.@totalNumberOfHits; //TODO: use this to implement pagination
	  var pageNumber = doc.@page; //This is a string like "Page 1 of 5". setting get parameter page=X gives page.
	  var pageSize = doc.@pagesize; //TODO: same as above. setting get parameter pagesize=y sets pagesize. seems not to work when searching.
	  */

	  for each (var contentNode in doc.tv4ci::contentList.tv4ci::content) {
		  clipPopulator(page, contentNode);
	      }

	  page.loading = false;
      });


  function pageController(page, loader, populator) {
      var offset = 1;

      function paginator() {
	  var pageSize = 20; //may be updated by the return value from the request
	  var num = 0;
	  
	  while(1) {
	      var doc = new XML(loader(offset, pageSize).toString());
	      var numberOfHits = parseInt(doc.tv4ci::contentList.attribute('totalNumberOfHits'));
	      page.entries = (isNaN(numberOfHits) ? 0 : numberOfHits);

	      var returnedPageSize = parseInt(doc.tv4ci::contentList.attribute('pagesize'));
	      if(!isNaN(returnedPageSize))
		  pageSize = returnedPageSize;

	      var c = 0;
	      
	      for each (var item in doc.tv4ci::contentList.tv4ci::content) {
		      c++;
		      populator(page, item);
		}
	      page.loading = false;
	      num += c;
	      if(c == 0 || num > pageSize)
		  break;
	  }
	  offset += num;
	  return offset < page.entries;
      }

      page.type = "directory";
      paginator();
      page.paginator = paginator;
  }

  function clipPopulator(page, item) {
      		  var metadata = { title: item.tv4ci::title,
				   description: item.tv4ci::text,
				   icon: item.tv4ci::w219imageUrl};
		  page.appendItem("tv4play:clip:" + item.tv4ci::vmanProgramId, "video", metadata);
  }

  /**
   * show clip 
   */
  plugin.addURI("tv4play:clip:([0-9]*)", function(page, clipId) {
	  var smilUrl="http://anytime.tv4.se/webtv/metafileFlash.smil?p=" + clipId + "&bw=" + maxBandwidth + "&emulate=true&sl=true";
	  var content = showtime.httpGet(smilUrl).toString();
	  /*	  
	  var content = '<?xml version="1.0" encoding="iso-8859-1"?>' + "\n" + content;
	  showtime.print(smilUrl);
	  showtime.print(content);
	  */
	  var doc = new XML(content);
	  
	  var videoURL = getVerifiableVideoUrl(getVideoURL(doc));      

	  showtime.trace("playing video at " + videoURL);
	  page.loading = false;
	  page.url = videoURL;
	  page.type = "video";
	  
      });



  
  //TODO: add search function
  plugin.addSearcher(
		     "TV4 Play", plugin.config.path + "tv4play.jpg", 
		     function(page, query) {
			 
			 pageController(page, function(offset, pageSize) {				 
				 var pageNum = Math.floor(offset/pageSize); 
				 return showtime.httpGet("http://wwwb.tv4play.se/1.1379876/sok_pa_tv4_play?view=xml&sortorder=publishedDate&popup=true&type=VIDEOA", {
					 page: pageNum,
					     q: query,
					     pagesize: pageSize
					     }).toString();
			     }, clipPopulator);
		     });
  
})(this);
