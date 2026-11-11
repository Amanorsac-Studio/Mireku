/* The Releases wall: choosing a cover loads that release into the Spotify
   player under it. Without JavaScript the covers still show, and the player
   keeps James's top tracks. */
(function () {
  var frame = document.getElementById('spotify');
  var now = document.getElementById('now');
  var tiles = document.querySelectorAll('.release');
  if (!frame) return;
  tiles.forEach(function (b) {
    b.addEventListener('click', function () {
      tiles.forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); });
      frame.src = 'https://open.spotify.com/embed/album/' + b.dataset.album + '?theme=0';
      frame.title = b.querySelector('.rt').textContent + ' on Spotify';
      now.textContent = 'Playing: ' + b.querySelector('.rt').textContent;
      if (window.mirekuTrack) window.mirekuTrack('release_play', b.querySelector('.rt').textContent);
      var r = frame.getBoundingClientRect();
      if (r.top < 0 || r.bottom > window.innerHeight) {
        frame.scrollIntoView({ behavior: matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth', block: 'center' });
      }
    });
  });
})();
