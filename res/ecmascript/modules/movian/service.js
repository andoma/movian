
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
        require('native/service').enable(this.id, val);
      },
      get: function() {
        require('native/service').enable(this.id);
      }
    }
  });
}

Service.prototype.destroy = function() {
  Core.resourceDestroy(this.id);
}


exports.create = function(title, url, type, enabled, icon) {
  var s = require('native/service');
  return new Service(s.create("plugin:" + Plugin.id, title, url,
                              type, enabled, icon));

}
