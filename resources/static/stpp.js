
STPP = {
  SUBSCRIBE: 1,
  UNSUBSCRIBE: 2,
  SET: 3,
  NOTIFY: 4,
  ADDNODES: 5,
  DELNODES: 6,
  MOVENODE: 7
}

//
// STPPSub
//
function STPPSub(stpp, prop, path, handler) {
  this.id = stpp.idtally++;
  stpp.subs[this.id] = this;
  this.stpp = stpp;
  this.prop = prop;
  this.path = path;
  this.handler = handler;
  if(stpp.connected)
    this.subscribe();
}

STPPSub.prototype.destroy = function() {
  if(this.stpp) {
    if(this.stpp.connected)
      this.unsubscribe();
    delete this.stpp.subs[this.id];
  }
}

STPPSub.prototype.subscribe = function() {
  this.stpp.send([1, this.id, this.prop, this.path]);
}

STPPSub.prototype.unsubscribe = function() {
  this.stpp.send([2, this.id]);
}

//
// STPPClient
//
function STPPClient() {
  this.con = new WebSocket('ws://' + window.location.host + '/showtime/stpp');
  this.subs = {};
  this.idtally = 1;
  this.connected = false;
  this.global = 0;
  this.con.onopen = this.onopen.bind(this);
  this.con.onmessage = this.onmessage.bind(this);
}


STPPClient.prototype.onopen = function() {
  this.connected = true;
  for(var v in this.subs)
    this.subs[v].subscribe();
}


STPPClient.prototype.onmessage = function(e) {
  var msg = JSON.parse(e.data);
  switch(msg[0]) {
  case STPP.NOTIFY:
  case STPP.ADDNODES:
  case STPP.DELNODES:
  case STPP.MOVENODE:
    if (msg[1] in this.subs) {
      this.subs[msg[1]].handler(msg);
    }
    break;

  default:
    console.log("Input unknown to me:");
    console.log(msg);
    break;
  }
}



STPPClient.prototype.subValue = function(prop, path, fn) {
  return new STPPSub(this, prop, path, function(m) {
    if(m[0] == STPP.NOTIFY)
      fn(m[2]);
  });
}


STPPClient.prototype.subDir = function(prop, path, handler) {
  return new STPPSub(this, prop, path, handler);
}


STPPClient.prototype.send = function(obj) {
  this.con.send(JSON.stringify(obj));
}


//
// Helpers
//
STPPClient.prototype.bindInnerHTML = function(propref, path, elem, xlate) {
  if(typeof elem == 'string')
    elem = document.getElementById(elem);

  var s = this.subValue(propref, path, function(v) {
    if(typeof v == 'object' && v != null && v[0] == 'link')
      v = v[1];

    elem.innerHTML = xlate ? xlate(v) : v;
  });
  elem.addEventListener('DOMNodeRemovedFromDocument', function(e) {
    s.destroy();
  }, false);
}


STPPClient.prototype.bindIconURI = function(propref, path, elem, xlate) {
  var s = this.subValue(propref, path, function(v) {
    if(typeof v == 'object' && v != null && v[0] == 'link')
      v = v[2];
    elem.src = '/showtime/image?url=' + v
  });
  elem.addEventListener('DOMNodeRemovedFromDocument', function(e) {
    s.destroy();
  }, false);
}


//
// ElementFactory
//
STPP.ElementFactory = function(stpp, spec) {
  this.parent = spec.parent;
  this.initElement = spec.initElement;
  this.type = spec.type || 'div';
  this.cls = spec.cls;
  var s = stpp.subDir(spec.propref, spec.path, this.ops.bind(this));
  spec.parent.addEventListener('DOMNodeRemovedFromDocument', function(e) {
    s.destroy();
  }, false);

}

STPP.ElementFactory.prototype.ops = function(m) {
  switch(m[0]) {
  case STPP.NOTIFY:
    while(this.parent.hasChildNodes())
      this.parent.removeChild(this.parent.lastChild);
    break;

  case STPP.ADDNODES:
    var b = m[2] ? document.getElementById(m[2]) : null;
    for (var i in m[3]) {
      var e = document.createElement(this.type);
      e.className = this.cls;
      e.id = m[3][i];
      this.initElement(e);
      if(b)
	this.parent.insertBefore(e, b);
      else
	this.parent.appendChild(e);
    }
    break;

  case STPP.DELNODES:
    for (var i in m[2]) {
      var e = document.getElementById(m[2][i]);
      if(e)
	e.parentNode.removeChild(e)
    }
    break;

  case STPP.MOVENODE:
    var e = document.getElementById(m[2]);
    if(m[3])
      e.parentNode.insertBefore(e, document.getElementById(m[3]));
    else
      e.parentNode.appendChild(e);
    break;
  }
}

//
// ViewSwitcher
//
STPP.ViewSwitcher = function(stpp, spec) {
  this.views = spec.views;
  this.parent = spec.parent;
  this.clspfx = spec.clspfx;
  var s = stpp.subValue(spec.propref, spec.path, this.update.bind(this));
  this.parent.addEventListener('DOMNodeRemovedFromDocument', function(e) {
    s.destroy();
  }, false);
}

STPP.ViewSwitcher.prototype.update = function(v) {

  while(this.parent.hasChildNodes())
    this.parent.removeChild(this.parent.lastChild);
  var e = document.createElement('div');
  e.className = this.clspfx + v;
  if(v in this.views)
    this.views[v](e, v);
  else if('_default' in this.views)
    this.views._default(e, v);
  this.parent.appendChild(e);
}

