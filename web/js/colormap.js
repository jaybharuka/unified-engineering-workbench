// Viridis colour map (perceptually uniform, colour-blind safe), 10 anchor colours.
const STOPS = ['#440154', '#482878', '#3e4989', '#31688e', '#26828e', '#1f9e89', '#35b779', '#6ece58', '#b5de2b', '#fde725'];
const RGB = STOPS.map((h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16)));

export function colorAt(t) {
  const x = Math.min(1, Math.max(0, t)) * (RGB.length - 1);
  const i = Math.min(RGB.length - 2, Math.floor(x));
  const f = x - i;
  const a = RGB[i], b = RGB[i + 1];
  return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
}

export function cssGradient(direction = 'to top') {
  return `linear-gradient(${direction}, ${STOPS.join(', ')})`;
}
