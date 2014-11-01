var prop = require('showtime/prop');

// ---------------------------------------------------------------
// The Item object
// ---------------------------------------------------------------

function Item(root) {
  Object.defineProperties(this, {

    root: {
      value: prop.createRoot()
    }
  });
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
  printProp(this.root);
}

// ---------------------------------------------------------------
// The Page object
// ---------------------------------------------------------------

function Page(root, flat) {

  this.root = root;
  this.model = flat ? this.root : this.root.model;
  this.root.entries = 0;

  Object.defineProperties(this, {

    type: {
      get: function()  { return this.model.type; },
      set: function(v) { this.model.type = v; }
    },

    metadata: {
      get: function()  { return this.model.metadata; }
    },

    loading: {
      get: function()  { return this.model.loading; },
      set: function(v) { this.model.loading = v; }
    },

    source: {
      get: function()  { return this.root.source; },
      set: function(v) { this.root.source = v; }
    }
  });

  prop.subscribe(this.model.nodes, function(op, value) {
    if(op == 'wantmorechilds') {
      var nodes = this.model.nodes;
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

// ---------------------------------------------------------------
// ---------------------------------------------------------------
// Exported functions
// ---------------------------------------------------------------
// ---------------------------------------------------------------


exports.Route = function(re, callback) {

  this.route = Showtime.routeCreate(re, function(pageprop, args) {

     // First, convert the raw page prop object into a proxied one
    pageprop = prop.makeProp(pageprop);

    // Prepend a Page object as first argument to callback
    args.unshift(new Page(pageprop));

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

    var page = new Page(root, true);
    page.type = 'directory';
    root.url = Showtime.propMakeUrl(page.root);
    callback(page, query);
  });
}



exports.Searcher.prototype.destroy = function() {
  Showtime.resourceDestroy(this.searcher);
}
