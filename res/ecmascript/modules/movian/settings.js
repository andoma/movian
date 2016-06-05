var prop  = require('movian/prop');
var store = require('movian/store');


function createSetting(group, type, id, title) {

  var model = group.nodes[id];

  model.type = type;
  model.enabled = true;
  model.metadata.title = title;


  var item = {};

  Object.defineProperties(item, {
    model: {
      value: model
    },

    value: {
      get: function() {
        return model.value;
      },

      set: function(v) {
        model.value = v;
      }
    },

    enabled: {
      set: function(val) {
        model.enabled = val;
      },
      get: function() {
        return parseInt(model.enabled) ? true : false;
      }
    }
  });

  return item;
}



var sp = {};

/// -----------------------------------------------
/// Destroy group
/// -----------------------------------------------

sp.destroy = function() {
  this.zombie = 1;
  if(this.id)
    delete prop.global.settings.apps.nodes[this.id];
}

/// -----------------------------------------------
/// Dump contents of group
/// -----------------------------------------------

sp.dump = function() {
  prop.print(this.nodes);
}


/// -----------------------------------------------
/// Bool
/// -----------------------------------------------

sp.createBool = function(id, title, def, callback, persistent) {
  var group = this;
  var item = createSetting(group, 'bool', id, title);

  var initial = group.getvalue(id, def, 'bool', persistent);
  item.model.value = initial;

  prop.subscribeValue(item.model.value, function(newval) {
    if(group.zombie)
      return;

    group.setvalue(id, newval, persistent);
    callback(newval);
  }, {
    noInitialUpdate: true,
    ignoreVoid: true,
    autoDestroy: true
  });
  callback(initial);
  return item;
}

/// -----------------------------------------------
/// String
/// -----------------------------------------------

sp.createString = function(id, title, def, callback, persistent) {
  var group = this;
  var item = createSetting(group, 'string', id, title);


  var initial = group.getvalue(id, def, 'string', persistent);
  item.model.value = initial;

  prop.subscribeValue(item.model.value, function(newval) {
    if(group.zombie)
      return;

    group.setvalue(id, newval, persistent);
    callback(newval);
  }, {
    noInitialUpdate: true,
    ignoreVoid: true,
    autoDestroy: true
  });

  callback(initial);
  return item;
}

/// -----------------------------------------------
/// Integer
/// -----------------------------------------------

sp.createInt = function(id, title, def, min, max, step, unit,
                        callback, persistent) {
  var group = this;

  var item = createSetting(group, 'integer', id, title);

  var initial = group.getvalue(id, def, 'int', persistent);
  item.model.value = initial;

  prop.setClipRange(item.model.value, min, max);

  item.model.min  = min;
  item.model.max  = max;
  item.model.step = step;
  item.model.unit = unit;

  prop.subscribeValue(item.model.value, function(newval) {
    if(group.zombie)
      return;

    newval = parseInt(newval);
    group.setvalue(id, newval, persistent);
    callback(newval);
  }, {
    noInitialUpdate: true,
    ignoreVoid: true,
    autoDestroy: true
  });

  callback(initial);
  return item;
}


/// -----------------------------------------------
/// Divider
/// -----------------------------------------------

sp.createDivider = function(title) {
  var group = this;
  var node = prop.createRoot();
  node.type = 'separator';
  node.metadata.title = title;
  prop.setParent(node, group.nodes);
}


/// -----------------------------------------------
/// Info
/// -----------------------------------------------

sp.createInfo = function(id, icon, description) {
  var group = this;
  var node = prop.createRoot();
  node.type = 'info';
  node.description = description;
  node.image = icon;
  prop.setParent(node, group.nodes);
}


/// -----------------------------------------------
/// Action
/// -----------------------------------------------

sp.createAction = function(id, title, callback) {
  var group = this;

  var item = createSetting(group, 'action', id, title);

  prop.subscribe(item.model.eventSink, function(type) {
    if(type == 'action')
      callback();
  }, {
    autoDestroy: true
  });

  return item;
}

/// -----------------------------------------------
/// Multiopt
/// -----------------------------------------------

sp.createMultiOpt = function(id, title, options, callback, persistent) {
  var group = this;

  var model = group.nodes[id];
  model.type = 'multiopt';
  model.enabled = true;
  model.metadata.title = title;

  var initial = group.getvalue(id, null, 'string', persistent);

  for(var i in options) {
    var o = options[i];

    var opt_id = o[0].toString();
    var opt_title = o[1];
    var opt_default = o[2];

    var opt = model.options[opt_id];

    opt.title = opt_title;

    if(initial == null && opt_default)
      initial = opt_id;
  }

  if(!initial)
    initial = options[0][0].toString();

  if(initial) {
    var opt = model.options[initial];
    prop.select(opt);
    prop.link(opt, model.current);
    model.value = initial;
    callback(initial);
  }

  prop.subscribe(model.options, function(type, a) {
    if(type == 'selectchild') {
      var selected = prop.getName(a);
      group.setvalue(id, selected, persistent);
      prop.link(a, model.current);
      model.value = selected;
      callback(selected);
    }
  }, {
    noInitialUpdate: true,
    autoDestroy: true
  });


}


/// ---------------------------------------------------------------
/// Store settings using store.js and add to global settings tree
/// ---------------------------------------------------------------

exports.globalSettings = function(id, title, icon, desc) {

  this.__proto__ = sp;

  var basepath = Core.storagePath + '/settings';

  require('native/fs').mkdirs(basepath);

  this.id = id;

  var model = prop.global.settings.apps.nodes[id];

  prop.unloadDestroy(model);

  var metadata = model.metadata;

  model.type = 'settings';
  model.url = prop.makeUrl(model);
  this.nodes = model.nodes;

  metadata.title = title;
  metadata.icon = icon;
  metadata.shortdesc = desc;

  var mystore = store.createFromPath(basepath + '/' + id);

  this.getvalue = function(id, def) {
    return id in mystore ? mystore[id] : def;
  };

  this.setvalue = function(id, value) {
    mystore[id] = value;
  };

  this.properties = model;
}



/// -----------------------------------------------
/// Store settings in the kvstore (key'ed on an URL)
/// -----------------------------------------------

exports.kvstoreSettings = function(nodes, url, domain) {

  this.__proto__ = sp;

  this.nodes = nodes;

  var kvstore = require('native/kvstore');

  this.getvalue = function(id, def, type, persistent) {
    if(!persistent)
      return def;

    if(type == 'int')
      return kvstore.getInteger(url, domain, id, def);
    else if(type == 'bool')
      return kvstore.getBoolean(url, domain, id, def);
    else
      return kvstore.getString(url, domain, id) || def;
  };

  this.setvalue = function(id, value, persistent) {
    if(persistent)
      kvstore.set(url, domain, id, value);
  };
}



