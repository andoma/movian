

exports.writeFileSync = function(filename, data, opts) {
  var fd = Showtime.fs.open(filename, 'w');
  try {
    Showtime.fs.write(fd, data, 0, data.length, 0);
  }
  finally {
    Showtime.resourceDestroy(fd);
  }
}

exports.readFileSync = function(filename, opts) {
  var fd = Showtime.fs.open(filename, 'r');
  try {
    var buf = new Duktape.Buffer(Showtime.fs.fsize(fd));
    Showtime.fs.read(fd, buf.valueOf(), 0, buf.length, 0);
    return buf;
  } finally {
    Showtime.resourceDestroy(fd);
  }
}
