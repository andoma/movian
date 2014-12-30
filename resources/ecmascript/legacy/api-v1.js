var prop    = require('showtime/prop');
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
  basename: Showtime.fs.basename,

  sha1digest: function(str) {
    return cryptodigest('sha1', str);
  },

  md5digest: function(str) {
    return cryptodigest('md5', str);
  },

  RichText: function(x) {
    this.str = x.toString();
  },

  systemIpAddress: function() {
      return Showtime.systemIpAddress();
  },

  getSubtitleLanguages: Showtime.getSubtitleLanguages,

  xmlrpc: function() {
    var a = [];
    for(var i = 2; i < arguments.length; i++)
      a.push(arguments[i]);
    var json = JSON.stringify(a);
    var x = Showtime.xmlrpc(arguments[0], arguments[1], json);
    return require('showtime/xml').htsmsg(x);
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

  addHTTPAuth: Showtime.httpInspectorCreate,

  copyFile: Showtime.fs.copyfile,
  selectView: Showtime.selectView,

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
  },

  config: {},

  properties: prop.global.plugin[Plugin.id],

  addItemHook: function(conf) {
    require('showtime/itemhook').create(conf);
  },

  addSubtitleProvider: function(fn) {
    Showtime.subtitleAddProvider(function(root, query, basescore, autosel) {
      var req = Object.create(query);
      req.addSubtitle = function(url, title, language, format,
                                 source, score) {
        Showtime.subtitleAddItem(root, url, title, language, format, source,
                                 basescore + score, autosel);
      }
      fn(req);
    }, Plugin.id, Plugin.id);
  }

};

var x = Showtime.compile(Plugin.url);
x.call(plugin);
