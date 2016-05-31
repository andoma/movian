var page = require('movian/page');


new page.Route('asyncPageLoad:test:(.*)', function(page, arg1) {
  var offset = 0;

  function loader() {
    setTimeout(function() {

      if(offset > 100) {
        page.haveMore(false);
        return;
      }

      for(var i = 0; i < 20; i++) {
        page.appendItem('asyncPageLoad:item:' + (offset + i), "directory", {
          title: "Item" + (offset + i)
        });
      }
      offset += 20;
      page.haveMore(true);
    }, 1000);

  }

  page.type = "directory";
  page.asyncPaginator = loader;
  loader();
});
