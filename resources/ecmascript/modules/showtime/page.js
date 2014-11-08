var prop = require('showtime/prop');
var settings = require('showtime/settings');

// ---------------------------------------------------------------
// The Item object
// ---------------------------------------------------------------

function Item(root) {
  Object.defineProperties(this, {

    root: {
      value: prop.createRoot()
    }
  });
  this.eventhandlers = {};
}

Duktape.fin(Item.prototype, function(x) {
  if(this.mlv)
    Showtime.videoMetadataUnbind(this.mlv);
});

Item.prototype.bindVideoMetadata = function(obj) {
  if(this.mlv)
    Showtime.videoMetadataUnbind(this.mlv);
  this.mlv = Showtime.videoMetadataBind(this.root, this.root.url, obj);
}

Item.prototype.dump = function(obj) {
  prop.print(this.root);
}

Item.prototype.addOptAction = function(title, action) {
  var node = prop.createRoot();
  node.type = 'action';
  node.metadata.title = title;
  node.enabled = true;
  node.action = action;

  prop.setParent(node, this.root.options);
}


Item.prototype.addOptURL = function(title, url) {
  var node = prop.createRoot();
  node.type = 'location';
  node.metadata.title = title;
  node.enabled = true;
  node.url = url;

  prop.setParent(node, this.root.options);
}



Item.prototype.onEvent = function(type, callback) {
  if(type in this.eventhandlers) {
    this.eventhandlers[type].push(callback);
  } else {
    this.eventhandlers[type] = [callback];
  }

  if(!this.eventSubscription) {
    this.eventSubscription =
      prop.subscribe(this.root, function(type, val) {
        if(type != "event")
          return;
        if(val in this.eventhandlers) {
          for(x in this.eventhandlers[val]) {
            this.eventhandlers[val][x](this);
          }
        }

    }.bind(this), {
      autoDestroy: true
    });
  }
}


// ---------------------------------------------------------------
// The Page object
// ---------------------------------------------------------------

function Page(root, sync, flat) {

  this.sync = sync;
  this.root = root;
  this.model = flat ? this.root : this.root.model;
  this.root.entries = 0;

  var model = this.model;

  Object.defineProperties(this, {

    type: {
      get: function()  { return model.type; },
      set: function(v) { model.type = v; }
    },

    metadata: {
      get: function()  { return model.metadata; }
    },

    loading: {
      get: function()  { return model.loading; },
      set: function(v) { model.loading = v; }
    },

    source: {
      get: function()  { return root.source; },
      set: function(v) { root.source = v; }
    }
  });

  if(!flat) {
    this.options = new settings.kvstoreSettings(this.model.options,
                                                this.root.url,
                                                'plugin');
  }

  this.nodesub =
    prop.subscribe(model.nodes, function(op, value) {
      if(op == 'wantmorechilds') {
        var nodes = model.nodes;
        var have_more = typeof this.paginator == 'function' && !!this.paginator();
        Showtime.propHaveMore(nodes, have_more);
      }
    }.bind(this), {
      autoDestroy: true
    });
}


Page.prototype.error = function(msg) {
  this.model.loading = false;
  this.model.type = 'openerror';
  this.model.error = msg;
}


Page.prototype.appendItem = function(url, type, metadata) {
  this.root.entries++;

  var item = new Item();
  var root = item.root;
  root.url = url;
  root.type = type;
  root.metadata = metadata;
  Showtime.propSetParent(root, this.model.nodes);
  return item;
}

Page.prototype.appendPassiveItem = function(type, data, metadata) {
  this.root.entries++;

  var item = new Item();
  var root = item.root;
  root.type = type;
  root.data = data;
  root.metadata = metadata;
  Showtime.propSetParent(root, this.model.nodes);
  return item;
}

Page.prototype.dump = function() {
  Showtime.propPrint(this.root);
}

Page.prototype.flush = function() {
  prop.deleteChilds(this.model.nodes);
}

Page.prototype.redirect = function(url) {

  Showtime.resourceDestroy(this.nodesub);

  if(this.sync) {
    Showtime.backendOpen(this.root, url, true);
  } else {
    prop.sendEvent(this.root.eventSink, "redirect", url);
  }
}


// ---------------------------------------------------------------
// ---------------------------------------------------------------
// Exported functions
// ---------------------------------------------------------------
// ---------------------------------------------------------------


exports.Route = function(re, callback) {

  this.route = Showtime.routeCreate(re, function(pageprop, sync, args) {

     // First, convert the raw page prop object into a proxied one
    pageprop = prop.makeProp(pageprop);

    // Prepend a Page object as first argument to callback
    args.unshift(new Page(pageprop, sync, false));

    callback.apply(null, args);
  });
}

exports.Route.prototype.destroy = function() {
  Showtime.resourceDestroy(this.route);
}


exports.Searcher = function(title, icon, callback) {

  this.searcher = Showtime.hookRegister('searcher', function(model, query) {

    // Convert the raw page prop object into a proxied one
    model = prop.makeProp(model);

    var root = prop.createRoot();

    root.metadata.title = title;
    root.metadata.icon = icon;
    root.type = 'directory';

    prop.setParent(root, model.nodes);

    var page = new Page(root, false, true);
    page.type = 'directory';
    root.url = Showtime.propMakeUrl(page.root);
    callback(page, query);
  });
}



exports.Searcher.prototype.destroy = function() {
  Showtime.resourceDestroy(this.searcher);
}
