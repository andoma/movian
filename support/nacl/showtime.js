
function launchShowtime() {
  document.body.innerHTML ='<embed id="showtime" src="showtime.nmf" type="application/x-pnacl"/>';
}

navigator.webkitPersistentStorage.requestQuota(128*1024*1024, function(bytes) {
  console.log('Allocated '+bytes+' bytes of persistant storage.');
  launchShowtime();
  document.getElementById("showtime").focus();
}, function(e) {
  alert('Failed to allocate disk space, Showtime will not start')
});



document.addEventListener('visibilitychange', function() {
  document.getElementById('showtime').postMessage(document.hidden ? 'hidden'
                                                  : 'visible');
}, false);

