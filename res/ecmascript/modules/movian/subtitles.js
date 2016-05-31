var subtitle = require('native/subtitle');


exports.addProvider = function(fn) {
  subtitle.addProvider(function(root, query, basescore, autosel) {
    var req = Object.create(query);
    req.addSubtitle = function(url, title, language, format,
                               source, score) {
      subtitle.addItem(root, url, title, language, format, source,
                       basescore + score, autosel);
      }
    fn(req);
  }, Plugin.id, Plugin.id);
}


exports.getLanguages = subtitle.getLanguages;
