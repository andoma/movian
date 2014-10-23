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

exports.create = function(name) {
  var obj = {
    filename: 'store/' + name,
    keys: {}
  };

  Showtime.fs.mkdirs('store');

  try {
    obj.keys = JSON.parse(fs.readFileSync(obj.filename));
  } catch (e) {
  }

  return new Proxy(obj, storeproxy);
}
