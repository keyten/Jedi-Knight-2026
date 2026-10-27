(() => {
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* starfield with slow parallax drift */
  const canvas = document.getElementById('stars');
  const ctx = canvas.getContext('2d');
  let stars = [], w = 0, h = 0, dpr = 1, scrollY = 0;

  function resize() {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    w = canvas.width = innerWidth * dpr;
    h = canvas.height = innerHeight * dpr;
    const count = Math.round((innerWidth * innerHeight) / 4200);
    stars = Array.from({ length: count }, () => ({
      x: Math.random() * w,
      y: Math.random() * h,
      z: Math.random() * 0.8 + 0.2,
      t: Math.random() * Math.PI * 2,
      hue: Math.random() < 0.12 ? (Math.random() < 0.5 ? 200 : 350) : 220,
    }));
  }

  function draw(time) {
    ctx.clearRect(0, 0, w, h);
    for (const s of stars) {
      const y = ((s.y - scrollY * dpr * s.z * 0.25) % h + h) % h;
      const tw = reduced ? 1 : 0.65 + 0.35 * Math.sin(time * 0.0012 * s.z + s.t);
      const r = s.z * 1.2 * dpr;
      ctx.fillStyle = `hsla(${s.hue}, 80%, ${s.hue === 220 ? 92 : 75}%, ${s.z * tw})`;
      ctx.beginPath();
      ctx.arc(s.x, y, r, 0, Math.PI * 2);
      ctx.fill();
    }
    if (!reduced) requestAnimationFrame(draw);
  }

  resize();
  addEventListener('resize', resize);
  requestAnimationFrame(draw);

  /* nav background + active link */
  const nav = document.getElementById('nav');
  const links = [...document.querySelectorAll('.nav-links a')];
  const sections = links.map(a => document.querySelector(a.getAttribute('href')));
  function onScroll() {
    scrollY = window.scrollY;
    nav.classList.toggle('scrolled', scrollY > 20);
    let current = -1;
    sections.forEach((s, i) => { if (s && s.getBoundingClientRect().top < innerHeight * 0.4) current = i; });
    links.forEach((a, i) => a.classList.toggle('active', i === current));
    if (reduced) draw(0);
  }
  addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  /* mobile menu */
  const toggle = document.getElementById('navToggle');
  const menu = document.getElementById('navLinks');
  toggle.addEventListener('click', () => {
    const open = menu.classList.toggle('open');
    toggle.setAttribute('aria-expanded', open);
  });
  links.forEach(a => a.addEventListener('click', () => {
    menu.classList.remove('open');
    toggle.setAttribute('aria-expanded', 'false');
  }));

  /* reveal on scroll */
  const io = new IntersectionObserver(entries => {
    entries.forEach(e => {
      if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); }
    });
  }, { threshold: 0.12, rootMargin: '0px 0px -40px 0px' });
  document.querySelectorAll('.reveal').forEach((el, i) => {
    el.style.transitionDelay = `${(i % 4) * 70}ms`;
    io.observe(el);
  });

  /* level pips */
  document.querySelectorAll('.pips').forEach(p => {
    p.innerHTML = '<i></i>'.repeat(+p.dataset.lv || 0);
    p.setAttribute('aria-label', `${p.dataset.lv} levels`);
  });

  /* graphics filter */
  const tabs = [...document.querySelectorAll('#gfxTabs button')];
  const cards = [...document.querySelectorAll('#gfxGrid .card')];
  tabs.forEach(t => t.addEventListener('click', () => {
    tabs.forEach(x => x.setAttribute('aria-selected', x === t));
    const cat = t.dataset.tab;
    cards.forEach(c => {
      const show = cat === 'all' || c.dataset.cat === cat;
      c.classList.toggle('hidden', !show);
      c.classList.remove('pop');
      if (show && !reduced) { void c.offsetWidth; c.classList.add('pop'); }
    });
  }));
})();
