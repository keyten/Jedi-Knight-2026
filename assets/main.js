(() => {
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* starfield */
  const canvas = document.getElementById('stars');
  const ctx = canvas.getContext('2d');
  let stars = [], w = 0, h = 0, dpr = 1, scrollY = window.scrollY;

  function resize() {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    w = canvas.width = innerWidth * dpr;
    h = canvas.height = innerHeight * dpr;
    const count = Math.round((innerWidth * innerHeight) / 6000);
    stars = Array.from({ length: count }, () => ({
      x: Math.random() * w,
      y: Math.random() * h,
      z: Math.random() * 0.7 + 0.15,
      t: Math.random() * Math.PI * 2,
    }));
  }

  function draw(time) {
    ctx.clearRect(0, 0, w, h);
    for (const s of stars) {
      const y = ((s.y - scrollY * dpr * s.z * 0.15) % h + h) % h;
      const tw = reduced ? 1 : 0.75 + 0.25 * Math.sin(time * 0.0006 * s.z + s.t);
      ctx.fillStyle = `rgba(220, 230, 255, ${s.z * 0.8 * tw})`;
      ctx.beginPath();
      ctx.arc(s.x, y, s.z * 1.1 * dpr, 0, Math.PI * 2);
      ctx.fill();
    }
    if (!reduced) requestAnimationFrame(draw);
  }

  resize();
  addEventListener('resize', () => { resize(); if (reduced) draw(0); });
  requestAnimationFrame(draw);

  /* nav background */
  const nav = document.getElementById('nav');
  function onScroll() {
    scrollY = window.scrollY;
    nav.classList.toggle('scrolled', scrollY > 20);
    if (reduced) draw(0);
  }
  addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  /* mobile menu */
  const toggle = document.getElementById('navToggle');
  const menu = document.getElementById('navLinks');
  toggle.addEventListener('click', () => {
    toggle.setAttribute('aria-expanded', menu.classList.toggle('open'));
  });
  menu.querySelectorAll('a').forEach(a => a.addEventListener('click', () => {
    menu.classList.remove('open');
    toggle.setAttribute('aria-expanded', 'false');
  }));

  /* feature modal */
  const features = window.FEATURES || {};
  const modal = document.getElementById('modal');
  const body = document.getElementById('modalBody');
  const panel = modal.querySelector('.modal-panel');
  let opener = null;

  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const icons = {
    image: '<svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="2"/><circle cx="9" cy="10" r="1.6"/><path d="m21 16-5-5-8 8"/></svg>',
    video: '<svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="2"/><path d="m10 9 5 3-5 3z"/></svg>',
  };

  function mediaItem(m) {
    const cap = m.caption ? `<figcaption>${esc(m.caption)}</figcaption>` : '';
    if (m.type === 'video') return `<figure><video src="${esc(m.src)}" controls muted loop playsinline preload="metadata"></video>${cap}</figure>`;
    if (m.type === 'youtube') return `<figure><iframe src="https://www.youtube-nocookie.com/embed/${esc(m.id)}" title="${esc(m.caption || 'Video')}" allowfullscreen loading="lazy"></iframe>${cap}</figure>`;
    return `<figure><img src="${esc(m.src)}" alt="${esc(m.caption || '')}" loading="lazy">${cap}</figure>`;
  }

  function mediaHtml(list) {
    if (!list.length) {
      const slot = (type, label) => `<div class="media-slot"><div>${icons[type]}${label}</div></div>`;
      return `<div class="media">${slot('image', 'Screenshot')}${slot('image', 'Screenshot')}${slot('video', 'Video')}</div>`;
    }
    const cls = list.length === 1 ? ' single' : list.length === 2 ? ' two' : '';
    return `<div class="media${cls}">${list.map(mediaItem).join('')}</div>`;
  }

  function open(id, el) {
    const f = features[id];
    if (!f) return;
    opener = el || null;
    const multi = f.sections.length > 1;
    body.innerHTML =
      `<p class="modal-group">${esc(f.group)}</p>` +
      `<h2 id="modalTitle">${esc(f.title)}</h2>` +
      `<p class="modal-cvar"><code>${esc(f.cvar)}</code></p>` +
      mediaHtml(f.media) +
      (f.how ? `<div class="how"><h3>How it works</h3><p>${f.how}</p></div>` : '') +
      `<div class="doc">${f.sections.map(s => `<section>${multi ? `<h3>${esc(s.title)}</h3>` : ''}${s.html}</section>`).join('')}</div>`;
    modal.hidden = false;
    document.body.classList.add('modal-open');
    panel.scrollTop = 0;
    modal.querySelector('.modal-close').focus();
    if (location.hash !== '#f/' + id) history.replaceState(null, '', '#f/' + id);
  }

  function close() {
    if (modal.hidden) return;
    modal.hidden = true;
    document.body.classList.remove('modal-open');
    body.querySelectorAll('video').forEach(v => v.pause());
    history.replaceState(null, '', location.pathname + location.search);
    if (opener) opener.focus({ preventScroll: true });
  }

  document.querySelectorAll('[data-feature]').forEach(card => {
    card.addEventListener('click', e => { if (!e.target.closest('a')) open(card.dataset.feature, card); });
    card.addEventListener('keydown', e => {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(card.dataset.feature, card); }
    });
  });
  modal.addEventListener('click', e => { if (e.target.closest('[data-close]')) close(); });
  addEventListener('keydown', e => {
    if (modal.hidden) return;
    if (e.key === 'Escape') close();
    if (e.key === 'Tab') {
      const focusable = [...panel.querySelectorAll('button, a[href], video, iframe')];
      const first = focusable[0], last = focusable[focusable.length - 1];
      if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
      else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
    }
  });

  const fromHash = () => {
    const m = location.hash.match(/^#f\/([\w-]+)$/);
    if (m) open(m[1], document.querySelector(`[data-feature="${m[1]}"]`));
  };
  addEventListener('hashchange', fromHash);
  fromHash();
})();
