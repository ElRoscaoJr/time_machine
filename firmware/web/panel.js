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
  if (!device.sd) summary.push('Inserta una microSD FAT32 y reinicia');
  $('#summary').textContent = summary.join(' · ');
  $('#files').disabled = !device.sd;
  $('#upload-control').setAttribute('aria-disabled', String(!device.sd));
  if (device.ap) $('#network').open = true;
  updateWifi(device);
  await showPage();
}

async function showPage() {
  const page = await api('photos?offset=' + offset);
  if (offset && !page.items.length) {
    offset = Math.max(0, Math.ceil(page.total / pageSize) - 1) * pageSize;
    return showPage();
  }
  $('#photos').replaceChildren(...page.items.map(({ id, date }) => {
    const button = document.createElement('button');
    const image = document.createElement('img');
    const label = document.createElement('span');
    button.className = 'tile';
    image.loading = 'lazy';
    image.alt = '';
    image.src = '/api/photos/' + id + '/image';
    label.textContent = date ? date.split('-').reverse().join('/') : 'Sin fecha';
    button.append(image, label);
    button.onclick = () => openExisting(id).catch((error) => status(error.message));
    return button;
  }));
  $('#empty').hidden = !!page.total || $('#files').disabled;
  $('#pages').hidden = page.total <= pageSize;
  $('#prev').disabled = offset === 0;
  $('#next').disabled = offset + pageSize >= page.total;
  $('#page').textContent = (offset / pageSize + 1) + ' / ' + Math.ceil(page.total / pageSize);
}

function fill(meta) {
  for (const name of ['date', 'place', 'note', 'lat', 'lon']) form.elements[name].value = meta[name] ?? '';
}

function showDialog(title, url, upload) {
  $('#dialog-title').textContent = title;
  const preview = new Image();
  preview.alt = 'Vista previa de la foto';
  preview.src = url;
  $('#preview').replaceChildren(preview);
  $('#delete').hidden = upload;
  $('#skip').hidden = !upload;
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

async function nextFile() {
  releasePreview();
  while (files.length) {
    const file = files.shift();
    try {
      const { meta } = await processPhoto({ file });
      previewUrl = URL.createObjectURL(file);
      const image = new Image();
      image.src = previewUrl;
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
      releasePreview();
      const optimized = new Blob([jpeg], { type: 'image/jpeg' });
      previewUrl = URL.createObjectURL(optimized);
      current = { optimized };
      fill(meta);
      showDialog(file.name, previewUrl, true);
      return;
    } catch {
      releasePreview();
      status('No se pudo abrir «' + file.name + '». Si es HEIC, prueba a exportarla como JPEG.');
    }
  }
  current = null;
  if (dialog.open) dialog.close();
  await refresh();
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
  $('#save').disabled = $('#skip').disabled = $('#delete').disabled = $('#close').disabled = true;
  try { await action(); }
  catch (error) { if (dialog.open) photoStatus(error.message); else status(error.message); }
  finally {
    busy = false;
    $('#save').disabled = $('#skip').disabled = $('#delete').disabled = $('#close').disabled = false;
  }
}

form.onsubmit = (event) => {
  event.preventDefault();
  runPhoto(async () => {
    const meta = metaFields();
    if (current.optimized) {
      const encoded = new TextEncoder().encode(JSON.stringify(meta));
      await api('photos', {
        method: 'POST', body: new Blob([encoded, current.optimized]),
        headers: { 'Content-Type': 'application/octet-stream', 'X-Meta-Length': String(encoded.length) },
      });
      status('Foto guardada.');
      await nextFile();
    } else {
      await api('photos/' + current.id, { method: 'POST', data: { ...meta, rev: current.rev } });
      dialog.close();
      await refresh();
      status('Ficha guardada.');
    }
  });
};

$('#delete').onclick = () => runPhoto(async () => {
  if (!confirm('¿Borrar esta foto y su nota de la microSD?')) return;
  await api('photos/' + current.id, { method: 'DELETE', data: { rev: current.rev } });
  dialog.close();
  await refresh();
  status('Foto borrada.');
});
$('#clear-gps').onclick = () => {
  for (const key of ['place', 'lat', 'lon']) form.elements[key].value = '';
};
$('#skip').onclick = () => runPhoto(nextFile);
$('#close').onclick = () => dialog.close();
dialog.oncancel = (event) => { if (busy) event.preventDefault(); };
dialog.onclose = () => { files = []; current = null; releasePreview(); };
$('#files').onchange = (event) => {
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
