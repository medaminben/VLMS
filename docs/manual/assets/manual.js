// Opens a screenshot full-size on click; Escape or a click closes it.
(function () {
  var box = document.querySelector('.lightbox');
  if (!box) return;
  var big = box.querySelector('img');
  document.querySelectorAll('figure.shot a').forEach(function (link) {
    link.addEventListener('click', function (event) {
      event.preventDefault();
      big.src = link.getAttribute('href');
      big.alt = link.querySelector('img').alt;
      box.hidden = false;
    });
  });
  function close() { box.hidden = true; big.removeAttribute('src'); }
  box.addEventListener('click', close);
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') close(); });
})();
