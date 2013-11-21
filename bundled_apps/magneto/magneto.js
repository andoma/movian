
(function(plugin) {

  plugin.createService("Magneto", "magneto:start", "tv", true,
                       plugin.path + "magneto.png");


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


//  var webgate = "http://dev1.benski.cloud.spotify.net:8080/";

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



  plugin.addURI("magneto:channels", function(page) {
    page.type = "directory";
    page.contents = "items";
    page.loading = false;
    page.metadata.glwview = plugin.path + "zap.view";

    var v = jsonApi("epg/channels2", {
      args: {
        f: 'json'
      }
    })

    for(var i in v.channels) {
      var ch = v.channels[i];
      var url = ch.playlist_url;
      page.appendItem(url, "video", {
	title: ch.long_name,
        icon: 'imageset:' + showtime.JSONEncode(ch.logo)
      });
    }
  });

})(this);
