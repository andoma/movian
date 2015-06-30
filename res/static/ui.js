
function durationText(v) {
  var h = parseInt(v / 3600);
  var m = parseInt(v / 60) % 60;
  var s = parseInt(v) % 60;
  s = (s <= 9 ? '0' + s : s);
  if(h > 0) {
    return h + ':' + (m <= 9 ? '0'+ m : m) + ':' + s;
  } else {
    return m + ':' + s;
  }
}



var stpp = new STPPClient();


window.onload = function() {

  stpp.bindInnerHTML(stpp.global, "media.current.metadata.title",
		     'currentTrack');

  stpp.bindInnerHTML(stpp.global, "nav.currentpage.model.metadata.title",
		     'pageTitle');

  new STPP.ElementFactory(stpp, {
    propref: stpp.global,
    path: "nav.currentpage.model.nodes",
    parent:  document.getElementById('items'),
    cls: 'listrow',
    initElement: function(e) {
      var node = e.id;
      new STPP.ViewSwitcher(stpp, {
	parent: e,
	propref: e.id,
	path: 'type',
	clspfx: 'list',
	views: {
	  'video': function(e) {
	    var s = document.createElement('span');
	    s.className = 'listcell itemtitle'
	    stpp.bindInnerHTML(node, "metadata.title", s);
	    e.appendChild(s);
      
	    var s = document.createElement('span');
	    s.className = 'listcell itemduration'
	    stpp.bindInnerHTML(node, "metadata.duration", s, durationText);
	    e.appendChild(s);
	  },


	  'audio': function(e) {
	    var s = document.createElement('img');
	    s.className = 'listcell itemicon';
	    stpp.bindIconURI(node, "metadata.album_art", s);
	    e.appendChild(s);


	    var s = document.createElement('span');
	    s.className = 'listcell itemduration'
	    stpp.bindInnerHTML(node, "metadata.duration", s, durationText);
	    e.appendChild(s);

	    var s = document.createElement('span');
	    s.className = 'listcell itemtitle'
	    stpp.bindInnerHTML(node, "metadata.title", s);
	    e.appendChild(s);
      
	    var s = document.createElement('span');
	    s.className = 'listcell itemartist'
	    stpp.bindInnerHTML(node, "metadata.artist", s);
	    e.appendChild(s);

	    var s = document.createElement('span');
	    s.className = 'listcell itemalbum'
	    stpp.bindInnerHTML(node, "metadata.album", s);
	    e.appendChild(s);


	  },

	  '_default': function(e, v) {
	    var s = document.createElement('span');
	    s.className = 'listcell'
	    stpp.bindInnerHTML(node, "metadata.title", s);
	    e.appendChild(s);
	  }
	}
      });
    }
  });
}



