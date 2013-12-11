
(function(plugin) {

  plugin.createService("Magneto", "magneto:start", "tv", true,
                       plugin.path + "magneto.png");


  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  // var webgate = "http://dev1.benski.cloud.spotify.net:8080/";

  var webgate = "https://mwebgw.spotify.com/";

  function api(method, obj) {
    return showtime.httpReq(webgate + method, obj);
  }

  function jsonApi(method, obj) {
    return showtime.JSONDecode(api(method, obj))
  }

  var store = plugin.createStore('authinfo', true);

  function auth(authreq) {

    // This function should return 'true' if we handled the request.
    // We should always do that even if something fails because nothing
    // else will be able to auth magneto requests

    if(!("token" in store)) {

      var credentials = plugin.getAuthCredentials("Magneto Login",
	                                          "Need login",
                                                  true);
      var token = api("login", {
        postdata: {
          username: 'ludde_test',
          password: 'test1'
        }
      })
      var v = api("getToken", {
        postdata: {
          login: token
        }
      })
      store.token = v.toString();
    }
    return authreq.setHeader('Authorization',
                             'Oauth oauth_token="' + store.token + '"');
  }

  plugin.addHTTPAuth(webgate +".*", auth);


  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  function load_all_channels(page) {

    var v = jsonApi("epg/channels2", {
      args: {
        f: 'json'
      }, debug: true
    })

    for(var i in v.channels) {
      var ch = v.channels[i];
      var url = ch.playlist_url;
      page.appendItem(url, "video", {
	title: ch.long_name,
        icon: ch.logo ? 'imageset:' + showtime.JSONEncode(ch.logo) : null
      });
    }
  }

  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  function populate_model_macbook(page) {
    page.type = "directory";
    page.contents = "items";
    page.loading = false;

    var url = "hls:http://172.31.254.254:8080/out/"

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

  plugin.addURI("magneto:posters", function(page) {
    page.metadata.glwview = plugin.path + "posters.view";
    page.type = "directory";
    page.contents = "items";
    page.loading = false;
  });

  plugin.addURI("magneto:pwnz", function(page) {
    page.metadata.glwview = plugin.path + "pwnz.view";
    page.type = "directory";
    page.contents = "items";
    load_all_channels(page);
    page.loading = false;
  });

  plugin.addURI("magneto:livechannels", function(page) {
    page.type = "directory";
    page.contents = "items";
    load_all_channels(page);
    page.loading = false;
  });

  plugin.addURI("magneto:mbpchannels", function(page) {
    page.metadata.glwview = plugin.path + "zap.view";
    page.type = "directory";
    page.contents = "items";
    populate_model_macbook(page);
    page.loading = false;
  });

  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  plugin.addURI("magneto:start", function(page) {
    page.type = "directory";
    page.contents = "items";
    page.loading = false;
    page.metadata.title = "Slipstream demo app";

    page.appendItem("magneto:channels", "directory", {
      title: "Online channels"
    });

    page.appendItem("magneto:mbpchannels", "directory", {
      title: "Stream zapping (local content)"
    });

    page.appendItem("magneto:posters", "directory", {
      title: "Posters demo"
    });

    page.appendItem("magneto:postersvideo", "directory", {
      title: "Posters demo with video"
    });

    page.appendItem("magneto:livechannels", "directory", {
      title: "Standard channel list"
    });

    page.appendItem("magneto:pwnz", "directory", {
      title: "#outcast pwnz"
    });

    page.appendItem("", "separator", {
      title: "Experimental"
    });

    page.appendItem("http://172.31.254.254:8080/the.dark.knight.rises.2012.1080p.bluray.dts.x264-publichd.mkv", "video", {
      title: "The Dark Knight Rises"
    });

    page.appendItem("http://172.31.254.254:8080/multibitrate.ts", "video", {
      title: "Multiresolution annexb stream"
    });

    page.appendItem("http://172.31.254.254:8080/cedar/good.mkv", "video", {
      title: "Big Buck Bunny (no weighted pred)"
    });

    page.appendItem("http://172.31.254.254:8080/cedar/bad.mkv", "video", {
      title: "Big Buck Bunny (weighted pred)"
    });

  });


  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  plugin.addURI("magneto:channels", function(page) {
    page.type = "directory";
    page.contents = "items";
    page.metadata.glwview = plugin.path + "zap.view";

    load_all_channels(page);

    page.loading = false;
  });

  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  plugin.addURI("magneto:categories", function(page) {
    page.type = "directory";
    page.contents = "items";

    var v = jsonApi("magneto-categories-view/v1/categories/ipad", {
      args: {
        f: 'json'
      },
      debug: true
    })

    for(var i in v.categories) {
      var c = v.categories[i];

      page.appendItem(c.endpoint, "directory", {
	title: c.title
      });
    }

    page.loading = false;
  });


  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  plugin.addURI("sp://webgate/vbartender/stories(.*)", function(page, arg) {
    page.type = "directory";
    page.contents = "items";

    var v = jsonApi("magneto-categories-view/v1/stories/ipad" + arg, {
      debug: true
    })

    for(var i in v.stories) {
      var s = v.stories[i];
//      showtime.print("----------");
//      showtime.print(showtime.JSONEncode(s));

      var item = s.recommended_item;

      page.appendItem(item.uri, "video", {
	title: item.display_name
      });
    }
    page.loading = false;
  });

  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  plugin.addURI("projectxx:tvchannel:(.*)", function(page, gid) {


    var v = jsonApi("epg/channels2/" + gid, {
      args: {
        f: 'json'
      },
      debug: true
    });

    page.source = v.playlist_url;
    page.type = 'video';
    page.loading = false;
  });

  //****************************************************************************
  //****************************************************************************
  //****************************************************************************

  plugin.addURI("projectxx:epgevent:(.*)", function(page, gid) {

    var v = jsonApi("metadata/event/" + gid, {
      args: {
        f: 'json'
      },
      debug: true
    })

//    showtime.print(showtime.JSONEncode(v, true));


    var url = v.channel.playlist_url + "?start=" + v.start_ms + "&stop=" + v.stop_ms;

    showtime.print(url);
    page.source = url;
    page.type = 'video';
    page.loading = false;
  });


})(this);


/*

 hurl -z tcp://dev1.benski.cloud.spotify.net:4242 hm://metadata/event/d16a1851ce9248f7bafd6bc41270813a?f=json
REPLY UUID: 93b5b6a0-5377-11e3-a4a5-0a15b7cb668e
REQ UUID: 938e74c8-5377-11e3-a10a-d4bed9f3e7fa
Status: 200 None

['{"gid": "d16a1851ce9248f7bafd6bc41270813a","start_ms": 1385075700000,"stop_ms": 1385079300000,"title": "My Bloody Valentine","description": "En storf\xc3\xb6rbrytares son blir m\xc3\xb6rdad.","episode": {"gid": "5ab6c06d4d4b47b9b379b895d75ab512","title": "My Bloody Valentine","number": 12,"season": {"gid": "905783972a3d48afa76f212893f7cd80","number": 4,"series": {"gid": "ac668fbe1cad45d9b150583aa251fc43","title": "The Mentalist","genre": ["CRIME","HORROR_AND_SUSPENSE","SCIFI_AND_SUSPENSE"],"images": {"iconics": [{"url": "http://nomo.tmsimg.com/assets/p186597_i_h13_aa.jpg","width": 480,"height": 270},{"url": "http://nomo.tmsimg.com/assets/p186597_i_v8_aa.jpg","width": 960,"height": 1440}]}},"playable_episodes": 37,"images": {}},"synopsis": "En storf\xc3\xb6rbrytares son blir m\xc3\xb6rdad.","first_aired": {"year": 2012,"month": 1,"day": 19},"thumbnail": [{"url": "http://nomo.tmsimg.com/assets/p186597_i_h6_aa.jpg","width": 720,"height": 540}],"images": {"iconics": [{"url": "http://nomo.tmsimg.com/assets/p186597_i_h13_aa.jpg","width": 480,"height": 270},{"url": "http://nomo.tmsimg.com/assets/p186597_i_v8_aa.jpg","width": 960,"height": 1440}],"tvbanners": [{"url": "http://nomo.tmsimg.com/assets/p186597_b_h6_aa.jpg","width": 720,"height": 540},{"url": "http://nomo.tmsimg.com/assets/p186597_b_v4_aa.jpg","width": 67,"height": 90}],"tvbanner_logos": [{"url": "http://nomo.tmsimg.com/assets/p8738257_blt_h13_aa.jpg","width": 480,"height": 270},{"url": "http://nomo.tmsimg.com/assets/p8738257_blt_v8_aa.jpg","width": 960,"height": 1440}]}},"channel": {"gid": "61c03526f5954b52a6fbadef39871850","short_name": "TV8","long_name": "TV8","logo": [{"url": "http://d1bemnkxyrk8nr.cloudfront.net/180x74/TV8.png","width": 180,"height": 74},{"url": "http://d1bemnkxyrk8nr.cloudfront.net/360x148/TV8.png","width": 360,"height": 148}],"playlist_url": "https://d9mwg731u8hop.cloudfront.net/e283fa519624463688dcba89e53879b8/p.m3u8","live_thumbnail": [{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=90","height": 90},{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=180","height": 180},{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=480","height": 480}],"old_channel_id": "*DEPRECATED*","images": {"keyframes": [{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=90","height": 90},{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=180","height": 180},{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=480","height": 480}],"logotypes": [{"url": "http://d1bemnkxyrk8nr.cloudfront.net/180x74/TV8.png","width": 180,"height": 74},{"url": "http://d1bemnkxyrk8nr.cloudfront.net/360x148/TV8.png","width": 360,"height": 148}]}},"genre": ["CRIME","HORROR_AND_SUSPENSE","SCIFI_AND_SUSPENSE"],"images": {"keyframes": [{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=90&time_ms=1385075880000","height": 90},{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=180&time_ms=1385075880000","height": 180},{"url": "https://d33t1sz0lcvlxb.cloudfront.net/video/thumbnail/live/e283fa519624463688dcba89e53879b8?height=480&time_ms=1385075880000","height": 480}]}}']
andoma@sto-jumphost-a1:~$






*/
