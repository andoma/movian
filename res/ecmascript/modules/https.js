

exports.request = function(opts, cb) {
  return require('./http').request(opts, cb, true);
}
exports.get = function(opts, cb) {
  return require('./http').get(opts, cb, true);
}
