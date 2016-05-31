
var htsmsg = require('native/htsmsg');

function getfield(obj, key)
{
  var v = htsmsg.get(obj.msg, key);
  if(v === undefined)
    return undefined;
  if('msg' in v)
    return new Proxy(v, htsmsgHandler);
  return v.value;
}

var htsmsgHandler = {
  get: function(obj, name) {

    if(name == 'toString') {
      return function() {
        return obj.value ? obj.value.toString() : "<XML/>";
      }
    }

    if(name == 'valueOf') {
      return function() {
        return obj.value;
      }
    }

    if(name == 'dump') {
      return function() {
        htsmsg.print(obj.msg);
      }
    }

    if(name == 'toJSON') {
      return undefined;
    }

    if(name == 'filterNodes') {
      return function(filter) {
        var count = htsmsg.length(obj.msg);
        var ret = [];

        for(var i = 0; i < count; i++) {
          if(htsmsg.getName(obj.msg, i) == filter) {
            ret.push(getfield(obj, i));
          }
        }
        return ret;
      }
    }

    if(name == 'length')
      return htsmsg.length(obj.msg);

    return getfield(obj, name);
  },

  enumerate: function(obj) {
    return obj.msg ? htsmsg.enumerate(obj.msg) : [];
  },

  has: function(obj, name) {
    return false;
  }
}


exports.parse = function(str) {
  var x = htsmsg.createFromXML(str);
  return new Proxy({msg: x}, htsmsgHandler);
}

exports.htsmsg = function(x) {
  return new Proxy({msg: x}, htsmsgHandler);
}
