
var http    = require('showtime/http');
var service = require('showtime/service');
var page    = require('showtime/page');

showtime = {

  print: print,

  JSONDecode: JSON.parse,
  JSONEncode: JSON.stringify,

  httpGet: function(url, args, headers, ctrl) {

    var c = {
      args: args,
      headers: headers
    };

    for(var p in ctrl)
      c[p] = ctrl[p];

    return http.request(url, c);
  },

  httpReq: http.request,

  entityDecode: Showtime.entityDecode,
  queryStringSplit: Showtime.queryStringSplit,
  pathEscape: Showtime.pathEscape,
  paramEscape: Showtime.paramEscape,

  RichText: function(x) {
    this.str = x;
  }
};


// This makes prop.js understand that it should set it as Rich formated prop
showtime.RichText.prototype.toRichString = function(x) {
  return this.str;
}


// -------------------------------------------------------------------------
// Setup the plugin object
// -------------------------------------------------------------------------

var plugin = {

  createService: service.create,

  addURI: function(re, callback) {
    return new page.Route(re, callback);
  },

  addSearcher: function(title, icon, cb) {
    return new page.Searcher(title, icon,cb);
  },

  path: Plugin.path,

  getDescriptor: function() {
    return JSON.parse(Plugin.manifest);
  }

};

var x = Showtime.compile(Plugin.url);

x.call(plugin);
