
var sqlite = require('native/sqlite');

exports.DB = function(dbname) {
  this.db = sqlite.create(dbname);
}

exports.DB.prototype.close = function() {
  Core.resourceDestroy(this.db);
}

exports.DB.prototype.query = function() {
  var i;
  var args = [];
  for(var i = 0; i < arguments.length; i++)
    args[i] = arguments[i];
  args.unshift(this.db);

  sqlite.query.apply(null, args);
}

exports.DB.prototype.step = function() {
  return sqlite.step(this.db);
}


exports.DB.prototype.upgradeSchema = function(path) {
  return sqlite.upgradeSchema(this.db, path);
}


Object.defineProperties(exports.DB.prototype, {
  lastRowId: {
    get: function() { return sqlite.lastRowId(this.db) }
  },
  lastErrorString: {
    get: function() { return sqlite.lastErrorString(this.db) }
  },
  lastErrorCode: {
    get: function() { return sqlite.lastErrorCode(this.db) }
  }

});
