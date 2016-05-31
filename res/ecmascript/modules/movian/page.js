var prop = require('movian/prop');
var settings = require('movian/settings');

// ---------------------------------------------------------------
// The Item object
// ---------------------------------------------------------------

function Item(page) {
  Object.defineProperties(this, {

    root: {
      value: prop.createRoot()
    },
    page: {
      value: page
    }
  });
  this.eventhandlers = {};
}

Item.prototype.bindVideoMetadata = function(obj) {
  if(this.mlv)
    Core.resourceDestroy(this.mlv);
  this.mlv = require('native/metadata').videoMetadataBind(this.root,
                                                          this.root.url, obj);
}

Item.prototype.unbindVideoMetadata = function(obj) {
  if(this.mlv) {
    Core.resourceDestroy(this.mlv);
    delete this.mlv
  }
}

Item.prototype.toString = function() {
  return '[ITEM with title: ' + this.root.metadata.title.toString() + ']';
}

Item.prototype.dump = function() {
  prop.print(this.root);
}

Item.prototype.enable = function() {
  this.root.enabled = true;
}

Item.prototype.disable = function() {
  this.root.enabled = false;
}

Item.prototype.destroyOption = function(item) {
  prop.destroy(item);
}

Item.prototype.addOptAction = function(title, func, subtype) {
  var node = prop.createRoot();
  node.type = 'action';
  node.metadata.title = title;
  node.enabled = true;
  node.subtype = subtype;

  prop.subscribe(node.eventSink, function(type, val) {
    if(type == "action" && val.indexOf('Activate') != -1)
      func();
  }, {
    autoDestroy: true,
    actionAsArray: true,
  });
  prop.setParent(node, this.root.options);
  return node;
}


Item.prototype.addOptURL = function(title, url, subtype) {
  var node = prop.createRoot();
  node.type = 'location';
  node.metadata.title = title;
  node.enabled = true;
  node.url = url;
  node.subtype = subtype

  prop.setParent(node, this.root.options);
}


Item.prototype.addOptSeparator = function(title) {
  var node = prop.createRoot();
  node.type = 'separator';
  node.metadata.title = title;
  node.enabled = true;

  prop.setParent(node, this.root.options);
}


Item.prototype.destroy = function() {
  var pos = this.page.items.indexOf(this);
  if(pos != -1)
    this.page.items.splice(pos, 1);
  prop.destroy(this.root);
}


Item.prototype.moveBefore = function(before) {
  prop.moveBefore(this.root, before ? before.root : null);
  var thispos = this.page.items.indexOf(this);
  if(before) {
    var beforepos = this.page.items.indexOf(before);
    this.page.items.splice(thispos, 1);
    if(beforepos > thispos)
      beforepos--;
    this.page.items.splice(beforepos, 0, this);
  } else {
    this.page.items.splice(thispos, 1);
    this.page.items.push(this);
  }
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
        if(type != "action")
          return;
        if(val in this.eventhandlers) {
          for(x in this.eventhandlers[val]) {
            this.eventhandlers[val][x](val);
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
  this.items = [];
  this.sync = sync;
  this.root = root;
  this.model = flat ? this.root : this.root.model;
  this.root.entries = 0;
  this.eventhandlers = [];

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
    },

    entries: {
      get: function()  { return root.entries; },
      set: function(v) { root.entries = v; }
    },

    paginator: {
      get: function()  { return this.paginator_; },
      set: function(v) {
        this.paginator_ = v;
        this.haveMore(true);
      }
    }

  });

  if(!flat) {
    this.options = new settings.kvstoreSettings(this.model.options,
                                                this.root.url,
                                                'plugin');
  }

  this.nodesub =
    prop.subscribe(model.nodes, function(op, value, value2) {
      if(op == 'wantmorechilds') {
        var nodes = model.nodes;
        var have_more = false;

        if(typeof this.asyncPaginator == 'function') {
          this.asyncPaginator();
          return;
        }

        if(typeof this.paginator == 'function') {

          try {
            have_more = !!this.paginator();
          } catch(e) {
            if(!prop.isZombie(model)) {
              throw e;
            } else {
              console.log("Page closed during pagination, error supressed");
            }
          }
        }
        prop.haveMore(nodes, have_more);
      }

      if(op == 'reqmove' && typeof this.reorderer == 'function') {
        var item = this.items[this.findItemByProp(value)];
        var before = value2 ? this.items[this.findItemByProp(value2)] : null;
        this.reorderer(item, before);
      }
    }.bind(this), {
      autoDestroy: true
    });
}


Page.prototype.haveMore = function(v) {
  prop.haveMore(this.model.nodes, v);
}

Page.prototype.findItemByProp = function(v) {
  for(var i = 0; i < this.items.length; i++) {
    if(prop.isSame(this.items[i].root, v)) {
      return i;
    }
  }
  return -1;
}

Page.prototype.error = function(msg) {
  this.model.loading = false;
  this.model.type = 'openerror';
  this.model.error = msg.toString();
}

Page.prototype.getItems = function() {
  return this.items.slice(0);
}


Page.prototype.appendItem = function(url, type, metadata) {

  var item = new Item(this);
  this.items.push(item);

  var root = item.root;
  root.url = url;
  root.type = type;
  root.metadata = metadata;

  if(type == 'video') {

    var metabind_url = url;
    if(url.indexOf('videoparams:') == 0) {
      try {
        var x = JSON.parse(url.substring(12));
        if(typeof(x.canonicalUrl) == 'string') {
          metabind_url = x.canonicalUrl;
        } else {
          for(var i = 0; i < x.sources.length; i++) {
            if(typeof(x.sources[i].url) == 'string') {
              metabind_url = x.sources[i].url;
              break;
            }
          }
        }
      } catch(e) {
      }
    }
    require('native/metadata').bindPlayInfo(root, metabind_url);
  }

  prop.setParent(root, this.model.nodes);
  return item;
}

Page.prototype.appendAction = function(title, func, subtype) {
  var item = new Item(this);

  var root = item.root;
  root.type = "action";
  root.metadata.title = title;
  root.enabled = true;
  root.subtype = subtype;

  prop.subscribe(root.eventSink, function(type, val) {
    if(type == "action" && val.indexOf('Activate') != -1)
      func();
  }, {
    autoDestroy: true,
    actionAsArray: true,
  });
  prop.setParent(root, this.model.nodes);

  return item;
}

Page.prototype.appendPassiveItem = function(type, data, metadata) {

  var item = new Item(this);
  this.items.push(item);

  var root = item.root;
  root.type = type;
  root.data = data;
  root.metadata = metadata;
  prop.setParent(root, this.model.nodes);
  return item;
}

Page.prototype.dump = function() {
  prop.print(this.root);
}

Page.prototype.flush = function() {
  prop.deleteChilds(this.model.nodes);
}

Page.prototype.redirect = function(url) {

  Core.resourceDestroy(this.nodesub);

  if(this.sync) {
    require('native/route').backendOpen(this.root, url, true);
  } else {
    prop.sendEvent(this.root.eventSink, "redirect", url);
  }
}

Page.prototype.onEvent = function(type, callback) {
  if(type in this.eventhandlers) {
    this.eventhandlers[type].push(callback);
  } else {
    this.eventhandlers[type] = [callback];
  }

  if(!this.eventSubscription) {
    this.eventSubscription =
      prop.subscribe(this.root.eventSink, function(type, val) {
        if(type != "action")
          return;
        if(val in this.eventhandlers) {
          for(x in this.eventhandlers[val]) {
            this.eventhandlers[val][x](val);
          }
        }

    }.bind(this), {
      autoDestroy: true
    });
  }
}


// ---------------------------------------------------------------
// ---------------------------------------------------------------
// Exported functions
// ---------------------------------------------------------------
// ---------------------------------------------------------------


exports.Route = function(re, callback) {

  this.route = require('native/route').create(re, function(pageprop, sync, args) {

    try {

      // First, convert the raw page prop object into a proxied one
      pageprop = prop.makeProp(pageprop);

      // Prepend a Page object as first argument to callback
      args.unshift(new Page(pageprop, sync, false));

      callback.apply(null, args);
    } catch(e) {

      if(!prop.isZombie(pageprop)) {
        throw e;
      } else {
        console.log("Page at route " + re + " was closed, error supressed");
      }
    }
  });
}

exports.Route.prototype.destroy = function() {
  Core.resourceDestroy(this.route);
}


exports.Searcher = function(title, icon, callback) {

  this.searcher = require('native/hook').register('searcher', function(model, query, loading) {

    try {

      // Convert the raw page prop object into a proxied one
      model = prop.makeProp(model);

      var root = prop.createRoot();

      root.metadata.title = title;
      root.metadata.icon = icon;
      root.type = 'directory';
      prop.setParent(root, model.nodes);

      var page = new Page(root, false, true);
      page.type = 'directory';
      root.url = prop.makeUrl(page.root);
      prop.atomicAdd(loading, 1);
      try {
        callback(page, query);
      } finally {
        prop.atomicAdd(loading, -1);
      }
    } catch(e) {

      if(!prop.isZombie(model)) {
        throw e;
      } else {
        console.log("Search for " + query + " was closed, error supressed");
      }
    }
  });
}



exports.Searcher.prototype.destroy = function() {
  Core.resourceDestroy(this.searcher);
}
