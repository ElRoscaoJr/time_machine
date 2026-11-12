"""Entretiempo V2. CAD en milímetros. Python 3.13 y build123d 0.13."""
from dataclasses import dataclass
from pathlib import Path

from build123d import (
    Align, Box, Cone, Compound, Cylinder, Plane, Pos, RectangleRounded, Rot, Text,
    export_step, export_stl, extrude, loft, mirror,
)

MIN = Align.MIN
CENTER = Align.CENTER
EPS = 0.03


@dataclass
class Medidas:
    # Vista frontal: X a la derecha, Y hacia abajo, Z hacia el interior.
    ancho: float = 172
    alto: float = 82
    profundidad: float = 64
    pared: float = 3.2
    radio: float = 8
    frontal: float = 3.2
    tapa: float = 3
    paso_m3: float = 3.45
    piloto_m3: float = 2.65

    pantalla_x: float = -25
    pantalla_w: float = 92
    pantalla_h: float = 60
    pantalla_holgura: float = 0.60  # Por canto
    ventana_w: float = 76
    ventana_h: float = 51.6
    junta_comprimida: float = 0.8
    fondo_tetones: float = 10.8
    pad_trasero_comprimido: float = 1.5
    paso_x: float = 75.14
    paso_y: float = 51.39
    agujero_izq: float = 10.34
    rail_w: float = 5.6
    rail_t: float = 3.2
    rail_poste_z: float = 12
    rail_desvio: float = 6

    acceso_h: float = 52
    acceso_x: float = -39
    bateria_w: float = 78
    modulos_w: float = 28
    bandeja_h: float = 44
    bandeja_d: float = 22
    bandeja_x: float = -35
    bandeja_pared: float = 2

    encoder_x: float = 51
    encoder_placa: float = 3
    casquillo_d: float = 7.4
    casquillo_piloto: float = 5.8
    mando_d: float = 15
    mando_holgura: float = 0.55
    manivela_radio: float = 20
    manivela_t: float = 9
    manivela_z: float = -6  # Presentación nominal del montaje
    pomo_d: float = 18.5
    pomo_h: float = 16

    @property
    def cristal_z(self):
        return self.frontal + self.junta_comprimida

    @property
    def rail_z(self):
        return self.cristal_z + self.fondo_tetones + self.pad_trasero_comprimido

    @property
    def tetones_x(self):
        izq = self.pantalla_x - self.pantalla_w / 2 + self.agujero_izq
        return izq, izq + self.paso_x

    @property
    def rail_extremos(self):
        return self.pantalla_x - self.pantalla_w / 2 - 5, self.pantalla_x + self.pantalla_w / 2 + 6

    def rail_anclajes(self, y):
        izq, der = self.rail_extremos
        return (izq, y - self.rail_desvio), (der, y)

    @property
    def tornillos_tapa(self):
        return [(x, y) for x in (-self.ancho / 2 + 20, self.ancho / 2 - 11)
                for y in (-self.alto / 2 + 5.2, self.alto / 2 - 5.2)]

    @property
    def tornillos_frente(self):
        return [(x, y) for x in (-34, 36) for y in (-self.alto / 2 + 6, self.alto / 2 - 6)]


def caja(x, y, z, w, h, d):
    return Pos(x, y, z) * Box(w, h, d, align=(MIN, MIN, MIN))


def cilindro(x, y, z, diametro, d):
    return Pos(x, y, z) * Cylinder(diametro / 2, d, align=(CENTER, CENTER, MIN))


def rectangulo(w, h, r, z=0, x=0, y=0):
    return Pos(x, y, z) * RectangleRounded(w, h, r)


def prisma(w, h, r, d, z=0, x=0, y=0):
    return extrude(rectangulo(w, h, r, z, x, y), d)


def validar_parametros(m):
    assert m.encoder_x - m.manivela_radio - 14 > m.pantalla_x + m.ventana_w / 2
    assert m.alto / 2 > m.manivela_radio + 14
    assert m.bandeja_x - m.bandeja_pared > m.acceso_x
    assert m.bandeja_x + m.bateria_w + m.modulos_w + 2 * m.bandeja_pared < m.ancho / 2 - m.pared
    assert m.bandeja_h / 2 + m.bandeja_pared < m.alto / 2 - m.pared
    assert m.profundidad - m.bandeja_d > m.rail_z + m.rail_t + 10


def ventana(m):
    return loft([
        rectangulo(m.ventana_w + 2.6, m.ventana_h + 2.6, 2.1, -EPS, m.pantalla_x),
        rectangulo(m.ventana_w, m.ventana_h, 0.8, 1.3, m.pantalla_x),
    ], ruled=True) + prisma(m.ventana_w, m.ventana_h, 0.8, m.frontal - 1.3 + 2 * EPS, 1.3, m.pantalla_x)


def guias_pantalla(m, pieza):
    """Guías bajas exteriores al marco original, con paso libre de los puertos."""
    z = m.frontal - EPS
    alto = 2.4 + EPS
    lado_x = m.pantalla_w / 2 + m.pantalla_holgura + 0.9
    lado_y = m.pantalla_h / 2 + m.pantalla_holgura + 0.9
    for signo in (-1, 1):
        pieza += prisma(1.8, 16, 0.8, alto, z, m.pantalla_x + signo * lado_x)
        for x in (m.pantalla_x - 24, m.pantalla_x + 24):
            pieza += prisma(12, 1.8, 0.8, alto, z, x, signo * lado_y)
    return pieza


def frontal(m):
    chaflan = 1.5
    pieza = loft([
        rectangulo(m.ancho - 2 * chaflan, m.alto - 2 * chaflan, m.radio - chaflan),
        rectangulo(m.ancho, m.alto, m.radio, chaflan),
    ], ruled=True) + prisma(m.ancho, m.alto, m.radio, m.frontal - chaflan, chaflan)
    pieza = guias_pantalla(m, pieza)
    for x, y in m.rail_anclajes(-m.paso_y / 2):
        for yy in (-abs(y), abs(y)):
            pieza += cilindro(x, yy, m.frontal - EPS, 8, m.rail_poste_z - m.frontal + EPS)
            pieza -= cilindro(x, yy, m.rail_poste_z - 7.8, m.piloto_m3, 8)
    for x, y in m.tornillos_frente:
        pieza += cilindro(x, y, m.frontal - EPS, 8.2, 8.1 - m.frontal + EPS)
        pieza -= cilindro(x, y, 2.3, m.piloto_m3, 6)
    for y in (-19, 19):
        pieza += cilindro(m.encoder_x, y, m.frontal - EPS, 8, 2.8 + EPS)
        pieza -= cilindro(m.encoder_x, y, 1.8, m.piloto_m3, 4.3)
    pieza -= ventana(m)
    pieza -= cilindro(m.encoder_x, 0, -EPS, 22, m.frontal + 2 * EPS)

    letras = Pos(m.pantalla_x, 33, -EPS) * Rot(0, 180, 180) * Text(
        "E N T R E T I E M P O", 4.7, font="FreeSans")
    pieza -= extrude(letras, 0.65, dir=(0, 0, 1))
    for x in (-61, 11):
        pieza -= prisma(7, 0.9, 0.4, 0.68, -EPS, x, 33)
    for angulo in range(0, 360, 30):
        marca = prisma(0.85, 2.8 if angulo % 90 == 0 else 1.65, 0.39,
                       0.65, -EPS, y=-32)
        pieza -= Pos(m.encoder_x, 0, 0) * Rot(0, 0, angulo) * marca
    return pieza


def carcasa(m):
    z = m.frontal
    d = m.profundidad - z
    pieza = prisma(m.ancho, m.alto, m.radio, d, z)
    pieza -= prisma(m.ancho - 2 * m.pared, m.alto - 2 * m.pared, m.radio - m.pared,
                    d + 2 * EPS, z - EPS)
    pieza -= caja(-m.ancho / 2 - 1, -m.acceso_h / 2, z - EPS,
                  m.pared + 2, m.acceso_h, d + 2 * EPS)
    for x, y in m.tornillos_frente:
        # Casquillo alrededor del poste frontal. El embudo evita un techo plano.
        tope = m.alto / 2
        limite = m.pantalla_h / 2 + m.pantalla_holgura + 0.15
        banda_y = limite if y > 0 else -tope
        casquillo = cilindro(x, y, z, 14, 12.4 - z)
        casquillo &= caja(x - 8, banda_y, z - EPS, 16, tope - limite, 10)
        pieza += casquillo
        pieza -= cilindro(x, y, z - EPS, 9, 8.35 - z + EPS)
        pieza -= Pos(x, y, 8.35 - EPS) * Cone(4.5, m.paso_m3 / 2, 2.8, align=(CENTER, CENTER, MIN))
        pieza -= cilindro(x, y, 11.1, m.paso_m3, 1.5)
    for x, y in m.tornillos_tapa:
        # Columna continua y solidaria a pared. El tornillo forma su rosca.
        pieza += cilindro(x, y, z, 10, d)
        pieza -= cilindro(x, y, m.profundidad - 8, m.piloto_m3, 8.1)
    return pieza


def tapa(m):
    z = m.profundidad
    pieza = prisma(m.ancho, m.alto, m.radio, m.tapa, z)
    pieza -= caja(-m.ancho / 2 - 1, -m.acceso_h / 2, z - EPS,
                  m.acceso_x + m.ancho / 2 + 1, m.acceso_h, m.tapa + 2 * EPS)
    p = m.bandeja_pared
    x = m.bandeja_x
    h = m.bandeja_h
    fondo = z - m.bandeja_d
    y = -h / 2
    ancho = m.bateria_w + m.modulos_w + 3 * p
    marco = caja(x - p, y - p, fondo, ancho, h + 2 * p, m.bandeja_d + EPS)
    marco -= caja(x, y, fondo - EPS, m.bateria_w, h, m.bandeja_d + EPS)
    marco -= caja(x + m.bateria_w + p, y, fondo - EPS, m.modulos_w, h, m.bandeja_d + EPS)
    marco -= caja(x + m.bateria_w - EPS, -5, fondo - EPS, p + 2 * EPS, 10, 8)
    marco -= caja(x + m.bateria_w + p + 7, y - p - EPS, fondo - EPS, 10, p + 2 * EPS, 8)
    pieza += marco
    for xx, yy in m.tornillos_tapa:
        pieza -= cilindro(xx, yy, z - EPS, m.paso_m3, m.tapa + 2 * EPS)
    return pieza


def rail(m):
    y = -m.paso_y / 2
    izq, der = m.rail_extremos
    z = m.rail_z
    pieza = prisma(der - izq + 7.6, m.rail_w, 1.2, m.rail_t, z, (izq + der) / 2, y)
    for x, yy in m.rail_anclajes(y):
        pieza += cilindro(x, yy, z, 7.6, m.rail_t)
        if yy != y:
            pieza += prisma(7.6, abs(yy - y) + 5, 2.4, m.rail_t, z, x, (yy + y) / 2)
        pieza -= cilindro(x, yy, z - EPS, m.paso_m3, m.rail_t + 2 * EPS)
    # Slots de inspección y de fijación opcional en los tetones documentados.
    for x in m.tetones_x:
        pieza -= prisma(5.4, 2.8, 1.38, m.rail_t + 2 * EPS, z - EPS, x, y)
    return pieza


def soporte_encoder(m, paso, abrazadera=False):
    z = m.frontal + 2.8
    x = m.encoder_x
    pieza = prisma(31, 46, 6, m.encoder_placa, z, x)
    fondo = 2.4 if abrazadera else 0
    if abrazadera:
        pieza += prisma(21, 16, 3.5, fondo + m.encoder_placa, z - fondo, x + 0.4)
        pieza -= caja(x, -0.65, z - fondo - EPS, 17, 1.3, fondo + m.encoder_placa + 2 * EPS)
        tornillo = Pos(x + 7.25, -9, z + 0.2) * Rot(-90, 0, 0)
        pieza -= tornillo * cilindro(0, 0, 0, m.paso_m3, 18)
    pieza -= cilindro(x, 0, z - fondo - EPS, paso, fondo + m.encoder_placa + 2 * EPS)
    for y in (-19, 19):
        pieza -= cilindro(x, y, z - EPS, m.paso_m3, m.encoder_placa + 2 * EPS)
    return pieza


def manivela(m):
    # Se modela con el lado hacia la carcasa apoyado. El acople pasa por
    # fuera del mando original, que conserva su unión estriada con el eje.
    r = m.manivela_radio
    t = m.manivela_t
    pieza = prisma(r + 25, 24, 10.5, t, x=r / 2)
    pieza += prisma(8, 14, 1.5, t, x=-10.5)
    pieza -= cilindro(0, 0, -EPS, m.mando_d + m.mando_holgura, t + 2 * EPS)
    pieza -= caja(-16, -0.75, -EPS, 16, 1.5, t + 2 * EPS)
    tornillo = Pos(-10.9, -9, t / 2) * Rot(-90, 0, 0)
    pieza -= tornillo * cilindro(0, 0, 0, m.paso_m3, 18)
    pomo = [
        (t - EPS, m.pomo_d - 2), (t + 3, m.pomo_d),
        (t + m.pomo_h - 2, m.pomo_d - 1.5), (t + m.pomo_h, m.pomo_d - 5),
    ]
    for (a, da), (b, db) in zip(pomo, pomo[1:]):
        pieza += Pos(r, 0, a) * Cone(da / 2, db / 2, b - a, align=(CENTER, CENTER, MIN))
    return Pos(m.encoder_x, 0, m.manivela_z) * Rot(180, 0, 0) * pieza


def plantilla_pantalla(m):
    z = m.frontal - 1.2
    exterior = prisma(m.pantalla_w + 7, m.pantalla_h + 7, 5, 1.2, z, m.pantalla_x)
    interior = prisma(m.ventana_w, m.ventana_h, 0.8, 1.3, z - EPS, m.pantalla_x)
    return guias_pantalla(m, exterior - interior)


def plantilla_mando(m):
    pieza = prisma(65, 22, 4.5, 2.4)
    centro = m.mando_d + m.mando_holgura
    for n, x in enumerate((-21, 0, 21)):
        pieza -= cilindro(x, 0, -EPS, centro + 0.30 * (n - 1), 2.5)
        for marca in range(n + 1):
            pieza -= cilindro(x + (marca - n / 2) * 2.2, -9, 1.85, 1.2, 0.6)
    return pieza


def piezas(m):
    validar_parametros(m)
    return {
        "01-frontal": frontal(m),
        "02-cuerpo": carcasa(m),
        "03-tapa-bandeja": tapa(m),
        "04-rail-pantalla": rail(m),
        "05-soporte-encoder": soporte_encoder(m, m.casquillo_d),
        "06-manivela": manivela(m),
        "07-plantilla-pantalla": plantilla_pantalla(m),
        "08-plantilla-mando": plantilla_mando(m),
        "09-soporte-encoder-piloto": soporte_encoder(m, m.casquillo_piloto),
        "10-soporte-encoder-abrazadera": soporte_encoder(m, m.casquillo_d, abrazadera=True),
        "11-abrazadera-piloto": soporte_encoder(m, m.casquillo_piloto, abrazadera=True),
    }


def plano_impresion(nombre, pieza):
    if nombre in {"03-tapa-bandeja", "06-manivela", "10-soporte-encoder-abrazadera", "11-abrazadera-piloto"}:
        pieza = Rot(180, 0, 0) * pieza
    bb = pieza.bounding_box()
    return Pos(-bb.center().X, -bb.center().Y, -bb.min.Z) * pieza


def ensamblaje(m, modelos):
    componentes = [(nombre, pieza) for nombre, pieza in modelos.items()
                   if not nombre.startswith(("07", "08", "09", "10", "11"))]
    componentes.append(("04-rail-pantalla-inferior", mirror(modelos["04-rail-pantalla"], about=Plane.XZ)))
    for nombre, pieza in componentes:
        pieza.label = nombre
    return componentes


def exportar(destino):
    m = Medidas()
    modelos = piezas(m)
    for formato in ("stl", "step"):
        (destino / formato).mkdir(parents=True, exist_ok=True)
    for nombre, pieza in modelos.items():
        imp = plano_impresion(nombre, pieza)
        assert imp.is_valid and len(imp.solids()) == 1, nombre
        assert export_stl(imp, destino / "stl" / f"{nombre}.stl", tolerance=0.025, angular_tolerance=0.10)
        assert export_step(imp, destino / "step" / f"{nombre}.step")
    conjunto = Compound(children=[p for _, p in ensamblaje(m, modelos)], label="Entretiempo V2")
    assert export_step(conjunto, destino / "step" / "00-conjunto.step")


if __name__ == "__main__":
    exportar(Path(__file__).resolve().parents[1] / "fabricacion")
