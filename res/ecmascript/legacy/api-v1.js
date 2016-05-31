var prop    = require('movian/prop');

var cryptodigest = function(algo, str) {
  var crypto = require('native/crypto');
  var hash = crypto.hashCreate(algo);
  crypto.hashUpdate(hash, str);
  var digest = crypto.hashFinalize(hash);
  return Duktape.enc('hex', digest);
}

var misc = require('native/misc');
var string = require('native/string');
var popup = require('native/popup');

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

    return require('movian/http').request(url, c);
  },

  currentVersionInt: Core.currentVersionInt,
  currentVersionString: Core.currentVersionString,
  deviceId: Core.deviceId,

  httpReq: function(url, ctrl, cb) {
    return require('movian/http').request(url, ctrl, cb);
  },

  entityDecode: string.entityDecode,
  queryStringSplit: string.queryStringSplit,
  pathEscape: string.pathEscape,
  paramEscape: string.paramEscape,
  durationToString: string.durationToString,

  message: popup.message,
  textDialog: popup.textDialog,
  notify: popup.notify,

  probe: require('native/io').probe,

  print: print,
  trace: console.log,
  basename: require('native/fs').basename,

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
      return misc.systemIpAddress();
  },

  getSubtitleLanguages: require('native/subtitle').getLanguages,

  xmlrpc: function() {
    var a = [];
    for(var i = 2; i < arguments.length; i++)
      a.push(arguments[i]);
    var json = JSON.stringify(a);
    var x = require('native/io').xmlrpc(arguments[0], arguments[1], json);
    return require('movian/xml').htsmsg(x);
  },

  sleep: function(x) {
      return Core.sleep(x);
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

  createService: function(title, url, type, enabled, icon) {
    return require('movian/service').create(title, url, type, enabled, icon);
  },

  createStore: function(name) {
    return require('movian/store').create(name);
  },

  addURI: function(re, callback) {
    var page = require('movian/page');
    return new page.Route(re, callback);
  },

  addSearcher: function(title, icon, cb) {
    var page = require('movian/page');
    return new page.Searcher(title, icon,cb);
  },

  path: Plugin.path,

  getDescriptor: function() {
    if(this.descriptor === undefined)
      this.descriptor = JSON.parse(Plugin.manifest);

    return this.descriptor;
  },

  getAuthCredentials: popup.getAuthCredentials,

  addHTTPAuth: require('native/io').httpInspectorCreate,

  copyFile: require('native/fs').copyfile,
  selectView: misc.selectView,

  createSettings: function(title, icon, description) {
    var settings = require('movian/settings');
    return new settings.globalSettings(Plugin.id, title, icon, description);
  },

  cachePut: function(stash, key, obj, maxage) {
    misc.cachePut('plugin/' + Plugin.id + '/' + stash,
                      key, JSON.stringify(obj), maxage);
  },

  cacheGet: function(stash, key) {
    var v = misc.cacheGet('plugin/' + Plugin.id + '/' + stash, key);
    return v ? JSON.parse(v) : null;
  },

  config: {},

  properties: prop.global.plugin[Plugin.id],

  addItemHook: function(conf) {
    require('movian/itemhook').create(conf);
  },

  addSubtitleProvider: function(fn) {
    require('native/subtitle').addProvider(function(root, query, basescore, autosel) {
      var req = Object.create(query);
      req.addSubtitle = function(url, title, language, format,
                                 source, score) {
        require('native/subtitle').addItem(root, url, title, language, format, source,
                                 basescore + score, autosel);
      }
      fn(req);
    }, Plugin.id, Plugin.id);
  }

};

// This is the return value
plugin;
