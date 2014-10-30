/**
 *
 */

(function(plugin) {

  var x = plugin.createSettings('Settings example for plugin', null,
                               'This is the description');

  x.createBool('v1', 'en bool2', true, function(val) {
    print("Bool is now", val);
  });

  x.createString('v2', 'A string', 'default string', function(val) {
    print("Bool is now", val);
  });

  x.createInt('v3', 'One int', 42, -50, 50, 1, 'px', function(val) {
    print("Int is now", val);
  });

  x.createDivider("It's dangerous below. Bevare!");

  x.createAction('action', 'press here to explode the universe', function() {
    print("BANG");
  });



})(this);
