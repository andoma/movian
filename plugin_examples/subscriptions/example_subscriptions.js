/**
 *
 */

(function(plugin) {

  var U = "example:subscriptions:";

  plugin.subscribe("global.clock.unixtime", function(v) {
    showtime.print("Current unix time is " + v);
  });

  plugin.subscribe("global.navigators.current.currentpage.url", function(v) {
    showtime.print("Current URL is " + v);
  });

  // Register a service (will appear on home page)
  plugin.createService("Subscriptions example", U, "other", true);

  plugin.addURI(U, function(page) {
    page.type = "directory";
    page.metadata.title = "Hello";
    page.loading = false;

    // Dump the page's property tree on the console
    page.dump();

    // Show if the page is bookmarked or not
    page.subscribe("page.bookmarked", function(v) {
      showtime.print("Page " + U + " is " + (v ? "bookmarked" : "not bookmarked"));
    });
  });

})(this);
