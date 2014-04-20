var v = Showtime.load(Plugin.url);

// Setup the showtime object

v.globalobject.showtime = {
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

    var res = Showtime.httpReq(url, c);
    return new HttpResponse(res.buffer, res.responseheaders);
  },

  httpReq: function(url, ctrl) {
    var res = Showtime.httpReq(url, ctrl || {});
    return new HttpResponse(res.buffer, res.responseheaders);
  },

  XML: function(str) {
    return makeHtsmsg(Showtime.htsmsgCreateFromXML(str));
  }


};

// -------------------------------------------------------------------------
// Http response object
// -------------------------------------------------------------------------

function HttpResponse(bytes, headers) {
  this.bytes = bytes;
  this.allheaders = headers;
  this.headers = {};
  this.headers_lc = {};
  this.multiheaders = {};
  this.multiheaders_lc = {};

  for(var i = 0; i < headers.length; i+=2) {
    var key = headers[i];
    var key_lc = key.toLowerCase();
    var val = headers[i+1];

    this.headers[key] = val;

    this.headers_lc[key_lc] = val;

    if(key in this.multiheaders) {
      this.multiheaders[key].push(val);
    } else {
      this.multiheaders[key] = [val];
    }

    if(key_lc in this.multiheaders_lc) {
      this.multiheaders_lc[key_lc].push(val);
    } else {
      this.multiheaders_lc[key_lc] = [val];
    }
  }

  Object.freeze(this.allheaders);
  Object.freeze(this.headers);
  Object.freeze(this.headers_lc);
  Object.freeze(this.multiheaders);
  Object.freeze(this.multiheaders_lc);

  this.contenttype = this.headers_lc['content-type'];
  Object.freeze(this);
}


HttpResponse.prototype.toString = function() {

  // This function should do the equiv. of what http_response_toString()
  // does in js_io.c

  var cs = (/.*charset=(.*)/.exec(this.contenttype) || [])[1];

  if(cs)   // A character set was set in the HTTP header contenttype header
    return Showtime.utf8FromBytes(this.bytes, cs);

  if(Showtime.isUtf8(this.bytes))
    // Data validates as UTF-8, return it
    return this.bytes;

  return Showtime.utf8FromBytes(this.bytes, "latin-1");
}



// -------------------------------------------------------------------------
// Prop object
// -------------------------------------------------------------------------


function PropRef(ptr) {
  this.ptr = ptr;
  Object.freeze(this);
}

Duktape.fin(PropRef.prototype, function(x) {
  Showtime.propRelease(x.ptr);
});


var propHandler = {
  get: function(obj, name) {
    if(name == '_ref') {
      return obj.ptr;
    }
    var v = Showtime.propGet(obj.ptr, name);
    return (typeof v === 'pointer') ? makeProp(v) : v;
  },
  set: function(obj, name, value) {

    if(typeof value == 'object') {

      var x = Showtime.propGet(obj.ptr, name);
      if(typeof x !== 'pointer')
        throw "Assignment to non directory prop";

      x = makeProp(x);
      for(var i in value)
        x[i] = value[i];

    } else {
      Showtime.propSet(obj.ptr, name, value);
    }
  }
}


function makeProp(rawptr) {
  if(typeof rawptr != 'pointer')
    throw "Need pointer"
  return new Proxy(new PropRef(rawptr), propHandler);
}

function makePropRoot() {
  return makeProp(Showtime.propCreate());
}

function printProp(p) {
  Showtime.propPrint(p._ref);
}


// ---------------------------------------------------------------
// Service
// ---------------------------------------------------------------

function Service(id) {
  Object.defineProperties(this, {
    id: {
      value: id,
    },
    enabled: {
      set: function(val) {
        Showtime.serviceEnable(this.id, val);
      },
      get: function() {
        return Showtime.serviceEnable(this.id);
      }
    }
  });
}


Duktape.fin(Service.prototype, function(x) {
  Showtime.resourceRelease(x.id);
});


Service.prototype.destroy = function() {
  Showtime.resourceDestroy(this.id);
}

// ---------------------------------------------------------------
// Route
// ---------------------------------------------------------------

var routes = [];

function Route(id, cb) {
  Object.defineProperties(this, {
    id: {
      value: id,
    },
    cb: {
      value: cb,
    }
  });
}

Duktape.fin(Route.prototype, function(x) {
  Showtime.resourceRelease(x.id);
});


// ---------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------

var hooks = [];

function Hook(id, cb) {
  Object.defineProperties(this, {
    id: {
      value: id,
    },
    cb: {
      value: cb,
    }
  });
}

Duktape.fin(Hook.prototype, function(x) {
  Showtime.resourceRelease(x.id);
});

// ---------------------------------------------------------------
// The Item object
// ---------------------------------------------------------------

function Item(root) {
  Object.defineProperties(this, {

    root: {
      value: makePropRoot()
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
  this.mlv = Showtime.videoMetadataBind(this.root._ref, this.root.url, obj);
}

Item.prototype.dump = function(obj) {
  printProp(this.root);
}


// ---------------------------------------------------------------
// The Page object
// ---------------------------------------------------------------

function Page(root) {
  Object.defineProperties(this, {

    root: {
      value: makeProp(root)
    },

    type: {
      get: function()  { return this.root.model.type; },
      set: function(v) { this.root.model.type = v; }
    },

    metadata: {
      get: function()  { return this.root.model.metadata; }
    },
  });

  Object.seal(this);
}


Page.prototype.appendItem = function(url, type, metadata) {
  var item = new Item();
  var root = item.root;
  root.url = url;
  root.type = type;
  root.metadata = metadata;
  Showtime.propSetParent(root._ref, this.root.model.nodes._ref);
  return item;
}

Page.prototype.dump = function() {
  printProp(this.root);
}


// ---------------------------------------------------------------
// The HTSMSG object (Used to back XML)
// ---------------------------------------------------------------

function HtsmsgRef(msg, value) {
  this.msg = msg;
  this.value = value;
  Object.freeze(this);
}

Duktape.fin(HtsmsgRef.prototype, function(x) {
  Showtime.htsmsgRelease(x.msg);
});

var htsmsgHandler = {
  get: function(obj, name) {
    if(name == 'toString') {
      return function(x) {
        return obj.value;
      }
    }
    var v = Showtime.htsmsgGet(obj.msg, name);
    return (typeof v === 'object') ? makeHtsmsg(v.msg, v.value) : v;
  }
}


function makeHtsmsg(msg, value) {
  return new Proxy(new HtsmsgRef(msg, value), htsmsgHandler);
}

// -------------------------------------------------------------------------
// Setup the plugin object
// -------------------------------------------------------------------------

var plugin = {

  createService: function(title, url, type, enabled, icon) {
    return new Service(Showtime.serviceCreate("plugin:" + Plugin.id, title, url,
                                              type, enabled, icon));
  },

  addURI: function(re, cb) {
    var r = new Route(Showtime.routeCreate(re), cb);
    routes.push(r);
    return r;
  },

  addSubtitleProvider: function(cb) {
    var r = new Route(Showtime.hookCreate('subtitleProvider'), cb);
    hooks.push(r);
    return r;
  },

  path: Plugin.path,
};


function invokator(fun, obj, args)
{
  var t = new Duktape.Thread(function() {
    fun.apply(obj, args);
  });
  return Duktape.Thread.resume(t);
}



function routeInvoke(route, pageptr, args)
{
  args.unshift(new Page(pageptr));

  for(var i = 0; i < routes.length; i++) {
    if(routes[i].id == route) {
      return invokator(routes[i].cb, plugin, args);
    }
  }
}


v.entry.call(plugin);
