// Display helpers. Change the currency symbol / locale here.
const CURRENCY = '₹';
const LOCALE = 'en-IN';

export const money = (n) =>
  CURRENCY +
  Number(n || 0).toLocaleString(LOCALE, { minimumFractionDigits: 2, maximumFractionDigits: 2 });

export const plain = (v) => {
  if (v === null || v === undefined || v === '') return '–';
  const n = Number(v);
  return Number.isNaN(n)
    ? String(v)
    : n.toLocaleString(LOCALE, { minimumFractionDigits: 2, maximumFractionDigits: 2 });
};

export const clock = (ts) => (ts && ts.length >= 19 ? ts.slice(11, 19) : ts || '–');

export const tid = (id) => `T${id}`;
