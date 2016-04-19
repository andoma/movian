var string = require('native/string');

function Response(res) {
  this.statusCode = res.statuscode;
  this.encoding = 'utf8';
  this.bytes = res.buffer;
  this.onData = function() {}
  this.onEnd = function() {}

  var resp = this;

  setTimeout(function() {
    resp.onData(string.utf8FromBytes(resp.bytes, resp.encoding));
    resp.onEnd();
  }, 0);
}

Response.prototype.setEncoding = function(enc) {
  this._encoding = enc;
}

Response.prototype.on = function(event, fn) {
  if(event == 'data')
    this.onData = fn;
  if(event == 'end')
    this.onEnd = fn;
}


function Request(url) {
  this.url = url
  this.headers = [];
  this.onResponse = function() {}
  this.onError = function() {}
}


Request.prototype.end = function() {
  var io = require('native/io')

  var ctrl = {};
  ctrl.debug = true;
  var req = this;

  io.httpReq(this.url, ctrl, function(err, res) {
    if(err) {
      req.onError(err);
    } else {
      req.onResponse(new Response(res));
    }
  });
}

Request.prototype.on = function(name, fn) {
  if(name == 'response')
    this.onResponse = fn;
  if(name == 'error')
    this.onError = fn;
}

exports.request = function(opts, callback, https) {
  url = typeof(opts) === 'string' ? opts : require('url').format(opts);
  return new Request(url);
}


exports.get = function(opts, callback, https) {

  var r = exports.request(opts, callback, https);
  r.end();
  return r;
}
