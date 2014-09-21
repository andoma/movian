
// This is WIP and not working yet

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
