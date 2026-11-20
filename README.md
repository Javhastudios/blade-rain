# Blade Rain

Emulación del **VJ-330** (inspirado en el Roland VP-330 Vocoder Plus): Strings, Human Voice con sus
bancos de formantes, vocoder de 10 bandas, Ensemble, Pitch Shift, delay ping-pong y reverb.

Es la versión nativa del prototipo Web Audio (que está en [`web/`](web/)). Del mismo código salen:

- **Aplicación independiente** para Windows, macOS y Linux (sin navegador ni conexión).
- **Plugin VST3** (y **Audio Unit** en Mac), como instrumento polifónico.

## Descargar

1. Pestaña **Actions** → la última ejecución en verde de *Compilar Blade Rain*.
2. Abajo, en **Artifacts**, descarga `BladeRain-Windows`, `BladeRain-macOS` o `BladeRain-Linux`.
3. Dentro hay un `LEEME.txt` con cómo abrirlo, instalar el plugin y usar el micrófono.

Si se sube una etiqueta `v1.0.0` (o cualquier `v…`), además se crea una *Release* con los tres archivos.

## Micrófono del vocoder

- **App:** el micro es la entrada de audio elegida en *Options* (hay que desmarcar *Mute audio input*).
- **Plugin:** el micro entra por el **sidechain** del instrumento. Activa el tablet **Mic** y asigna el
  Vocoder a Lower y/o Upper.

## Qué hay dentro

| Carpeta | Contenido |
|---|---|
| `Source/dsp/WebAudio.h` | Piezas de Web Audio en C++ según la especificación: automatización de parámetros, biquads, oscilador, retardos, waveshaper, convolución y compresor |
| `Source/dsp/VP330Engine.h` | El instrumento: voces, Strings, Human Voice, vocoder, ensemble y efectos (traducción del grafo del prototipo) |
| `Source/PluginProcessor.*` | Parámetros automatizables, MIDI, Hold y pedal, micrófono, estado |
| `Source/PluginEditor.*`, `Source/ui/` | Interfaz nativa con el aspecto del panel del prototipo |
| `Resources/` | Fuentes TR-909 (logotipo), Oswald e IBM Plex Mono (licencia OFL) |
| `tests/` | Comparación con el prototipo original y prueba de humo del plugin |
| `web/` | El prototipo web original |

## Comprobación contra el prototipo

`tests/run_js.mjs` ejecuta el HTML original en Node con
[node-web-audio-api](https://github.com/ircam-ismm/node-web-audio-api) y un DOM simulado: pulsa teclas,
mueve faders y tablets como un usuario y alimenta el vocoder con una voz sintética. `compare_cpp` hace
lo mismo con el motor en C++ y `compare.mjs` compara las dos salidas (hoy coinciden con una diferencia
de 70–98 dB por debajo de la señal).

```
cd tests && npm install --no-save node-web-audio-api@2.2.0 && cd ..
node tests/run_js.mjs out
g++ -std=c++17 -O2 -o compare_cpp tests/compare_cpp.cpp && ./compare_cpp out
node tests/compare.mjs out
```

## Diferencias con el prototipo web

Corregido en la versión nativa (fallos del prototipo):

- Strings y Human Voice no sonaban tras *Power on* hasta tocar un fader de Balance o un tablet.
- Al encender el micro, el *Direct Mic* no sonaba hasta mover su fader.
- El fader **Tone del vocoder** no hacía nada (se tomaba la salida antes de su filtro).

Cambios por ser un instrumento nativo:

- No hay botón *Power*: siempre está encendido.
- No se porta el secuenciador MIDI (el propio prototipo lo indicaba): el DAW hace ese papel.
- Los dispositivos MIDI y de audio se eligen en el DAW o en *Options* de la app.
- El pedal de sustain funciona como en cualquier instrumento (en la web solo actuaba con Hold).
- Todos los controles son parámetros automatizables y se guardan con el proyecto.

## Compilar en tu ordenador (opcional)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target BladeRain_All
```

JUCE se descarga solo (licencia gratuita para uso personal y código abierto).
