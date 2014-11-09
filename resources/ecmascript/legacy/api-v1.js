
var http    = require('showtime/http');
var service = require('showtime/service');
var page    = require('showtime/page');
var settings= require('showtime/settings');
var store   = require('showtime/store');

var cryptodigest = function(algo, str) {
  var hash = Showtime.hashCreate(algo);
  Showtime.hashUpdate(hash, str);
  var digest = Showtime.hashFinalize(hash);
  return Duktape.enc('hex', digest);
}


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


  currentVersionInt: Showtime.currentVersionInt,
  currentVersionString: Showtime.currentVersionString,
  deviceId: Showtime.deviceId,


  httpReq: http.request,

  entityDecode: Showtime.entityDecode,
  queryStringSplit: Showtime.queryStringSplit,
  pathEscape: Showtime.pathEscape,
  paramEscape: Showtime.paramEscape,
  message: Showtime.message,
  textDialog: Showtime.textDialog,
  probe: Showtime.probe,
  notify: Showtime.notify,
  durationToString: Showtime.durationToString,

  print: print,
  trace: console.log,

  sha1digest: function(str) {
    return cryptodigest('sha1', str);
  },

  md5digest: function(str) {
    return cryptodigest('md5', str);
  },

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

  createStore: store.create,

  addURI: function(re, callback) {
    return new page.Route(re, callback);
  },

  addSearcher: function(title, icon, cb) {
    return new page.Searcher(title, icon,cb);
  },

  path: Plugin.path,

  getDescriptor: function() {
    return JSON.parse(Plugin.manifest);
  },

  getAuthCredentials: Showtime.getAuthCredentials,

  createSettings: function(title, icon, description) {
    return new settings.globalSettings(Plugin.id, title, icon,
                                       description);
  },

  cachePut: function(stash, key, obj, maxage) {
    Showtime.cachePut('plugin/' + Plugin.id + '/' + stash,
                      key, JSON.stringify(obj), maxage);
  },

  cacheGet: function(stash, key) {
    var v = Showtime.cacheGet('plugin/' + Plugin.id + '/' + stash, key);
    return v ? JSON.parse(v) : null;
  }

};

var x = Showtime.compile(Plugin.url);
x.call(plugin);
