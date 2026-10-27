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
})();
