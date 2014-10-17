
exports.DB = function(dbname) {
  this.db = Showtime.sqlite.create(dbname);
}

exports.DB.prototype.close = function() {
  Showtime.resourceDestroy(this.db);
}

exports.DB.prototype.query = function() {
  var i;
  var args = [];
  for(var i = 0; i < arguments.length; i++)
    args[i] = arguments[i];
  args.unshift(this.db);

  Showtime.sqlite.query.apply(null, args);
}

exports.DB.prototype.step = function() {
  return Showtime.sqlite.step(this.db);
}


exports.DB.prototype.upgradeSchema = function(path) {
  return Showtime.sqlite.upgradeSchema(this.db, path);
}


Object.defineProperties(exports.DB.prototype, {
  lastRowId: {
    get: function() { return Showtime.sqlite.lastRowId(this.db) }
  },
  lastErrorString: {
    get: function() { return Showtime.sqlite.lastErrorString(this.db) }
  },
  lastErrorCode: {
    get: function() { return Showtime.sqlite.lastErrorCode(this.db) }
  }

});
