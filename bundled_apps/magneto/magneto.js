
(function(plugin) {

  plugin.createService("Magneto", "magneto:start", "tv", true,
                       plugin.path + "magneto.png");

  var oldURL = "https://d33t1sz0lcvlxb.cloudfront.net/live/";
  var newURL = "https://d1fmy8cmgbnuxm.cloudfront.net/";

  var channels = [
    ["SVT1",         newURL + "57050bdd9f2244b18705b05a37fc2dab"],
    ["SVT2",         newURL + "a00f02b775e64b50915b12a403fe24d6"],
    ["TV3",          newURL + "840e81fd327842c7a1538416b418786e"],
    ["TV4",          newURL + "f57c3034561347cfbc940007049de602"],
    ["Discovery",    newURL + "5a013bb9ce53422d9b40debe58f7ab1b"],
    ["MTV",          newURL + "849aed5bbb8d4ba8b2c53a38cff86ac7"],
    ["C More First", newURL + "72709ae1171c402a9cff74368830c458"]
  ];

  function populate_model(page) {
    page.type = "directory";
    page.contents = "items";
    page.loading = false;



    for(var i in channels) {
      var ch = channels[i];

      var url = "videoparams:" + showtime.JSONEncode({
	title: ch[0],
	sources: [{
	  url: 'hls:' + ch[1] + "/p.m3u8"
	}]
      });

      page.appendItem(url, "video", {
	title: ch[0]
      });
    }


    page.appendItem('hls:http://viasatlive3-i.akamaihd.net/hls/live/202771/viasatsportse/master.m3u8', "video", {
      title: "Viasat Sport"
    });

    page.appendItem('hls:http://viasatlive4-i.akamaihd.net/hls/live/202772/viasathockey/master.m3u8', "video", {
      title: "Viasat Hockey"
    });
  }


  function populate_model_macbook(page) {
    page.type = "directory";
    page.contents = "items";
    page.loading = false;

    var url = "hls:http://aether:8080/out/"

    page.appendItem(url + 'svt1/hls.m3u8', "video", {
      title: "SVT1"
    });
    page.appendItem(url + 'svt2/hls.m3u8', "video", {
      title: "SVT2"
    });
    page.appendItem(url + 'tv3/hls.m3u8', "video", {
      title: "TV3"
    });

  }

  plugin.addURI("magneto:channels", function(page) {
    populate_model(page);
  });

  plugin.addURI("magneto:channels2", function(page) {
    page.metadata.glwview = plugin.path + "test.view";
    populate_model(page);
  });

  plugin.addURI("magneto:channels3", function(page) {
    page.metadata.glwview = plugin.path + "grid.view";
    populate_model(page);
  });

  plugin.addURI("magneto:4", function(page) {
    page.metadata.glwview = plugin.path + "activation.view";
    populate_model(page);
  });

  plugin.addURI("magneto:5", function(page) {
    page.metadata.glwview = plugin.path + "zap.view";
    populate_model(page);
  });

  plugin.addURI("magneto:6", function(page) {
    page.metadata.glwview = plugin.path + "zap.view";
    populate_model_macbook(page);
  });

  plugin.addURI("magneto:posters", function(page) {
    page.metadata.glwview = plugin.path + "posters.view";
    page.type = "directory";
    page.contents = "items";
    page.loading = false;
  });

  plugin.addURI("magneto:postersvideo", function(page) {
    page.metadata.glwview = plugin.path + "posters.view";
    page.type = "directory";
    page.contents = "items";
    page.loading = false;
    page.metadata.enablevideo = true;
  });



  plugin.addURI("magneto:start", function(page) {
    page.type = "directory";
    page.contents = "items";
    page.loading = false;
    page.metadata.title = "Slipstream demo app";

    page.appendItem("magneto:6", "directory", {
      title: "Stream zapping (local content)"
    });

    page.appendItem("magneto:posters", "directory", {
      title: "Posters demo"
    });

    page.appendItem("magneto:postersvideo", "directory", {
      title: "Posters demo with video"
    });

    page.appendItem("", "separator", {
      title: "Experimental"
    });

    page.appendItem("magneto:channels", "directory", {
      title: "Normal channel list"
    });

    page.appendItem("magneto:channels2", "directory", {
      title: "Awesome coverflow demo"
    });

    page.appendItem("magneto:channels3", "directory", {
      title: "List zap"
    });

    page.appendItem("magneto:4", "directory", {
      title: "Activation lab"
    });

    page.appendItem("magneto:5", "directory", {
      title: "Zap lab"
    });


  });


})(this);
