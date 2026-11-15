const $ = (selector) => document.querySelector(selector);
const dialog = $('#photo-dialog');
const form = $('#photo-form');
let worker;
const pageSize = 24;
let offset = 0;
let current;
let files = [];
let fileCount = 0;
let previewUrl;
let busy = false;
let localities;
let lookupVersion = 0;
let selecting = false;
let pageItems = [];
let deleteBusy = false;
const selectedPhotos = new Map();
const selectionDialog = $('#selection-dialog');

function processPhoto(data, transfer = []) {
  return new Promise((resolve, reject) => {
    if (!worker) {
      const url = URL.createObjectURL(new Blob([PHOTO_WORKER], { type: 'text/javascript' }));
      try { worker = new Worker(url); }
      finally { URL.revokeObjectURL(url); }
    }
    worker.onmessage = ({ data }) => data.error ? reject(new Error(data.error)) : resolve(data);
    worker.onerror = () => reject(new Error('No se pudo procesar la imagen.'));
    worker.postMessage(data, transfer);
  });
}

async function api(path, { method = 'GET', data, body, headers = {} } = {}) {
  if (method !== 'GET') headers['X-Panel'] = '1';
  if (data !== undefined) {
    headers['Content-Type'] = 'application/json';
    body = JSON.stringify(data);
  }
  const response = await fetch('/api/' + path, { method, headers, body, cache: 'no-store' });
  if (!response.ok) {
    const error = new Error(await response.text());
    error.status = response.status;
    error.duplicate = response.headers.get('X-Photo-Duplicate') === '1';
    throw error;
  }
  return response.headers.get('Content-Type')?.includes('application/json') ? response.json() : response.text();
}

function status(message) { $('#status').textContent = message; }
function photoStatus(message) { $('#photo-status').textContent = message; }

async function refresh() {
  const device = await api('status');
  $('#ip').textContent = device.ip;
  $('#title').textContent = device.name ? 'Fotos de ' + device.name : 'Biblioteca';
  const summary = [device.count + (device.count === 1 ? ' foto' : ' fotos')];
  if (device.age >= 0) summary.push(device.age + ' años');
  if (device.freeBytes !== undefined) summary.push((device.freeBytes / 1048576).toLocaleString('es-ES', { maximumFractionDigits: 0 }) + ' MB libres');
  if (device.loading) summary.push('Leyendo la microSD…');
  else if (!device.sd) summary.push('Inserta una microSD FAT32 y reinicia');
  $('#summary').textContent = summary.join(' · ');
  $('#files').disabled = !device.sd;
  $('#upload-control').setAttribute('aria-disabled', String(!device.sd));
  if (device.ap) $('#network').open = true;
  updateWifi(device);
  if (device.loading) {
    if (location.pathname === '/') setTimeout(() => refresh().catch((error) => status(error.message)), 1500);
    return;
  }
  await showPage();
}

async function showPage() {
  const page = await api('photos?offset=' + offset);
  if (offset && !page.items.length) {
    offset = Math.max(0, Math.ceil(page.total / pageSize) - 1) * pageSize;
    return showPage();
  }
  pageItems = page.items;
  renderPhotos();
  $('#select-photos').hidden = !page.total || selecting;
  $('#empty').hidden = !!page.total || $('#files').disabled;
  $('#pages').hidden = page.total <= pageSize;
  $('#prev').disabled = offset === 0;
  $('#next').disabled = offset + pageSize >= page.total;
  $('#page').textContent = (offset / pageSize + 1) + ' / ' + Math.ceil(page.total / pageSize);
}

function renderPhotos() {
  $('#photos').replaceChildren(...pageItems.map(({ id, date, rev }) => {
    const button = document.createElement('button');
    const image = document.createElement('img');
    const label = document.createElement('span');
    button.className = 'tile';
    image.loading = 'lazy';
    image.alt = '';
    image.src = '/api/photos/' + id + '/image';
    label.textContent = date ? date.split('-').reverse().join('/') : 'Sin fecha';
    button.append(image, label);
    if (selecting) button.setAttribute('aria-pressed', String(selectedPhotos.has(id)));
    button.onclick = () => {
      if (!selecting) return openExisting(id).catch((error) => status(error.message));
      if (selectedPhotos.has(id)) selectedPhotos.delete(id);
      else selectedPhotos.set(id, rev);
      button.setAttribute('aria-pressed', String(selectedPhotos.has(id)));
      updateSelection();
    };
    return button;
  }));
  updateSelection();
}

function updateSelection() {
  $('#selection').hidden = !selecting;
  $('#select-photos').hidden = selecting || !pageItems.length;
  $('#selection-count').textContent = selectedPhotos.size + ' en la selección';
  $('#delete-selected').disabled = !selectedPhotos.size;
  $('#select-page').disabled = !pageItems.length || pageItems.every(({ id }) => selectedPhotos.has(id));
}

$('#select-photos').onclick = () => { selecting = true; renderPhotos(); };
$('#cancel-selection').onclick = () => {
  selecting = false;
  selectedPhotos.clear();
  renderPhotos();
};
$('#select-page').onclick = () => {
  for (const { id, rev } of pageItems) if (!selectedPhotos.has(id)) selectedPhotos.set(id, rev);
  renderPhotos();
};
$('#delete-selected').onclick = () => {
  $('#selection-confirmation').textContent = selectedPhotos.size === 1
    ? '¿Borrar la foto seleccionada y su nota de la microSD?'
    : '¿Borrar las ' + selectedPhotos.size + ' fotos seleccionadas y sus notas de la microSD?';
  selectionDialog.showModal();
};
$('#cancel-bulk-delete').onclick = () => selectionDialog.close();
selectionDialog.oncancel = (event) => { if (deleteBusy) event.preventDefault(); };
$('#confirm-bulk-delete').onclick = async () => {
  if (deleteBusy) return;
  deleteBusy = true;
  $('#confirm-bulk-delete').disabled = $('#cancel-bulk-delete').disabled = true;
  let deleted = 0;
  const total = selectedPhotos.size;
  let processed = 0;
  const errors = new Set();
  try {
    for (const [id, rev] of selectedPhotos) {
      $('#selection-confirmation').textContent = 'Procesando ' + (++processed) + ' de ' + total + '…';
      try {
        await deletePhoto(id, rev);
        selectedPhotos.delete(id);
        ++deleted;
      } catch (error) {
        if (error.status === 404) selectedPhotos.delete(id);
        else if (error.status === 409) {
          selectedPhotos.delete(id);
          errors.add('Vuelve a seleccionar las fichas que hayan cambiado.');
        } else errors.add(error.message);
      }
    }
    if (!selectedPhotos.size && !errors.size) selecting = false;
    try { await refresh(); } catch { errors.add('Recarga el panel para actualizar la galería.'); }
    status(deleted + (deleted === 1 ? ' foto borrada.' : ' fotos borradas.') +
      (total > deleted ? ' ' + (total - deleted) + ' sin borrar.' : '') +
      (errors.size ? ' ' + [...errors].join(' ') : ''));
  } finally {
    selectionDialog.close();
    deleteBusy = false;
    $('#confirm-bulk-delete').disabled = $('#cancel-bulk-delete').disabled = false;
  }
};

function deletePhoto(id, rev) {
  return api('photos/' + id + '/delete', { method: 'POST', data: { rev } });
}

function fill(meta) {
  for (const name of ['date', 'place', 'note', 'lat', 'lon']) form.elements[name].value = meta[name] ?? '';
  resetPlaceLookup();
  if (!form.elements.place.value.trim() && !$('#find-place').hidden) findPlace();
}

function resetPlaceLookup() {
  ++lookupVersion;
  $('#place-status').textContent = '';
  $('#find-place').hidden = !['lat', 'lon'].every((key) => {
    const field = form.elements[key];
    return field.value !== '' && field.validity.valid;
  });
  $('#find-place').disabled = false;
}

function nearestLocality(rows, lat, lon) {
  const radians = Math.PI / 180;
  const longitudeScale = Math.cos(lat * radians);
  let distanceSquared = (12 / 111.2) ** 2;
  let place = '';
  for (const [candidateLat, candidateLon, name] of rows) {
    const deltaLat = candidateLat - lat;
    const deltaLon = (candidateLon - lon) * longitudeScale;
    const squared = deltaLat ** 2 + deltaLon ** 2;
    if (squared < distanceSquared) {
      distanceSquared = squared;
      place = name;
    }
  }
  return place;
}

async function findPlace() {
  const version = ++lookupVersion;
  const button = $('#find-place');
  const message = $('#place-status');
  button.disabled = true;
  message.textContent = 'Buscando localidad…';
  const lat = Number(form.elements.lat.value);
  const lon = Number(form.elements.lon.value);
  try {
    const place = await lookupPlace(lat, lon);
    if (version !== lookupVersion) return;
    if (place) form.elements.place.value = place;
    message.textContent = place ? 'Localidad cercana sugerida. Puedes editarla.'
      : 'No se encontró una localidad cercana. Puedes escribirla a mano.';
  } catch {
    localities = null;
    if (version === lookupVersion) message.textContent = 'No se pudo cargar el catálogo de localidades.';
  } finally {
    if (version === lookupVersion) button.disabled = false;
  }
}

async function lookupPlace(lat, lon) {
  localities ??= api('localidades').then((data) => data.trimEnd().split('\n').slice(1)
    .map((row) => row.split('\t')));
  try { return nearestLocality(await localities, lat, lon); }
  catch (error) { localities = null; throw error; }
}

$('#find-place').onclick = findPlace;
for (const key of ['place', 'lat', 'lon']) form.elements[key].oninput = resetPlaceLookup;

function confirmDelete(show) {
  $('#delete-confirmation').hidden = !show;
  $('#delete').setAttribute('aria-expanded', String(show));
}

function showDialog(title, url, upload) {
  $('#dialog-title').textContent = title;
  const preview = new Image();
  preview.alt = 'Vista previa de la foto';
  preview.src = url;
  $('#preview').replaceChildren(preview);
  $('#delete').hidden = upload;
  confirmDelete(false);
  $('#skip').hidden = !upload;
  $('#save-all').hidden = !upload || !files.length;
  $('#file-progress').textContent = upload ? (fileCount - files.length) + ' de ' + fileCount : '';
  photoStatus('');
  if (!dialog.open) dialog.showModal();
}

async function openExisting(id) {
  if (busy) return;
  current = await api('photos/' + id);
  fill(current);
  showDialog('Editar foto', '/api/photos/' + id + '/image', false);
}

function releasePreview() {
  if (previewUrl) URL.revokeObjectURL(previewUrl);
  previewUrl = null;
}

async function preparePhoto(file) {
  const { meta } = await processPhoto({ file });
  const url = URL.createObjectURL(file);
  try {
    const image = new Image();
    image.src = url;
    await image.decode();
    const scale = Math.min(480 / image.naturalWidth, 254 / image.naturalHeight, 1);
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.round(image.naturalWidth * scale));
    canvas.height = Math.max(1, Math.round(image.naturalHeight * scale));
    const ctx = canvas.getContext('2d', { alpha: false });
    ctx.fillStyle = '#141311';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(image, 0, 0, canvas.width, canvas.height);
    const pixels = ctx.getImageData(0, 0, canvas.width, canvas.height);
    const { jpeg } = await processPhoto({ pixels }, [pixels.data.buffer]);
    return { meta, optimized: new Blob([jpeg], { type: 'image/jpeg' }) };
  } finally {
    URL.revokeObjectURL(url);
  }
}

async function nextFile() {
  releasePreview();
  while (files.length) {
    const file = files.shift();
    try {
      current = await preparePhoto(file);
      previewUrl = URL.createObjectURL(current.optimized);
      fill(current.meta);
      showDialog(file.name, previewUrl, true);
      return;
    } catch {
      status('No se pudo abrir «' + file.name + '». Si es HEIC, prueba a exportarla como JPEG.');
    }
  }
  current = null;
  if (dialog.open) dialog.close();
  await refresh();
}

function uploadPhoto(meta, optimized) {
  const encoded = new TextEncoder().encode(JSON.stringify(meta));
  return api('photos', {
    method: 'POST', body: new Blob([encoded, optimized]),
    headers: { 'Content-Type': 'application/octet-stream', 'X-Meta-Length': String(encoded.length) },
  });
}

async function uploadBatch(meta) {
  const total = 1 + files.length;
  let saved = 0, duplicates = 0, failed = 0;
  const errors = new Set();
  const upload = async (metadata, optimized) => {
    try {
      await uploadPhoto(metadata, optimized);
      ++saved;
    } catch (error) {
      if (error.duplicate) ++duplicates;
      else { ++failed; errors.add(error.message); }
    }
  };
  photoStatus('Procesando 1 de ' + total + '…');
  await upload(meta, current.optimized);
  while (files.length) {
    photoStatus('Procesando ' + (total - files.length + 1) + ' de ' + total + '…');
    try {
      const { meta: nextMeta, optimized } = await preparePhoto(files.shift());
      if (nextMeta.lat !== null && nextMeta.lon !== null)
        nextMeta.place = await lookupPlace(nextMeta.lat, nextMeta.lon).catch(() => '');
      await upload(nextMeta, optimized);
    } catch {
      ++failed;
      errors.add('Algunas imágenes no se pudieron abrir.');
    }
  }
  try { await nextFile(); } catch { errors.add('Recarga el panel para actualizar la galería.'); }
  status(saved + (saved === 1 ? ' foto guardada.' : ' fotos guardadas.') +
    (duplicates ? ' Duplicadas omitidas: ' + duplicates + '.' : '') +
    (failed ? ' Sin guardar: ' + failed + '.' : '') +
    (errors.size ? ' ' + [...errors].join(' ') : ''));
}

function metaFields() {
  const fields = new FormData(form);
  const meta = Object.fromEntries(['date', 'place', 'note'].map((key) => [key, fields.get(key)]));
  const encode = new TextEncoder();
  if (encode.encode(meta.note).length > 4096 || encode.encode(meta.place).length > 160)
    throw new Error('La nota o el nombre del lugar supera el tamaño permitido.');
  if (Boolean(fields.get('lat')) !== Boolean(fields.get('lon'))) throw new Error('Introduce las dos coordenadas o déjalas vacías.');
  for (const key of ['lat', 'lon']) meta[key] = fields.get(key) === '' ? null : Number(fields.get(key));
  return meta;
}

async function runPhoto(action) {
  if (busy) return;
  busy = true;
  const controls = ['save', 'save-all', 'skip', 'delete', 'close', 'cancel-delete', 'confirm-delete'];
  for (const id of controls) $('#' + id).disabled = true;
  try { await action(); }
  catch (error) { if (dialog.open) photoStatus(error.message); else status(error.message); }
  finally {
    busy = false;
    for (const id of controls) $('#' + id).disabled = false;
  }
}

form.onsubmit = (event) => {
  event.preventDefault();
  runPhoto(async () => {
    resetPlaceLookup();
    const meta = metaFields();
    if (current.optimized) {
      if (event.submitter?.id === 'save-all') {
        await uploadBatch(meta);
      } else {
        let message;
        try {
          await uploadPhoto(meta, current.optimized);
          message = 'Foto guardada.';
        } catch (error) {
          if (!error.duplicate) throw error;
          message = 'Foto duplicada omitida.';
        }
        await nextFile();
        status(message);
      }
    } else {
      await api('photos/' + current.id, { method: 'POST', data: { ...meta, rev: current.rev } });
      dialog.close();
      await refresh();
      status('Ficha guardada.');
    }
  });
};

$('#delete').onclick = () => {
  confirmDelete(true);
  $('#cancel-delete').focus();
};
$('#cancel-delete').onclick = () => {
  confirmDelete(false);
  $('#delete').focus();
};
$('#confirm-delete').onclick = () => runPhoto(async () => {
  photoStatus('Borrando foto…');
  await deletePhoto(current.id, current.rev);
  dialog.close();
  try { await refresh(); }
  catch { status('Foto borrada. Recarga el panel para actualizar la galería.'); return; }
  status('Foto borrada.');
});
$('#clear-gps').onclick = () => {
  for (const key of ['place', 'lat', 'lon']) form.elements[key].value = '';
  resetPlaceLookup();
};
$('#skip').onclick = () => runPhoto(nextFile);
$('#close').onclick = () => dialog.close();
dialog.oncancel = (event) => { if (busy) event.preventDefault(); };
dialog.onclose = () => { files = []; current = null; releasePreview(); resetPlaceLookup(); };
$('#files').onchange = (event) => {
  if (busy || deleteBusy) { event.target.value = ''; return; }
  files = Array.from(event.target.files);
  fileCount = files.length;
  event.target.value = '';
  if (fileCount) runPhoto(nextFile);
};
$('#prev').onclick = () => { offset -= pageSize; showPage().catch((error) => status(error.message)); };
$('#next').onclick = () => { offset += pageSize; showPage().catch((error) => status(error.message)); };

async function connected() {
  await api('time', { method: 'POST', data: { epoch: Math.floor(Date.now() / 1000) } }).catch(() => {});
  await refresh();
}

const wifiForm = $('#wifi');
const wifiStatus = $('#wifi-status');
let wifiPoll;
let wifiDeadline;

function updateWifi(device) {
  for (const [index, name] of [device.wifi1, device.wifi2].entries())
    wifiForm.elements.slot.options[index].textContent = name ? 'Red ' + (index + 1) + ' · ' + name : 'Red ' + (index + 1);
  if (device.wifiState === 1) wifiStatus.textContent = 'Intentando conectar…';
  if (device.wifiState === 2) {
    const link = document.createElement('a');
    link.href = 'http://' + device.ip + '/';
    link.textContent = device.ip;
    wifiStatus.replaceChildren('Conectado. Abre ', link, ' desde esa red.');
  }
  if (device.wifiState === 3) {
    const reason = device.wifiReason;
    wifiStatus.textContent = reason === 201 ? 'Red no encontrada. Comprueba el nombre y que emita en 2,4 GHz.'
      : [2, 15, 202, 204].includes(reason) ? 'Falló la autenticación. Revisa la contraseña y la seguridad de la red.'
      : 'No se obtuvo una IP. Comprueba la cobertura y la configuración del router.';
  }
}

async function pollWifi() {
  try {
    const device = await api('status');
    updateWifi(device);
    $('#ip').textContent = device.ip;
    if (device.wifiState !== 1) return;
  } catch {
    // El móvil puede perder el portal mientras el ESP32 cambia de canal.
  }
  if (Date.now() < wifiDeadline) wifiPoll = setTimeout(pollWifi, 1200);
  else wifiStatus.textContent = 'Consulta la IP en la pantalla. Si sigue en 192.168.4.1, vuelve a conectar el móvil a time_machine.';
}

wifiForm.onsubmit = async (event) => {
  event.preventDefault();
  clearTimeout(wifiPoll);
  try {
    const data = Object.fromEntries(new FormData(wifiForm));
    data.slot = Number(data.slot);
    data.storeOnly = event.submitter?.value === 'store';
    const message = await api('wifi', { method: 'POST', data });
    wifiForm.elements.password.value = '';
    if (data.storeOnly) {
      updateWifi(await api('status'));
      wifiStatus.textContent = message;
      return;
    }
    wifiStatus.textContent = 'Intentando conectar…';
    wifiDeadline = Date.now() + 30000;
    wifiPoll = setTimeout(pollWifi, 800);
  } catch (error) { wifiStatus.textContent = error.message; }
};

const avatarSize = 64;
const avatarInput = $('#profile-file');
function avatarCanvas() {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = avatarSize;
  return canvas;
}

async function showAvatar() {
  const response = await fetch('/api/profile', { cache: 'no-store' });
  if (!response.ok) return;
  const buffer = await response.arrayBuffer();
  if (buffer.byteLength !== avatarSize * avatarSize * 2) return;
  const view = new DataView(buffer);
  const canvas = avatarCanvas();
  const ctx = canvas.getContext('2d');
  const pixels = ctx.createImageData(avatarSize, avatarSize);
  for (let i = 0; i < buffer.byteLength / 2; i++) {
    const value = view.getUint16(i * 2, true);
    pixels.data.set([((value >> 11) & 31) * 255 / 31, ((value >> 5) & 63) * 255 / 63, (value & 31) * 255 / 31, 255], i * 4);
  }
  ctx.putImageData(pixels, 0, 0);
  let avatar = $('.profile-avatar');
  if (!avatar) {
    avatar = new Image();
    avatar.alt = '';
    avatar.className = 'profile-avatar';
    $('.brand').prepend(avatar);
  }
  avatar.src = canvas.toDataURL();
}

avatarInput.onchange = async () => {
  const file = avatarInput.files[0];
  if (!file) return;
  avatarInput.disabled = true;
  const url = URL.createObjectURL(file);
  const message = $('#profile-status');
  try {
    const image = new Image();
    image.src = url;
    await image.decode();
    const side = Math.min(image.naturalWidth, image.naturalHeight);
    const canvas = avatarCanvas();
    const ctx = canvas.getContext('2d', { alpha: false });
    ctx.drawImage(image, (image.naturalWidth-side)/2, (image.naturalHeight-side)/2, side, side, 0, 0, avatarSize, avatarSize);
    const pixels = ctx.getImageData(0, 0, avatarSize, avatarSize).data;
    const view = new DataView(new ArrayBuffer(avatarSize * avatarSize * 2));
    for (let i = 0; i < pixels.length; i += 4)
      view.setUint16(i/2, (pixels[i] >> 3) << 11 | (pixels[i+1] >> 2) << 5 | pixels[i+2] >> 3, true);
    message.textContent = await api('profile', { method: 'POST', body: view.buffer });
    await showAvatar();
  } catch (error) { message.textContent = error.message || 'No se pudo abrir la imagen.'; }
  finally {
    URL.revokeObjectURL(url);
    avatarInput.value = '';
    avatarInput.disabled = false;
  }
};

connected().catch((error) => status(error.message));
showAvatar().catch(() => {});
