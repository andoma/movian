
// ---------------------------------------------------------------
// Service
// ---------------------------------------------------------------

function Service(id) {
  Object.defineProperties(this, {
    id: {
      value: id,
    },
    enabled: {
      set: function(val) {
        Showtime.serviceEnable(this.id, val);
      },
      get: function() {
        return Showtime.serviceEnable(this.id);
      }
    }
  });
}

Service.prototype.destroy = function() {
  Showtime.resourceDestroy(this.id);
}


exports.create = function(title, url, type, enabled, icon) {
  return new Service(Showtime.serviceCreate("plugin:" + Plugin.id, title, url,
                                            type, enabled, icon));

}
