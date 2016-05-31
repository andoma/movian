/**
 * A small demo that shows how to do web based authentication
 * for a plugin.
 *
 *  This does a full user authentication on the TMDB site.
 *
 *  This example uses Showtime's TMDB API key so avoid using that
 *  and get your own instead
 *
 */


var page = require('movian/page');
var http = require('movian/http');

new page.Route("devplug:webtest", function(page) {

  var d = http.request("http://api.themoviedb.org/3/authentication/token/new", {
    args: {
      api_key: 'a0d71cffe2d6693d462af9e4f336bc06'
    }
  });

  // URL we want themoviedb to redirect us to when the auth flow is done
  var trap = 'http://localhost:42000/showtime/done';

  // TMDB will give as a complete URL as a HTTP header
  // Ask it to redirect us to our trap URL when done
  var auth = d.headers['Authentication-Callback'] + '?redirect_to=' + trap;

  // Display web flow
  var o = require('native/popup').webpopup(auth, "Login and auth TMDB", trap);

  if(o.result == 'trapped') {
    // We ended up at our trap URL
    // Args from the HTTP URL are put into o.args

    if('approved' in o.args) {
      // Generate a session id using the request token
      var d = http.request("http://api.themoviedb.org/3/authentication/session/new", {
        args: {
          api_key: 'a0d71cffe2d6693d462af9e4f336bc06',
          request_token: o.args['request_token']
        }
      });

      var js = JSON.parse(d);
      print("Session id: " + js.session_id);
    } else {
      print("User did not approve Showtime :(");
    }
  } else {
    print('Error occured: ' + o.result);
  }
});
