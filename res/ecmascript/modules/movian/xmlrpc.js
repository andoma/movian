exports.call = function() {
  var a = [];
  for(var i = 2; i < arguments.length; i++)
    a.push(arguments[i]);
  var json = JSON.stringify(a);
  var x = require('native/io').xmlrpc(arguments[0], arguments[1], json);
  return require('movian/xml').htsmsg(x);
}
