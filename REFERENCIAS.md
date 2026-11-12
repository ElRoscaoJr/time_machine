# Referencias y licencias

## Wireless-Tag WT32-SC01 Plus

Se examinó el PDF V1.5 EN del módulo ZX3D50CE08S-USRC-4832. La figura 1 identifica el USB-C, la ranura SD, la expansión y el resto de los conectores. La figura 2, en la página 11 del PDF, aporta las cotas exteriores, el área visible, los cuatro tetones, su paso y el taladro de Ø2,3 mm. La boca USB queda arriba y la SD abajo cuando se monta la pantalla con puertos hacia la izquierda, vista desde delante. La posición exacta de sus bocas y la geometría de los enchufes no están acotadas allí. Las posiciones provisionales de CAD se estimaron sobre el dibujo trasero. [PDF original](https://docs.makehub.tw/wt32-sc01plus/WT32-SC01%2BPLUS%2BDatasheet-V1.5%2BEN.pdf)

## Printables

Se descargaron e inspeccionaron los dos STL publicados por **marcusone**. La API de la ficha declara **Creative Commons Attribution–NonCommercial (CC BY-NC)**, sin versión en los metadatos consultados. `Back.stl` mide 64,4 × 16,2 × 96,4 mm y `Base.stl` mide 67,2 × 64,0 × 41,2 mm en sus ejes de archivo. Son mallas cerradas. El autor indica una inclinación de aproximadamente 18° y soportes en la base para los pasos de bridas. La ficha lista esos dos STL y no aporta una fuente paramétrica editable. La comparación se usó como referencia de formato de sobremesa y de la separación entre respaldo y apoyo. [Ficha del modelo](https://www.printables.com/model/381026-wt32-sc01-plus-desk-case) · [Back.stl](https://files.printables.com/media/prints/381026/stls/3196957_85bb80d6-22fd-4644-a694-2a14d0fe3c25/wt32-sc01-plus-desk-case-back.stl) · [Base.stl](https://files.printables.com/media/prints/381026/stls/3201963_6ad42d15-c2c6-4a2c-b8a9-2941d9899e1b/wt32-sc01-plus-desk-case-base.stl)

## Pit Claw

Se descargó el repositorio en el commit `790571378610265dc3f8e34556661a542285313a`. Hay dos fuentes OpenSCAD, `bbq-case.scad` y `bbq-fan-assembly.scad`, y cinco STL. Para la pantalla se inspeccionaron `front-bezel.stl` (66 × 98 × 10,5 mm), `rear-shell.stl` (66 × 98 × 27,3 mm) y `kickstand.stl` (25 × 66,17 × 5,5 mm). `blower-housing.stl` y `uds-pipe-adapter.stl` corresponden al ventilador.

La fuente del frontal declara taladros M3 con un retiro aproximado de 3 mm desde los bordes de 60 × 92 mm. Eso da un paso de 54 × 86 mm en sus ejes, distinto del 51,39 × 75,14 mm de la ficha, que además indica Ø2,3 mm. El corte nominal de USB es 10 × 4 mm y se ubica a partir del borde interior de la tapa. La fuente no incluye abertura dedicada para microSD. La posición y el margen del cable se definieron desde el módulo en Entretiempo. El repositorio no incluye un archivo de licencia en la revisión descargada y el README indica que está pendiente. [Fuente y documentación](https://github.com/MrMatt57/pitclaw/tree/790571378610265dc3f8e34556661a542285313a/enclosure) · [STL publicados](https://github.com/MrMatt57/pitclaw/tree/790571378610265dc3f8e34556661a542285313a/enclosure/stl) · [Licencia declarada](https://github.com/MrMatt57/pitclaw/blob/790571378610265dc3f8e34556661a542285313a/README.md#license)

## Segunda comprobación del montaje de fábrica

- **MakerWorld, pixelwave.** El perfil 292768 de la carcasa magnética para la Plus lista M2 × 6, M3 × 10 e insertos M2/M3. Las fotos de montaje muestran el módulo completo y un frontal independiente con tornillos. La ficha y los renders respaldan esa arquitectura de montaje. El servicio de descarga pidió iniciar sesión para acceder a la malla del perfil, así que su geometría interna no pudo medirse. La licencia es Standard Digital File License. [Ficha y fotos de MakerWorld](https://makerworld.com/es/models/132984-wt32-sc01-plus-magnetic-case#profileId-292768)
- **Printables, eried.** Se descargó `sc01_plus_wallmount.stl`, un alojamiento a presión para el módulo completo. En secciones medidas de la malla, la zona de asiento interior se abre a 93,6 × 60,5 mm y se estrecha hasta 92,6 × 59,5 mm en el labio. Esto corrobora el uso del contorno del marco de fábrica como referencia de montaje. Entretiempo usa guías de 93,2 × 61,2 mm y apriete regulable desde atrás. La ficha declara CC BY 4.0. [Ficha de Printables](https://www.printables.com/model/400051-sc01-plus-esp32-touchscreen-walldesk-enclosuremoun)

## Encoder y tipografía

- **Encoder.** La imagen de cotas del vendedor muestra 15 × 12,5 mm para el cuerpo, 30 mm de alto total, 20 mm desde el hombro hasta la punta y un mando de Ø15 × 17 mm. Se aprecia eje estriado y ranurado, pulsador y cinco pines. No indica diámetro del eje ni especificación de la rosca. [Ficha de Amazon](https://www.amazon.es/dp/B0FB97T2YL)
- **Grabado.** Se usaron contornos de GNU FreeSans, publicada bajo GPL v3 o posterior con excepción tipográfica. El archivo de fuente tipográfica se obtiene por separado para regenerar el CAD. [GNU FreeFont](https://www.gnu.org/software/freefont/license.html)

Las piezas de Entretiempo se construyeron en CAD a partir de las cotas y envolventes documentadas. El paquete contiene los modelos propios y las vistas del CAD.
