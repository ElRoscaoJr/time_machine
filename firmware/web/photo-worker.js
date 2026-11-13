import { parse } from 'exifr/dist/full.esm.mjs';
import encode from 'jpeg-js/lib/encoder.js';

self.onmessage = async ({ data: { file, pixels } }) => {
  try {
    if (pixels) {
      const { data } = encode(pixels, 84);
      self.postMessage({ jpeg: data.buffer }, [data.buffer]);
      return;
    }
    let meta;
    try {
      meta = await parse(file, {
        pick: ['DateTimeOriginal', 'CreateDate', 'GPSLatitude', 'GPSLatitudeRef', 'GPSLongitude', 'GPSLongitudeRef'],
        reviveValues: false, gps: true,
      });
    } catch {
      meta = null;
    }
    const raw = meta?.DateTimeOriginal || meta?.CreateDate;
    const match = typeof raw === 'string' && /^(\d{4}):(\d{2}):(\d{2})(?:\s|$)/.exec(raw);
    const date = match ? match.slice(1, 4).join('-') : '';
    const lat = Number.isFinite(meta?.latitude) ? meta.latitude : null;
    const lon = Number.isFinite(meta?.longitude) ? meta.longitude : null;
    self.postMessage({ meta: { date, lat, lon } });
  } catch (error) {
    self.postMessage({ error: error.message });
  }
};
