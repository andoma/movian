var string = require('native/string');
var pe = string.paramEscape;

// https://nodejs.org/docs/latest/api/url.html#url_url_format_urlobj

exports.format = function(d) {
  var proto = d.protocol;
  if(proto[proto.length - 1] == ':')
    proto = proto.substring(0, proto.length - 1);

  var slashes = d.slashes ||
    ['http', 'https', 'ftp', 'file'].indexOf(proto) != -1;

  var host = d.host || (d.hostname + (d.port ? (':' + port) : ''));

  var url = proto + (slashes ? '://' : ':') + (d.auth ? d.auth + '@' : '') +
    host + (d.pathname[0] == '/' ? d.pathname : '/' + d.pathname)

  if(d.search) {
    url += d.search[0] == '?' ? d.search : '?' + d.search;
  } else if(d.query) {
    var pfx = '?';
    for(k in d.query) {
      url += pfx + pe(k) + '=' + pe(d.query[k]);
      pfx = '&';
    }
  }

  if(d.hash)
    url += d.hash[0] == '#' ? d.hash : '#' + d.hash;

  return url;
}

exports.parse = string.parseURL;

exports.resolve = string.resolveURL;

