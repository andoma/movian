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



/**
 *
 */
exports.request = function(url, ctrl, callback) {

  // If ctrl.args is an array we assume it's an array of objects
  // so we merge all those objects into one

  if(ctrl && ctrl.args instanceof Array) {
    var a0 = {}; // The object which contains all merged headers
    for(var i in ctrl.args) {
      for(var k in ctrl.args[i]) {
        a0[k] = ctrl.args[i][k];
      }
    }
    ctrl.args = a0;
  }


  if(callback) {

    Showtime.httpReq(url, ctrl || {}, function(err, res) {
      if(err)
        callback(err, null);
      else
        callback(null, new HttpResponse(res.buffer, res.responseheaders));
    });
    return;
  }
  var res = Showtime.httpReq(url, ctrl || {});
  return new HttpResponse(res.buffer, res.responseheaders);
}
