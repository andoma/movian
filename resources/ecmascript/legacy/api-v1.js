
var http    = require('showtime/http');
var service = require('showtime/service');
var page    = require('showtime/page');
var settings= require('showtime/settings');
var store   = require('showtime/store');

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
  message: Showtime.message,

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
    var x = new settings.globalSettings(Plugin.id, title, icon, description);

    var o = {
      destroy: x.destroy,

      createBool: function(id, title, def, func, persistent) {
        return x.createBool({
          id: id,
          title: title,
          def: def,
          callback: func,
          persistent: persistent
        });
      },

      createString: function(id, title, def, func, persistent) {
        return x.createString({
          id: id,
          title: title,
          def: def,
          callback: func,
          persistent: persistent
        });
      },

      createInt: function(id, title, def, min, max, step, unit,
                          func, persistent) {
        return x.createInteger({
          id: id,
          title: title,
          def: def,
          min: min,
          max: max,
          step: step,
          unit: unit,
          callback: func,
          persistent: persistent
        });
      },

      createAction: function(id, title, func) {
        return x.createAction({
          id: id,
          title: title,
          callback: func
        })
      },

      createDivider: function(title) {
        return x.createDivider({
          title: title
        })
      }
    }

    return o;
  }

};

var x = Showtime.compile(Plugin.url);

x.call(plugin);
