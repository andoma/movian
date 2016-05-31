var fs = require('fs');


var storeproxy = {

  get: function(obj, name) {
    return obj.keys[name];
  },

  set: function(obj, name, value) {
    if(obj.keys[name] === value)
      return;

    obj.keys[name] = value;

    if(obj.timer)
      clearTimeout(obj.timer);

    obj.timer = setTimeout(function() {
      console.log("Saving store to " + obj.filename);
      fs.writeFileSync(obj.filename, JSON.stringify(obj.keys));
      delete obj.timer;
    }, 5000);

  },


  has: function(obj, name) {
    return name in obj.keys;
  }
}

exports.createFromPath = function(path) {
  var obj = {
    filename: path,
    keys: {}
  };


  Duktape.fin(obj, function(obj) {
    if(obj.timer) {
      console.log("Finalized store to " + obj.filename);
      fs.writeFileSync(obj.filename, JSON.stringify(obj.keys));
    }
  });

  try {
    obj.keys = JSON.parse(fs.readFileSync(obj.filename));
  } catch (e) {
  }

  return new Proxy(obj, storeproxy);
}


exports.create = function(name) {
  var path = Core.storagePath + '/store/' + name;
  var fs = require('native/fs');

  fs.mkdirs(fs.dirname(path));
  return exports.createFromPath(path);
}
