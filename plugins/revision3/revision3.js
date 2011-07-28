/**
 * Revision3 plugin for showtime version 0.3  by NP
 *
 *  Copyright (C) 2011 NP
 *
 * 	ChangeLog:
 * 	0.3
 * 	Major rewrite
 * 	Add support to Archives
 * 	Add suport to all episodes 
 * 
 * 	TODO:
 * 	Clean up some of the code
 * 
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


(function(plugin) {


//settings 

  plugin.service =
    showtime.createService("Revision3", "revision3:start", "tv", false,
			   plugin.config.path + "logo_large.png");
  
  plugin.settings = plugin.createSettings("Revision3", "video",
					  plugin.config.path + "logo_large.png",
					 "Revison3: The Best TV Shows on the net");

  plugin.settings.createInfo("info",
			     plugin.config.path + "logo_large.png",
			     "Internet Television any way you want it.\n\n"+
				 "Revision3 is the leading television network for the internet generation.\n"+
				 "We create and produce all-original episodic community driven programs watched\n"+
				 "by a super-committed and passionate fan base. Learn more on wwww.revision3.com \n" + 
				 "Plugin developed by NP \n");

  plugin.settings.createBool("enabled", "Revision3", false, function(v) {
    plugin.config.URIRouting = v;
    plugin.config.search = v;
    plugin.service.enabled = v;
  });

plugin.settings.createBool("hd", "HD", false, function(v) {
    plugin.service.hd = v;
  });


function startPage(page) {      	
		
		var content = showtime.httpGet("http://revision3.com/shows").toString();
		var inicio = content.indexOf('<ul id="shows">');
		var fim = content.indexOf('</ul>', inicio);
		var nice = content.slice(inicio, fim+6);
		var split = nice.split('</li>');
	
		for each (var show in split) {
			if(show.toString().match('<h3><a href="/.*">.*</a></h3>') != null){
			var metadata = {
				title: show.toString().match('<h3><a href="/.*">.*</a></h3>').toString().match('">.*</a></h3>').toString().replace('</a></h3>',"").replace('">',""),
				//description: nice.toString(),//descrip.toString().replace('<p class="description">',"").replace('</p>',""), 
				icon: show.toString().match('<img src=".*" /></a>').toString().replace('<img src="',"").replace('" /></a>',"")
			};
			
			var url = show.toString().match('<h3><a href="/.*">').toString().replace('<h3><a href="/',"").replace('">','');
			page.appendItem("revision3:show:feed:" + url,"directory", metadata);
		}
		}
		
		//Archives	
		page.appendItem("revision3:archives", "directory", {
		  title: "Archived Shows",
		  icon:  plugin.config.path + "logo_large.png"
		  });
	
			
	page.type = "directory";
    page.contents = "items";
    page.loading = false;

    page.metadata.logo = plugin.config.path + "icon.png";
    page.metadata.title = "Revision3";

  }


plugin.addURI("revision3:archives", function(page) {

   page.type = "directory";
   page.contents = "items";
   page.loading = false;

   page.metadata.logo = plugin.config.path + "icon.png";
   page.metadata.title = "Archived Shows";

   var content = showtime.httpGet("http://revision3.com/shows/archive").toString();
		var inicio = content.indexOf('<ul id="shows">');
		var fim = content.indexOf('</ul>', inicio);
		var nice = content.slice(inicio, fim+6);
		var split = nice.split('</li>');
	
		for each (var show in split) {
			if(show.toString().match('<h3><a href="/.*">.*</a></h3>') != null){
			var metadata = {
				title: show.toString().match('<h3><a href="/.*">.*</a></h3>').toString().match('">.*</a></h3>').toString().replace('</a></h3>',"").replace('">',""),
				//description: nice.toString(),//descrip.toString().replace('<p class="description">',"").replace('</p>',""), 
				icon: show.toString().match('<img src=".*" /></a>').toString().replace('<img src="',"").replace('" /></a>',"")
			};
			
			var url = show.toString().match('<h3><a href="/.*">').toString().replace('<h3><a href="/',"").replace('">','');
			page.appendItem("revision3:show:feed:" + url,"directory", metadata);
		}
		}
 


  page.loading = false; 
});



plugin.addURI("revision3:show:feed:([a-z0-9,]*)", function(page, show) {
   
   if(plugin.service.hd == "1"){ var VideoQuality ="MP4-hd30"; }else{ var VideoQuality ="MP4-Large"; }
		
   page.contents = "video";
   page.type = "directory";
   
   page.metadata.logo = plugin.config.path + "icon.png";
   
   var doc = new XML(showtime.httpGet("http://revision3.com/" + show + "/feed/" + VideoQuality).toString());
   page.metadata.title = doc.channel.title;
	   

   for each (var arg in doc.channel.item) {
		  
	var metadata = {
	      title: arg.title,
	      description: arg.description,
	      icon:  doc.channel.image.url
	  };
	var url = "http://videos.revision3.com/revision3/web" + arg.guid;
	page.appendItem(url,"video", metadata);
   }
   
   //All Episodes
   page.appendItem("revision3:show:" + show, "directory", {
		  title: "All Episodes",
		  icon:  "http://videos.revision3.com/revision3/images/shows/unboxingporn/unboxingporn_160x160.jpg"
		  });
 
   
  page.loading = false; 
});

plugin.addURI("revision3:show:([a-z0-9,]*)", function(page, show) {
	
   page.contents = "video";
   page.type = "directory";
   page.metadata.logo = plugin.config.path + "icon.png";
   
   
   var content = showtime.httpGet("http://revision3.com/" + show).toString();
   var inicio = content.indexOf('<tbody>');
   var fim = content.indexOf('</tbody>', inicio);
   var nice = content.slice(inicio, fim+8);
   var split = nice.split('</tr>');
      
   var name = split[1].toString().match('<td class="show" nowrap>.*</td>').toString().replace('<td class="show" nowrap>',"").replace('</td>',"");
   page.metadata.title = name.toString();
   
   for each (var episode in split) {
	   if(episode.toString().match('<td class="title"><a href="/.*">.*</a></td>') != null){
		   var metadata = {
			   title: episode.toString().match('<td class="episode-number" nowrap>.*</td>').toString().replace('<td class="episode-number" nowrap>',"").replace('</td>',"") + '  '+ episode.toString().match('<td class="title"><a href="/.*">.*</a></td>').toString().replace('<td class="title"><a href="',"").match('">.*</a></td>').toString().replace('</a></td>',"").replace('">',""),
			   description: episode.toString().match('<td class="title"><a href="/.*">.*</a></td>').toString().replace('<td class="title"><a href="',"").match('">.*</a></td>').toString().replace('</a></td>',"").replace('">',""), 
			   icon: "http://videos.revision3.com/revision3/images/shows/unboxingporn/unboxingporn_160x160.jpg",
			   duration: episode.toString().match('<td class="running-time">.*</td>').toString().replace('<td class="running-time">',"").replace('</td>',""),
			   rating: 3 / 5.0
			};
			   
			   var url = episode.toString().match('<td class="title"><a href="/.*"').toString().replace('<td class="title"><a href="/',"").replace('">','');
			   //showtime.trace(url);
			   //url = getVideo(url);
			   //showtime.trace(url);
			   page.appendItem('revision3:link:' + url ,"directory", metadata);
			   
	   }
	}

  page.loading = false; 
});


plugin.addURI("revision3:link:(.*)", function(page, link) {
   	
   page.contents = "video";
   page.type = "directory";
   
   page.metadata.logo = plugin.config.path + "icon.png";
   
   var content = showtime.httpGet("http://revision3.com/" + link.toString().replace('"','')).toString();
   page.metadata.title = content.match('<title>.*</title>').toString().replace('<title>','').replace('</title>','');
   var descrip = content.match('<div class="summary">.*</div>').toString().replace('<div class="summary">','').replace('</div>','');
   var img = content.match('"><img class="thumbnail" src=".*" alt').toString().replace('"><img class="thumbnail" src="','').replace('" alt','');	   
   var runtime = content.match('running time .*</div>').toString().replace('running time ','').replace('</div>','');
   
   var url = content.match('href=".*">Tablet</a>');
   if(url != null){
	   url = url.toString().replace('href="','').replace('">Tablet</a>','');
		  
	var metadata = {
	      title: page.metadata.title,
	      description: descrip,
	      icon: img, 
	      duration: runtime
	  };
	
	page.appendItem(url,"video", metadata);
   }
   url = content.match('href=".*">HD</a>');
   //old episodes dont have HD 
   if(url != null){
	   url = url.toString().replace('href="','').replace('">HD</a>','');
    metadata = {
	      title: page.metadata.title + '  (HD)',
	      description: descrip,
	      icon:  img,
	      duration: runtime
	  };
	
	page.appendItem(url,"video", metadata);
   }
    
  page.loading = false; 
});

	
plugin.addURI("revision3:start", startPage);
})(this);
