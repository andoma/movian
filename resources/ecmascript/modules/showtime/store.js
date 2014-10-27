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
      fs.writeFileSync(obj.filename, JSON.stringify(obj.keys));
      delete obj.timer;
    }, 1000);

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


  try {
    obj.keys = JSON.parse(fs.readFileSync(obj.filename));
  } catch (e) {
  }

  return new Proxy(obj, storeproxy);
}


exports.create = function(name) {
  Showtime.fs.mkdirs('store');
  return exports.createFromPath('store/' + name);
}
