"""Genera el frontal para madera y la manivela con el Python de FreeCAD."""
import argparse
from math import cos, radians, sin
from pathlib import Path

import FreeCAD as App
import MeshPart
import Part

CARPETA = Path(__file__).resolve().parent
ORIGINALES = CARPETA.parent / "fabricacion" / "step"
EJE_Z = App.Vector(0, 0, 1)
ESPESOR = 3.2
PROFUNDIDAD_GRABADO = 0.65
ALARGUE_MANIVELA = 14
PANTALLA_Y = -5
PASO_PANTALLA_X = 75.5
PASO_PANTALLA_Y = 51.5
BORDE_A_ANCLAJE = 10.34
FONDO_TETONES = 10.0
JUNTA_COMPRIMIDA = 0.8
ALTURA_APOYO = ESPESOR + JUNTA_COMPRIMIDA + FONDO_TETONES
ESPESOR_PLETINA = 3.2
CORTE_ANCHO = 116
CORTE_ALTO = 76
TORNILLOS = [(x, y) for x in (-67, 67) for y in (-34, 34)]


def caja(x, y, z, ancho, alto, fondo):
    return Part.makeBox(ancho, alto, fondo, App.Vector(x, y, z))


def perfil(ancho, alto, radio, y, z):
    x = ancho / 2 - radio
    h = alto / 2 - radio
    arcos = []
    for cx, cy, inicio in ((x, y + h, 0), (-x, y + h, 90),
                           (-x, y - h, 180), (x, y - h, 270)):
        puntos = [App.Vector(cx + radio * cos(radians(angulo)),
                            cy + radio * sin(radians(angulo)), z)
                  for angulo in (inicio, inicio + 45, inicio + 90)]
        arcos.append(Part.Arc(*puntos).toShape())
    bordes = []
    for actual, siguiente in zip(arcos, arcos[1:] + arcos[:1]):
        bordes.extend((actual, Part.makeLine(actual.Vertexes[-1].Point,
                                             siguiente.Vertexes[0].Point)))
    return Part.Wire(bordes)


def prisma(ancho, alto, radio, y, z, fondo):
    return Part.Face(perfil(ancho, alto, radio, y, z)).extrude(EJE_Z * fondo)


def original(nombre):
    pieza = Part.Shape()
    pieza.read(str(ORIGINALES / f"{nombre}.step"))
    return pieza


def grabado(font, texto, y):
    letras = Part.makeWireString(texto, str(font.parent) + "/", font.name, 7)
    cara = Part.makeFace([trazo for letra in letras for trazo in letra],
                         "Part::FaceMakerBullseye")
    cara.translate(-cara.BoundBox.Center)
    cara.rotate(App.Vector(), App.Vector(1, 0, 0), 180)
    cara.translate(App.Vector(0, y, -0.03))
    return cara.extrude(EJE_Z * (PROFUNDIDAD_GRABADO + 0.03))


def ranura(ancho, alto, radio, y, trazo):
    exterior = prisma(ancho, alto, radio, y, -0.03, PROFUNDIDAD_GRABADO + 0.03)
    interior = prisma(ancho - 2 * trazo, alto - 2 * trazo, radio - trazo,
                      y, -0.06, PROFUNDIDAD_GRABADO + 0.09)
    return exterior.cut(interior)


def anclaje(lado, inferior=False):
    """Devuelve el agujero del marco negro y el poste impreso, vistos de frente."""
    signo = 1 if inferior else -1
    x = -46 + BORDE_A_ANCLAJE + lado * PASO_PANTALLA_X
    y = signo * PASO_PANTALLA_Y / 2
    poste_x, desvio = ((-51, 6), (52, 0))[lado]
    return (x, PANTALLA_Y + y), (poste_x, PANTALLA_Y + y + signo * desvio)


def pletina(lado, inferior=False):
    (x, y), (poste_x, poste_y) = anclaje(lado, inferior)
    puente = prisma(abs(x - poste_x) + 8, 8, 3.99, y, 0, ESPESOR_PLETINA)
    puente.translate(App.Vector((x + poste_x) / 2, 0, 0))
    if lado == 0:
        # El codo va hacia el canto más próximo y deja libre el lado de los puertos.
        codo = prisma(8, abs(y - poste_y) + 8, 3.99, (y + poste_y) / 2, 0, ESPESOR_PLETINA)
        codo.translate(App.Vector(poste_x, 0, 0))
        puente = puente.fuse(codo)
        borde = y - 6.5 if inferior else y + 2.5
        puente = puente.cut(caja(poste_x - 4.1, borde, -0.03, x - poste_x + 8.2,
                                 4, ESPESOR_PLETINA + 0.06))
    paso = Part.makeCylinder(1.725, ESPESOR_PLETINA + 0.06, App.Vector(poste_x, poste_y, -0.03))
    ranura = prisma(5, 3.2, 1.59, y, -0.03, ESPESOR_PLETINA + 0.06)
    ranura.translate(App.Vector(x, 0, 0))
    puente = puente.cut([paso, ranura]).removeSplitter()
    puente.translate(App.Vector(0, 0, ALTURA_APOYO))
    return puente


def frontal(font):
    placa = Part.makeLoft(
        [perfil(155.6, 101.6, 6.8, 0, 0), perfil(158, 104, 8, 0, 1.2)], True
    )
    pieza = placa.fuse(prisma(158, 104, 8, 0, 1.2, ESPESOR - 1.2))
    # Las guías bajas centran el marco con 0,6 mm libres por canto.
    for signo in (-1, 1):
        guia = prisma(1.8, 16, 0.8, PANTALLA_Y, ESPESOR - 0.03, 2.43)
        guia.translate(App.Vector(signo * 47.5, 0, 0))
        pieza = pieza.fuse(guia)
        for x in (-24, 24):
            guia = prisma(12, 1.8, 0.8, PANTALLA_Y + signo * 31.5, ESPESOR - 0.03, 2.43)
            guia.translate(App.Vector(x, 0, 0))
            pieza = pieza.fuse(guia)

    for lado in (0, 1):
        for inferior in (False, True):
            _, (x, y) = anclaje(lado, inferior)
            poste = Part.makeCylinder(4, ALTURA_APOYO - ESPESOR + 0.03,
                                      App.Vector(x, y, ESPESOR - 0.03))
            piloto = Part.makeCylinder(1.325, 8, App.Vector(x, y, ALTURA_APOYO - 7.8))
            pieza = pieza.fuse(poste.cut(piloto))
    entrada = Part.makeLoft(
        [perfil(78.6, 54.2, 2.1, PANTALLA_Y, -0.03),
         perfil(76, 51.6, 0.8, PANTALLA_Y, 1.3)], True
    )
    ventana = prisma(76, 51.6, 0.8, PANTALLA_Y, 1.3, 2)
    pieza = pieza.cut(entrada.fuse(ventana))
    for x, y in TORNILLOS:
        paso = Part.makeCylinder(1.65, ESPESOR + 0.06, App.Vector(x, y, -0.03))
        asiento = Part.makeCone(3.15, 1.65, 1.5, App.Vector(x, y, 0))
        pieza = pieza.cut(paso.fuse(asiento))
    ornamentos = [
        grabado(font, "DANIELA’S TIME MACHINE", 37),
        ranura(150, 96, 6.5, 0, 1.1),
        ranura(90, 63, 4.5, PANTALLA_Y, 1.2),
    ]
    for x in (-54, 54):
        for y in (-23, -14, -5, 4, 13):
            ancho = 9 if y == PANTALLA_Y else 5
            marca = prisma(ancho, 1.4, 0.65, y, -0.03, PROFUNDIDAD_GRABADO + 0.03)
            marca.translate(App.Vector(x, 0, 0))
            ornamentos.append(marca)
    pieza = pieza.cut(Part.makeCompound(ornamentos))
    return pieza.removeSplitter()


def manivela():
    # Mantiene el acople de Ø15,55 y la abrazadera M3 originales.
    brazo = original("06-manivela").common(caja(-25, -13, 0, 50, 26, 9))
    acople = brazo.common(caja(-25, -13, 0, 25.01, 26, 9))
    extremo = brazo.common(caja(-0.01, -13, 0, 25.01, 26, 9))
    extremo.translate(App.Vector(ALARGUE_MANIVELA, 0, 0))
    puente = caja(0, -12, 0, ALARGUE_MANIVELA, 24, 9)
    brazo = acople.fuse([puente, extremo])

    # El alojamiento abre hacia la caja. El techo interior sube a 45°
    # hasta dejar un puente corto que se puede imprimir sin soportes.
    eje_x = -9
    radio_acople = 7.775
    corona = Part.makeCylinder(10.5, 7.55, App.Vector(eje_x, 0, 8.95))
    bisel = Part.makeCone(10.5, 9.5, 1, App.Vector(eje_x, 0, 16.5))
    entrada = Part.makeCylinder(radio_acople, 1.1, App.Vector(eje_x, 0, 8.9))
    techo = Part.makeCone(radio_acople, 2.5, 5.275, App.Vector(eje_x, 0, 10))
    corte_abrazadera = caja(-25, -0.75, 8.9, 16, 1.5, 1.1)
    tapa_acople = corona.fuse(bisel).cut([entrada, techo, corte_abrazadera])

    perfiles = [(8.98, 17), (11.5, 19), (15, 18), (19, 16.5),
                (23, 17.5), (25, 18), (27, 15)]
    pomo = Part.makeLoft(
        [Part.Wire(Part.makeCircle(d / 2, App.Vector(11 + ALARGUE_MANIVELA, 0, z)))
         for z, d in perfiles],
        True,
    )
    return brazo.fuse([tapa_acople, pomo]).removeSplitter()


def exportar(pieza, nombre):
    assert pieza.isValid() and len(pieza.Solids) == 1, nombre
    pieza.exportStep(str(CARPETA / f"{nombre}.step"))
    malla = MeshPart.meshFromShape(Shape=pieza, LinearDeflection=0.025,
                                  AngularDeflection=0.10, Relative=False)
    assert malla.isSolid(), nombre
    malla.write(str(CARPETA / f"{nombre}.stl"))
    print(f"{nombre}: {pieza.BoundBox}")


def exportar_frontal(font):
    placa = frontal(font)
    exportar(placa, "frontal-madera")
    conjunto = [placa]
    for nombre, lado in (("pletina-puertos", 0), ("pletina-opuesta", 1)):
        superior = pletina(lado)
        conjunto.extend((superior, pletina(lado, inferior=True)))
        centro = superior.BoundBox.Center
        superior = superior.translated(App.Vector(-centro.x, -centro.y, -ALTURA_APOYO))
        exportar(superior, nombre)
    Part.makeCompound(conjunto).exportStep(str(CARPETA / "montaje-frontal.step"))


def plantilla():
    agujeros = "\n".join(
        f'<circle cx="{105 + x}" cy="{75 + y}" r="1.65"/>'
        for x, y in TORNILLOS
    )
    (CARPETA / "plantilla-corte-1a1.svg").write_text(f'''<svg xmlns="http://www.w3.org/2000/svg"
    width="210mm" height="148mm" viewBox="0 0 210 148">
  <rect width="210" height="148" fill="white"/>
  <g font-family="sans-serif" fill="#151515">
    <text x="105" y="12" font-size="5" text-anchor="middle">Daniela’s Time Machine</text>
    <text x="105" y="18" font-size="3.5" text-anchor="middle">Plantilla 1:1 · imprimir al 100 %</text>
    <text x="105" y="71" font-size="5" text-anchor="middle">Recortar {CORTE_ANCHO} × {CORTE_ALTO} mm</text>
    <text x="105" y="78" font-size="3.5" text-anchor="middle">Taladrar los cuatro centros laterales</text>
    <text x="105" y="91" font-size="3.5" text-anchor="middle">Puertos a la izquierda vistos desde fuera</text>
    <text x="105" y="142" font-size="3.5" text-anchor="middle">Comprobar que esta línea mide 50 mm</text>
  </g>
  <g fill="none" stroke="#171717" stroke-width=".25">
    <rect x="26" y="23" width="158" height="104" rx="8" stroke-dasharray="2 1"/>
    <rect x="{105 - CORTE_ANCHO / 2}" y="{75 + PANTALLA_Y - CORTE_ALTO / 2}"
          width="{CORTE_ANCHO}" height="{CORTE_ALTO}"/>
    {agujeros}
    <path d="M80 134v4m0-2h50m0-2v4"/>
  </g>
</svg>
''', encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fuente", type=Path, help="Ruta a LiberationSans-Bold.ttf")
    opciones = parser.parse_args()
    exportar_frontal(opciones.fuente.resolve())
    exportar(manivela(), "manivela-lateral")
    plantilla()
