
function getfield(obj, key)
{
  var v = Showtime.htsmsgGet(obj.msg, key);
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
        Showtime.htsmsgPrint(obj.msg);
      }
    }

    if(name == 'toJSON') {
      return undefined;
    }

    if(name == 'filterNodes') {
      return function(filter) {
        var count = Showtime.htsmsgLength(obj.msg);
        var ret = [];

        for(var i = 0; i < count; i++) {
          if(Showtime.htsmsgGetName(obj.msg, i) == filter) {
            ret.push(getfield(obj, i));
          }
        }
        return ret;
      }
    }

    if(name == 'length')
      return Showtime.htsmsgLength(obj.msg);

    return getfield(obj, name);
  },

  enumerate: function(obj) {
    return obj.msg ? Showtime.htsmsgEnumerate(obj.msg) : [];
  },

  has: function(obj, name) {
    return false;
  }
}


exports.parse = function(str) {
  var x = Showtime.htsmsgCreateFromXML(str);
  return new Proxy({msg: x}, htsmsgHandler);
}
