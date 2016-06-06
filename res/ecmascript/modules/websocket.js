var ws = require('native/websocket');


exports.w3cwebsocket = function(URL, protocol) {

  var self = this;

  self.onopen  = function() {}
  self.oninput = function() {}
  self.onclose = function() {}

  this._sock = ws.clientCreate(URL, protocol, {

    onConnect: function() {
      self.onopen();
    },
    onInput: function(d) {
      self.oninput({
        data: d
      });
    },
    onClose: function(code, msg) {
      self.onclose();
    }
  });
}


exports.w3cwebsocket.prototype.send = function(d) {
  ws.clientSend(this._sock, d);
}

exports.w3cwebsocket.prototype.close = function(d) {
  Core.resourceDestroy(this._sock);
  setTimeout(function() {
    this.onclose();
  }.bind(this), 0);
}
