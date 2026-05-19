/*
Nombre del programa: GORILLAS - Juego de artilleria en C++
Autor: Luis Gonzalez   


*/




// ============================================================
//  GORILLAS - Juego de consola en C++
//  Inspirado en el clasico Gorilla.bas de QBasic
//  Trabajo Practico - Programacion en C++
// ============================================================

#include <iostream>   // Entrada/salida de consola
#include <cmath>      // Funciones matematicas: sin, cos, sqrt, etc.
#include <vector>     // Contenedor dinamico para los edificios
#include <cstdlib>    // rand(), srand()
#include <ctime>      // time() para semilla aleatoria
#include <string>     // Manejo de cadenas de texto
#include <iomanip>    // Formato de salida (setw, setprecision)
#include <thread>     // this_thread::sleep_for (animacion)
#include <chrono>     // Duraciones de tiempo

using namespace std;

// ============================================================
//  CONSTANTES DEL JUEGO
// ============================================================
// CAMBIO MANUAL 1 - FISICA: Se modificaron los valores de
// gravedad y viento para hacer el juego mas desafiante.
// Gravedad original tipica: 9.8 | Nueva: 12.0 (mas pesada)
// Viento maximo original: 5    | Nuevo: rango -15 a +15
const double GRAVEDAD     = 12.0;   // m/s^2 - fuerza de caida
const double PI           = 3.14159265358979;
const int    ANCHO_MAPA   = 70;     // columnas de la consola
const int    ALTO_MAPA    = 22;     // filas de la consola
// CAMBIO MANUAL 3 - REGLAS: Sistema de puntos (mejor de 3 partidas)
const int    PUNTOS_PARA_GANAR = 3; // Primero en llegar a 3 gana

// ============================================================
//  ESTRUCTURAS DE DATOS
// ============================================================

// Representa un edificio en el escenario
struct Edificio {
    int columnaInicio; // columna izquierda
    int ancho;         // cuantas columnas ocupa
    int altura;        // cuantas filas tiene (desde abajo)
    char simbolo;      // caracter para dibujarlo
};

// Representa a un jugador (gorila)
struct Jugador {
    string nombre;     // nombre del jugador
    int columna;       // posicion horizontal en el mapa
    int fila;          // posicion vertical en el mapa
    int puntos;        // CAMBIO MANUAL 3: acumulador de puntos
    bool vivo;         // si sigue en juego en esta ronda
};

// Representa el estado del proyectil en un instante
struct Proyectil {
    double x;   // posicion horizontal (puede ser decimal)
    double y;   // posicion vertical   (puede ser decimal)
    double vx;  // velocidad horizontal
    double vy;  // velocidad vertical
};

// ============================================================
//  VARIABLES GLOBALES DEL JUEGO
// ============================================================
vector<Edificio> edificios;   // lista de edificios del escenario
Jugador jugador1, jugador2;
double viento = 0.0;          // fuerza del viento (negativo=izquierda)
int turnoActual = 1;          // 1 o 2, indica quien dispara

// ============================================================
//  FUNCION: limpiarPantalla
//  Descripcion: Limpia la consola de forma portatil
//  Parametros: ninguno
//  Retorna: void
// ============================================================
void limpiarPantalla() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// ============================================================
//  FUNCION: pausar
//  Descripcion: Espera N milisegundos (para animacion)
//  Parametros: milisegundos (int)
//  Retorna: void
// ============================================================
void pausar(int ms) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}

// ============================================================
//  FUNCION: generarEscenario
//  Descripcion: Crea los edificios de forma aleatoria.
//  CAMBIO MANUAL 2 - ESCENARIO: Se aumento la cantidad de
//  edificios de 5 a 8, y se ajusto el rango de alturas
//  (antes 5-12, ahora 4-18) para mayor variedad visual.
//  Parametros: ninguno
//  Retorna: void
// ============================================================
void generarEscenario() {
    edificios.clear();
    int x = 0;
    // CAMBIO MANUAL 2: 8 edificios en lugar de 5
    int numEdificios = 8;
    int anchoPorEdificio = ANCHO_MAPA / numEdificios;

    for (int i = 0; i < numEdificios; i++) {
        Edificio e;
        e.columnaInicio = x;
        // CAMBIO MANUAL 2: ancho entre 6 y 9 columnas
        e.ancho = 6 + rand() % 4;
        // CAMBIO MANUAL 2: altura entre 4 y 18 filas
        e.altura = 4 + rand() % 15;
        // Alternar simbolos para distinguir edificios
        e.simbolo = (i % 2 == 0) ? '#' : '=';
        edificios.push_back(e);
        x += anchoPorEdificio;
    }
}

// ============================================================
//  FUNCION: colocarGorilas
//  Descripcion: Pone a los jugadores en el techo del 1er y
//  ultimo edificio respectivamente.
//  Parametros: ninguno
//  Retorna: void
// ============================================================
void colocarGorilas() {
    // Gorila 1: encima del primer edificio
    Edificio& e1 = edificios.front();
    jugador1.columna = e1.columnaInicio + e1.ancho / 2;
    jugador1.fila    = ALTO_MAPA - e1.altura - 1;
    jugador1.vivo    = true;

    // Gorila 2: encima del ultimo edificio
    Edificio& e2 = edificios.back();
    jugador2.columna = e2.columnaInicio + e2.ancho / 2;
    jugador2.fila    = ALTO_MAPA - e2.altura - 1;
    jugador2.vivo    = true;
}

// ============================================================
//  FUNCION: dibujarMapa
//  Descripcion: Renderiza el escenario completo en consola.
//  CAMBIO MANUAL 4 - INTERFAZ: Se agrego informacion de viento
//  y puntuacion en la parte superior del mapa, y mejor
//  representacion visual de los gorilas con '@'.
//  Parametros: ninguno
//  Retorna: void
// ============================================================
void dibujarMapa() {
    limpiarPantalla();

    // CAMBIO MANUAL 4: encabezado con puntuacion y viento
    cout << "======================================================" << endl;
    // CAMBIO MANUAL 5: indicador visual de viento con flechas
    string indicadorViento = "";
    if (viento < -2)       indicadorViento = "<<< VIENTO FUERTE";
    else if (viento < 0)   indicadorViento = "<  viento suave";
    else if (viento > 2)   indicadorViento = "VIENTO FUERTE >>>";
    else if (viento > 0)   indicadorViento = "viento suave  >";
    else                   indicadorViento = "~~ sin viento ~~";

    cout << "  " << jugador1.nombre << ": " << jugador1.puntos << " pts"
         << "   |   " << indicadorViento << " (" << fixed << setprecision(1) << viento << ")"
         << "   |   " << jugador2.nombre << ": " << jugador2.puntos << " pts" << endl;
    cout << "======================================================" << endl;

    // Crear mapa como grilla de caracteres
    vector<string> mapa(ALTO_MAPA, string(ANCHO_MAPA, ' '));

    // Dibujar edificios
    for (const Edificio& e : edificios) {
        int filaBase = ALTO_MAPA - 1;        // fila del suelo
        int filaTecho = ALTO_MAPA - e.altura; // fila superior del edificio
        for (int f = filaTecho; f <= filaBase; f++) {
            for (int c = e.columnaInicio; c < e.columnaInicio + e.ancho && c < ANCHO_MAPA; c++) {
                if (f == filaTecho)
                    mapa[f][c] = '-'; // techo del edificio
                else
                    mapa[f][c] = e.simbolo;
            }
        }
    }

    // CAMBIO MANUAL 4: Gorilas representados con '@' mas visible
    if (jugador1.vivo && jugador1.fila >= 0 && jugador1.fila < ALTO_MAPA
        && jugador1.columna >= 0 && jugador1.columna < ANCHO_MAPA)
        mapa[jugador1.fila][jugador1.columna] = '@';

    if (jugador2.vivo && jugador2.fila >= 0 && jugador2.fila < ALTO_MAPA
        && jugador2.columna >= 0 && jugador2.columna < ANCHO_MAPA)
        mapa[jugador2.fila][jugador2.columna] = '@';

    // Imprimir el mapa fila por fila
    for (const string& fila : mapa) {
        cout << fila << "\n";
    }

    // Linea del suelo
    cout << string(ANCHO_MAPA, '~') << endl;
}

// ============================================================
//  FUNCION: gradosARadianes
//  Descripcion: Convierte angulo de grados a radianes.
//  Parametros: grados (double)
//  Retorna: double (radianes)
// ============================================================
double gradosARadianes(double grados) {
    return grados * PI / 180.0;
}

// ============================================================
//  FUNCION: hayColisionEdificio
//  Descripcion: Verifica si la posicion (x, y) esta dentro
//  de algun edificio del escenario.
//  Parametros: x (double), y (double)
//  Retorna: true si hay colision, false si no
// ============================================================
bool hayColisionEdificio(double x, double y) {
    int col = (int)x;
    int fil = (int)y;
    if (col < 0 || col >= ANCHO_MAPA) return false;

    for (const Edificio& e : edificios) {
        if (col >= e.columnaInicio && col < e.columnaInicio + e.ancho) {
            int filaTecho = ALTO_MAPA - e.altura;
            if (fil >= filaTecho && fil < ALTO_MAPA) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================
//  FUNCION: lanzarProyectil
//  Descripcion: Simula la trayectoria del banano lanzado.
//  Usa cinematica con paso de tiempo (dt) para actualizar
//  posicion y velocidad en cada frame.
//
//  FORMULAS USADAS:
//    vx(t) = vx0 + viento * dt
//    vy(t) = vy0 + GRAVEDAD * dt
//    x(t)  = x0  + vx * dt
//    y(t)  = y0  + vy * dt   (eje Y hacia abajo en consola)
//
//  Parametros:
//    xInicio, yInicio: posicion del gorila que dispara
//    angulo: angulo en grados (0-180)
//    velocidad: rapidez inicial del proyectil
//    objetivoCol, objetivoFil: posicion del gorila enemigo
//  Retorna: true si impacto al enemigo, false si fallo
// ============================================================
bool lanzarProyectil(double xInicio, double yInicio,
                     double angulo, double velocidad,
                     double objetivoCol, double objetivoFil) {

    double rad = gradosARadianes(angulo);

    // Velocidades iniciales descompuestas
    double vx0 = velocidad * cos(rad);
    double vy0 = -velocidad * sin(rad); // negativo porque Y crece hacia abajo

    // Si el jugador 2 dispara, invierte la direccion horizontal
    if (turnoActual == 2) vx0 = -vx0;

    Proyectil p;
    p.x  = xInicio;
    p.y  = yInicio;
    p.vx = vx0;
    p.vy = vy0;

    double dt = 0.15; // paso de tiempo de la simulacion

    for (int paso = 0; paso < 300; paso++) {
        // Actualizar velocidad por gravedad y viento
        // CAMBIO MANUAL 1: el viento afecta mas (factor 0.5 en vez de 0.1)
        p.vx += viento * 0.5 * dt;
        p.vy += GRAVEDAD * dt;

        // Actualizar posicion
        p.x += p.vx * dt;
        p.y += p.vy * dt;

        // --- Animacion del proyectil ---
        int px = (int)p.x;
        int py = (int)p.y;

        // Dibujar el mapa con el proyectil en su posicion actual
        dibujarMapa();

        // Mostrar proyectil si esta en pantalla
        if (px >= 0 && px < ANCHO_MAPA && py >= 0 && py < ALTO_MAPA) {
            // Mover cursor a la fila correcta y mostrar '*'
            // (Se imprime sobre la ultima pantalla usando posicion relativa)
            cout << "\033[" << (py + 4) << ";" << (px + 1) << "H" << "*";
            cout.flush();
        }
        pausar(40); // pausa de animacion en ms

        // --- Verificaciones de colision ---

        // 1. Salio por los bordes del mapa
        if (p.x < 0 || p.x >= ANCHO_MAPA) return false;

        // 2. Toco el suelo
        if (p.y >= ALTO_MAPA) return false;

        // 3. Impacto en edificio
        if (hayColisionEdificio(p.x, p.y)) return false;

        // 4. Impacto en el enemigo (rango de 2 celdas)
        double distancia = sqrt(pow(p.x - objetivoCol, 2) + pow(p.y - objetivoFil, 2));
        if (distancia < 2.0) {
            return true; // IMPACTO!
        }
    }
    return false;
}

// ============================================================
//  FUNCION: pedirDisparo
//  Descripcion: Solicita angulo y velocidad al jugador activo
//  y ejecuta el lanzamiento.
//  CAMBIO MANUAL 4: Mensajes mas descriptivos e informativos.
//  Parametros: ninguno
//  Retorna: true si el disparo fue un impacto exitoso
// ============================================================
bool pedirDisparo() {
    Jugador& atacante = (turnoActual == 1) ? jugador1 : jugador2;
    Jugador& defensor = (turnoActual == 1) ? jugador2 : jugador1;

    // CAMBIO MANUAL 4: info detallada antes del disparo
    cout << "\n----------------------------------------------" << endl;
    cout << "  TURNO DE: " << atacante.nombre << endl;
    cout << "  Tu posicion : columna " << atacante.columna << endl;
    cout << "  Enemigo en  : columna " << defensor.columna << endl;
    cout << "  Viento      : " << fixed << setprecision(2) << viento
         << " (positivo = derecha, negativo = izquierda)" << endl;
    cout << "----------------------------------------------" << endl;

    double angulo, velocidad;

    cout << "  Ingresa angulo (1-179 grados): ";
    cin >> angulo;
    if (angulo < 1)   angulo = 1;
    if (angulo > 179) angulo = 179;

    cout << "  Ingresa velocidad (10-150)   : ";
    cin >> velocidad;
    if (velocidad < 10)  velocidad = 10;
    if (velocidad > 150) velocidad = 150;

    cout << "\n  [Disparando...]\n";
    pausar(300);

    bool impacto = lanzarProyectil(
        (double)atacante.columna, (double)atacante.fila,
        angulo, velocidad,
        (double)defensor.columna, (double)defensor.fila
    );

    return impacto;
}

// ============================================================
//  FUNCION: mostrarExplosion
//  Descripcion: Muestra un efecto de explosion en la consola.
//  CAMBIO MANUAL 4: animacion mejorada con varios frames.
//  Parametros: col, fila - posicion de la explosion
//  Retorna: void
// ============================================================
void mostrarExplosion(int col, int fila) {
    // Frames de la animacion de explosion
    vector<string> frames = { "*", "**", "***", "**", "*", " " };
    for (const string& f : frames) {
        dibujarMapa();
        // Posicionar cursor (secuencia ANSI, funciona en Linux/Mac/Win10)
        cout << "\033[" << (fila + 4) << ";" << (col) << "H"
             << "  " << f << "BOOM!" << f;
        cout.flush();
        pausar(120);
    }
}

// ============================================================
//  FUNCION: juegoRonda
//  Descripcion: Controla el flujo de una ronda completa.
//  Alterna turnos hasta que uno de los dos jugadores impacta.
//  Parametros: ninguno
//  Retorna: numero del jugador ganador de la ronda (1 o 2)
// ============================================================
int juegoRonda() {
    srand((unsigned int)time(nullptr)); // semilla aleatoria

    // CAMBIO MANUAL 1: viento aleatorio en rango ampliado (-15 a +15)
    viento = ((rand() % 31) - 15);

    generarEscenario();
    colocarGorilas();
    turnoActual = 1;

    while (true) {
        dibujarMapa();
        bool impacto = pedirDisparo();

        if (impacto) {
            // Determinar quien gano esta ronda
            Jugador& ganador = (turnoActual == 1) ? jugador1 : jugador2;
            Jugador& perdedor = (turnoActual == 1) ? jugador2 : jugador1;

            mostrarExplosion(perdedor.columna, perdedor.fila);
            perdedor.vivo = false;

            dibujarMapa();
            cout << "\n\n  *** " << ganador.nombre << " ACERTO! ***\n" << endl;
            pausar(1500);
            return turnoActual;
        } else {
            // CAMBIO MANUAL 4: mensaje cuando falla el disparo
            dibujarMapa();
            cout << "\n  [" << ((turnoActual == 1) ? jugador1.nombre : jugador2.nombre)
                 << "] Fallo el disparo! Turno del otro jugador...\n";
            pausar(1200);

            // Alternar turno
            turnoActual = (turnoActual == 1) ? 2 : 1;
        }
    }
}

// ============================================================
//  FUNCION: mostrarMarcador
//  Descripcion: Muestra el marcador actual de la partida.
//  CAMBIO MANUAL 3: pantalla de puntaje entre rondas.
//  Parametros: ninguno
//  Retorna: void
// ============================================================
void mostrarMarcador() {
    limpiarPantalla();
    cout << "\n  ================================" << endl;
    cout << "       MARCADOR DE LA PARTIDA    " << endl;
    cout << "  ================================" << endl;
    cout << "   " << jugador1.nombre << "  :  " << jugador1.puntos
         << " punto(s)" << endl;
    cout << "   " << jugador2.nombre << "  :  " << jugador2.puntos
         << " punto(s)" << endl;
    cout << "  --------------------------------" << endl;
    cout << "   (Primero en llegar a " << PUNTOS_PARA_GANAR << " gana)" << endl;
    cout << "  ================================\n" << endl;
    pausar(2000);
}

// ============================================================
//  FUNCION: main
//  Descripcion: Punto de entrada. Controla el menu y el
//  ciclo de partidas completas.
//  Parametros: ninguno
//  Retorna: 0 (exito)
// ============================================================
int main() {
    char jugarDeNuevo = 's';

    // CAMBIO MANUAL 4: pantalla de bienvenida mejorada
    limpiarPantalla();
    cout << "\n";
    cout << "  ================================================\n";
    cout << "     ___  ___  ____  ____  __    __      __      \n";
    cout << "    / __)/ _ \\|  _ \\|_  _||  |  | |    / /      \n";
    cout << "   | (_ | (_) | |_) | || | | |__| |__ / /       \n";
    cout << "    \\___/\\___/|_| \\_\\ |_|  |_____|____/_/        \n";
    cout << "                                                  \n";
    cout << "         Juego de artilleria - C++ Edition        \n";
    cout << "  ================================================\n\n";
    cout << "  Ingresa el nombre del Jugador 1: ";
    getline(cin, jugador1.nombre);
    cout << "  Ingresa el nombre del Jugador 2: ";
    getline(cin, jugador2.nombre);

    // Inicializar puntos
    jugador1.puntos = 0;
    jugador2.puntos = 0;

    // ---- CICLO DE PARTIDA COMPLETA ----
    do {
        // CAMBIO MANUAL 3: jugar rondas hasta que alguien llegue a PUNTOS_PARA_GANAR
        while (jugador1.puntos < PUNTOS_PARA_GANAR &&
               jugador2.puntos < PUNTOS_PARA_GANAR) {

            int ganadorRonda = juegoRonda();

            // Sumar punto al ganador de la ronda
            if (ganadorRonda == 1)
                jugador1.puntos++;
            else
                jugador2.puntos++;

            mostrarMarcador();
        }

        // Mostrar ganador final de la partida
        limpiarPantalla();
        string nombreGanador = (jugador1.puntos >= PUNTOS_PARA_GANAR)
                               ? jugador1.nombre : jugador2.nombre;

        cout << "\n\n";
        cout << "  ================================================\n";
        cout << "    GANADOR DE LA PARTIDA: " << nombreGanador << "!!\n";
        cout << "  ================================================\n";
        cout << "  Puntuacion final:\n";
        cout << "    " << jugador1.nombre << ": " << jugador1.puntos << " puntos\n";
        cout << "    " << jugador2.nombre << ": " << jugador2.puntos << " puntos\n";
        cout << "  ================================================\n\n";

        // CAMBIO MANUAL 4: mensaje de reinicio mas claro
        cout << "  Desean jugar otra partida? (s/n): ";
        cin >> jugarDeNuevo;
        cin.ignore(); // limpiar buffer

        // Reiniciar puntos para nueva partida
        if (jugarDeNuevo == 's' || jugarDeNuevo == 'S') {
            jugador1.puntos = 0;
            jugador2.puntos = 0;
        }

    } while (jugarDeNuevo == 's' || jugarDeNuevo == 'S');

    cout << "\n  Gracias por jugar! Hasta la proxima!\n\n";
    return 0;
}