/**
 * Very simple plugin example of browsing music
 */

(function(plugin) {

  var U = "example:music:";

  // Register a service (will appear on home page)
  plugin.createService("Music example", U, "other", true);

  // Add a responder to the registered URI
  plugin.addURI(U, function(page) {
    page.type = "directory";
    page.metadata.title = "Music examples";

    var B = "http://www.lonelycoder.com/music/";

    page.appendItem(B + "Hybris_Intro-remake.mp3", "audio", {
      title: "Remix of Hybris (The Amiga Game)",
      artist: "Andreas Öman"
    });

    page.appendItem(B + "Russian_Ravers_Rave.mp3", "audio", {
      title: "Russian Ravers Rave",
      artist: "Andreas Öman"
    });

    page.appendItem(B + "spaceships_and_robots_preview.mp3", "audio", {
      title: "Spaceships and Robots",
      artist: "Andreas Öman"
    });

    page.appendItem(B + "enigma.mod.zip", "audio", {
      title: "mod.Enigma",
      artist: "Tip & Firefox"
    });

    page.loading = false;
  });
})(this);
