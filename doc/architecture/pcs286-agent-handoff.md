# PCS286 portable: relevo técnico y trabajo pendiente

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Fecha: 2026-09-24. Documento de continuidad para otra máquina/cuenta.
Estado de código examinado: `bcaa691c8f0ebca4778e2d75113af9d4e3db71e1`.
Este documento se añade después de ese commit; no describe funcionalidades
futuras como si estuviesen implementadas. No requiere el historial del chat.

## Aviso de entrega: PR documental y código en bundle

GitHub rechazó el push del código porque las credenciales OAuth actuales no
tienen permiso `workflow` para los cambios CI acumulados. No se modificaron
credenciales ni se eliminaron esos cambios para sortear la restricción.
Por ello la PR `docs/pcs286-agent-handoff` contiene SÓLO este documento y apunta
a `architecture/portable-engine`. **No contiene los 49 commits de implementación.**
Las referencias posteriores a la rama de trabajo son
`feature/pcs286-integration-p1`, no la rama documental.

Entrega completa local: `Z:/BluMach-pcs286-handoff-2026-09-24.bundle`.
Es un bundle Git autocontenido de la rama de trabajo, no una copia de BIOS o
medios locales. Llevarlo a la nueva máquina por un canal privado. Desde un
clon de BluMach sin cambios pendientes, importar sin sobrescribir ramas:

```text
git bundle verify Z:/BluMach-pcs286-handoff-2026-09-24.bundle
git fetch Z:/BluMach-pcs286-handoff-2026-09-24.bundle refs/heads/feature/pcs286-integration-p1:refs/heads/handoff/pcs286-20260924
git switch handoff/pcs286-20260924
git merge-base --is-ancestor bcaa691c8f0ebca4778e2d75113af9d4e3db71e1 HEAD
```

Adaptar Z al destino real del archivo. Si la rama de destino ya existe, revisar
su contenido, no forzar. Si sólo se dispone de la PR documental y no del bundle,
**falta código**: pedir el archivo antes de continuar A. Publicar luego una rama
de implementación con credenciales propias autorizadas para los cambios CI y
abrir su PR contra portable-engine, sin fusionar automáticamente.

## 1. Resumen ejecutivo

La rama `feature/pcs286-integration-p1` contiene el desarrollo incremental del
Intel 80286 portable y bases de la placa PCS286. Su destino es
`architecture/portable-engine`, NO `master`. Al preparar este relevo estaba
49 commits por delante y cero por detrás de esa base remota verificada.
La PR de relevo es un borrador para transferir trabajo, no una certificación
de máquina completa ni una solicitud de fusionar sin revisión.

La CPU tiene un intérprete real parcial ampliamente probado; los últimos
bloques construyen el modo protegido. **La ejecución pública con PE activado
continúa rechazada antes de fetch.** Existen helpers privados protegidos que
pasan pruebas sintéticas, pero eso no significa que se ejecuten programas PE.
PCS286 portable no se debe anunciar como máquina arrancable por estos tests.

Siguiente tarea recomendada: IRET protegido al mismo CPL, con pruebas propias.
Después, gestión de excepciones encadenadas y protección de accesos, antes
de habilitar un primer programa protegido completo.

## 2. Objetivo y límites arquitectónicos

- Motor C11, estado por instancia, sin globals mutables ni dependencia Qt/OS.
- Runtime y frontend opcionales; una CPU no conoce la GUI ni el catálogo.
- Separar CPU emulada, CPU anfitriona y sistema anfitrión. No introducir
  selectores x86 o TSS en el contrato genérico para resolver este bloque.
- Toda CPU dispone de intérprete portable. JIT y timing avanzado son trabajos
  independientes; no se pretende convertir automáticamente C en HDL.
- La máquina compone chips, buses, memoria, señales y relojes. Cada componente
  conserva su estado y contratos de propiedad. A20 pertenece a la placa.
- El router de direcciones no es un bus eléctrico ISA. El adaptador AT realiza
  arbitraje/conexiones; no se transforma el bus genérico en una interfaz x86.
- Tiempo de sesión y ciclos nativos son conceptos distintos. No añadir ticks
  ambiguos ni constantes de rendimiento inventadas para desbloquear BIOS.
- No reutilizar globals del núcleo clásico. Portado selectivo y trazable,
  conservando autores y licencia; una reescritura derivada no es clean-room.

No se añade otra máquina, interfaz declarativa, GUI, migrador de configuraciones
o plataforma anfitriona en esta continuación. Mantener PCS86/M15 y el resto
de regresiones existentes sin cambios ajenos al alcance.

## 3. Cómo recuperar el trabajo en otra cuenta

Clonar BluMach/BluMach con historial suficiente y obtener la rama de esta PR.
No empezar desde master ni desde una copia antigua de portable-engine.
Usar un checkout/worktree dedicado y comprobar:

```text
git status --short
git branch --show-current
git log -8 --oneline
git merge-base --is-ancestor bcaa691c8f0ebca4778e2d75113af9d4e3db71e1 HEAD
```

Si no aparece ese ancestro, detener la implementación y resolver qué rama se
ha descargado. No copiar archivos sueltos sobre otro trabajo para simular una
fusión. No rebase de la rama de integración publicada ni force-push.
Los paths de este documento son relativos al checkout salvo que se indique
explícitamente que son recursos locales de la estación original.

La cuenta nueva necesita sus propias credenciales/permisos GitHub para escribir.
No transferir tokens ni configuraciones privadas. No requiere acceso a la cuenta
del agente anterior para leer este documento o el código publicado.

## 4. Lectura obligatoria y mapa de código

Leer en este orden:

1. Este documento y las instrucciones AGENTS aplicables en la estación nueva.
2. `doc/architecture/pcs286-protected-mode-plan.md`: bloques y gates actuales.
3. `doc/architecture/pcs286-cpu286-coverage.md`: historial detallado de cobertura.
4. `components/cpu/80286/include/blumach/components/cpu_80286.h`: contrato público.
5. `components/cpu/80286/src/descriptor_286.h` y `.c`: contratos privados.
6. `components/cpu/80286/src/exception_286.c`: último bloque.
7. `components/cpu/80286/src/cpu_80286.c`: dispatcher y límites reales.
8. Tests de descriptores, entrada protegida, faults, interrupciones y segmentos.
9. `provenance/components.json`, antes de reutilizar implementación.

La cobertura contiene secciones históricas: un «pending» de una sección antigua
puede estar resuelto por otra posterior. El contrato inicial
`doc/architecture/pcs286-portable-contracts.md` dice «draft contracts only» para
su cambio original; tampoco es por sí solo el estado actual de toda la rama.
Contrastar siempre código, tests y secciones recientes; no convertir una lista
histórica de instrucciones en un inventario actualizado sin inspección.

Otros puntos de entrada:

| Área | Ubicación |
| --- | --- |
| Pruebas CPU/placa | `tests/components/pcs286/` |
| Registro de tests | `tests/components/pcs286/CMakeLists.txt` |
| Router genérico | `components/buses/include/blumach/components/bus.h` |
| Interconexión AT y PIC | `components/pc/src/at_bus.c`, `at_pic.c` |
| Memoria PCS286 | `systems/olivetti-pcs286/src/pcs286_memory.c` |
| Contratos de composición | `systems/olivetti-pcs286/include/blumach/systems/` |
| Corpus externo opcional | `tests/data/sst286-lock.json`, `tools/sst286.py` |

## 5. Qué está implementado y qué demuestra

### 5.1 Modo real

Hay bloques de transferencias, ALU, pila, control, strings/REP, I/O,
interrupciones y faults con tests específicos. Las variantes exactas están
en el ledger y dispatcher, no se afirma cobertura completa de todos los opcodes.
Mantener reglas de reinicio REP, sombras de SS/STI, NMI, LOCK, errores de
transporte y rollback arquitectónico ya probadas. No sustituirlas por un
dispatcher nuevo sin demostrar equivalencia.

SingleStepTests es una comparación funcional opcional de un subconjunto real
fijado por hash/commit. No certifica modo protegido, toda la ISA, tiempos ni
secuencias eléctricas. No afirmar «CPU completa» porque el subconjunto pase.

### 5.2 Interpretación de protección

Decodificación de selectores, tipos y bytes de acceso; bases de 24 bits,
límites de 16 bits, presencia, DPL y atributos. Límites normales y expand-down.
Sin significados 386 para bits reservados. Selector nulo distinto de índice
cero de LDT. Helpers privados, no ABI genérico instalado.

### 5.3 Tablas y carga de segmentos

Lectura GDT/LDT por transacciones existentes, preflight de entrada completa,
palabras alineadas o bytes en direcciones impares, wrap físico de 24 bits.
Los resultados distinguen rechazo arquitectónico de error host.
Preparación DS/ES/SS/LLDT con reglas de privilegio, presencia y nulidad.

El commit realiza RMW bloqueado del byte de acceso para establecer A incluso
si la lectura inicial ya lo tenía. Conserva los otros bits leídos en el RMW,
libera LOCK ante error y consume el plan para evitar repetir efectos.
Null/LDTR no necesitan escritura de A. La caché destino sólo cambia al éxito.

MOV a ES/SS/DS, POP de segmentos y LDS/LES usan la ruta común de estado.
En real conservan comportamiento previo. En PE se prueba el helper de estado,
no la ejecución pública del opcode. El dispatcher LLDT sigue pendiente.

### 5.4 Entrada protegida privada más reciente

`bm_286_pm_enter_event` admite puertas IDT de interrupción/trampa con la pila
actual. Comprueba límite IDT, tipo, DPL para origen software, presencia, código
destino, capacidad de pila e IP. Código conforming puede tener DPL inferior
sin cambiar CPL. Normaliza CS.RPL y apila FLAGS, CS, retorno y error opcional.
Limpia TF/NT; IF sólo en puerta de interrupción.

Task gates y transferencias a privilegio interior quedan unsupported.
El llamador debe decidir origen/EXT, IP de retorno, presencia del error,
INTA, arbitraje, bloqueo NMI y unwind. Un fault devuelto es metadata, NO una
excepción entregada recursivamente. No hay gestión completa de doble fault.

Preflight antes de escrituras, accessed antes del marco y commit de registros
después son política funcional; no una traza física validada. Fallos host
conservan registros pero no revierten writes de memoria ya completados.
No repetir automáticamente la operación tras un error.

## 6. Trabajo pendiente por bloques y criterios de cierre

### A. IRET protegido al mismo CPL — primer trabajo

- Consultar algoritmo 286 de Intel; no heredar reglas 386/v8086.
- Leer marco de pila y validar CS, RPL/DPL, tipo, presencia e IP antes de commit.
- Precisar máscaras/restauración de FLAGS, IOPL/IF según CPL y efectos NMI.
- Separar retorno ordinario de NT/task return y retorno a privilegio exterior.
  Los no implementados deben seguir explícitos; no degradarlos a retorno simple.
- El código de error no se retira mágicamente: lo elimina el handler según ISA.
- Reutilizar validación/RMW cuando corresponda, sin forzar reglas DS/SS sobre CS.
- Tests: interrupt/trap -> handler sintético -> retorno al contexto anterior;
  límites, presencia, selectores, flags, pila impar, fallos antes/después de cada
  transferencia, ausencia de commit parcial y separación host/guest.
- Mantener el gate público hasta completar C y D. Inicialmente la prueba puede
  conectar helpers privados y debe declararlo, no llamarse programa PE real.

### B. Excepciones encadenadas, doble fault y shutdown

- Clasificar faults/traps, retorno correcto y códigos de error por origen.
- Implementar reglas Intel para fallo durante entrega y transición a #DF.
  No todo segundo error es #DF y un fallo host no es un fault del procesador.
- Definir qué ocurre si falla la entrada de #DF, con estado shutdown observable.
- Evitar recursión ilimitada, reintentos de writes y frames reales en PE.
- Tests de combinaciones documentadas, prioridades, error EXT/IDT/TI, handler
  ausente, pila inválida y recuperación que sí esté permitida.
- Integrar IRQ/NMI, sombras y TF sin perder señales ni hacer INTA dos veces.

### C. Acceso protegido a instrucciones, datos y pila

- CS fetch: validación de caché, límites y anchura efectiva.
- DS/ES/SS: nulidad, permisos, tipo, privilegio y expand-down.
- Operandos de palabra, instrucciones multibyte, pila y strings en fronteras.
- Prioridad #GP/#SS/#NP y punto de retorno según instrucción/causa documentada.
- Preservar caches ocultas, dirección física de 24 bits y A20 de la placa.
- No añadir checks indiscriminados al router compartido: son semántica CPU.
- Tests positivos/negativos y de commit para lectura, escritura, fetch, REP y
  pila; programas sintéticos de reparación/reintento cuando el dispatcher abra.

### D. Entrada pública y sistema

- LMSW y transferencia lejana/carga de CS para entrar correctamente en PE.
- Conectar LLDT y revisar instrucciones de tabla/sistema ya presentes.
- LAR/LSL/VERR/VERW: reglas de consulta, resultado/ZF y no-fault donde proceda.
- Privilegios de instrucciones, IOPL, CLI/STI, PUSHF/POPF/IRET e I/O.
- No retirar las dos barreras PE (step y ejecución interna) por separado sin
  auditar todos sus consumidores. No proporcionar un bypass de test público.
- Gate de cierre: programa que entra en PE, carga segmentos, opera, provoca
  un fault, lo atiende y retorna; IRQ/NMI/TF y regresiones reales verificadas.
- Documentar exactamente la parte PE soportada, sin declarar bloques E/F listos.

### E. Transferencias entre privilegios

- Call gates, código conforming/no conforming, validación de privilegios.
- Cambio de pila mediante TSS, parámetros y orden de escrituras.
- Interrupciones hacia privilegio interior, RETF e IRET hacia exterior.
- Invalidación de segmentos que ya no sean utilizables al retornar.
- Matrices CPL/RPL/DPL y errores durante cada etapa. No adivinar precedencia.

### F. Tareas 286

- TSS 286, LTR, descriptor disponible/ocupado, límites y accesibilidad.
- Backlink, busy y NT; CALL/JMP/task gate e IRET anidado.
- Guardar/cargar estado, LDTR y caches según reglas 286.
- Excepciones durante transición y estado parcial arquitectónicamente permitido.
  No extender ciegamente la política de rollback de helpers a un task switch.
- Tests de tareas sintéticas y negativos antes de anunciar multitarea completa.

### G. Auditoría final de ISA y timing

- Inventario del dispatcher contra manual y ledger: faltantes, aliases y
  combinaciones de prefijos, separando documentado, observado y desconocido.
- CPU sin 80287 inicialmente; ESC/WAIT y señales deben describir ausencia real,
  no fingir un coprocesador que ejecuta. 80287 completo es otro hito.
- Timing actual UNKNOWN: no soporte estricto clocked demostrado para este core.
- Diseñar evidencia de ciclos/esperas/prefetch antes de introducir tiempos.
  Si se decide un modo provisional, nombrarlo y probar su contrato expresamente.
- Optimizar sólo con medidas reproducibles; no sacrificar corrección por POST.

## 7. Lo que falta de la máquina, aparte de CPU

La CPU no cierra el PCS286. Hay memoria/arbitraje AT/PIC y contratos, no un
ensamblado completo validado. Headland y AT DMA mantienen acceptance skips.
No reemplazar esos skips por implementaciones vacías o tests triviales.

Orden orientativo posterior, sujeto a lectura de los registros canónicos:

1. Headland: mapa RAM/ROM y registros documentados, identificación de revisión.
2. DMA AT byte/word y páginas, arbitraje HOLD/grant y transferencias periféricas.
3. Glue/IOC02, A20, reset, refresh/paridad y PIT/RTC/CMOS según evidencia.
4. Controlador de teclado conductual correcto, protocolo y conexiones.
5. Composición mínima CPU+ROM+RAM+I/O; trazas diagnósticas de POST sin atajos.
6. Vídeo PVGA1A/DAC, framebuffer neutral; teclado y floppy con DMA/IRQ.
7. ATA/IDE y modelos de disco documentados; NO XTA del PCS86.
8. Integración runtime/headless y frontend sólo cuando sean ejecutables.
9. Validación de arranque, reset, pausa, cierre, medios ausentes y errores host.

Identidad: PCS286 Headland, NO PCS286/S TI/OLIMCU. No copiar ranuras, jumpers,
RAM o perfiles del PCS86. El documento inicial de contratos incluye notas
corregidas de RAMDAC/KBC: comprobar las fuentes actuales antes de portarlas.
`62410C62.BIN` no es firmware válido de KBC. No inventar MCU firmware.

## 8. Pruebas y reproducción en una máquina nueva

Base observada en Windows, sin GUI ni firmware:

| Configuración | Resultado |
| --- | --- |
| GCC UCRT64 Debug | 107 tests ordinarios + 1 SST opcional pasan; 2 skips |
| GCC UCRT64 Release | 107 pasan; 2 skips |
| MSVC Debug / Release | 107 pasan en cada configuración; 2 skips |
| Python | 30 pasan |
| Procedencia | 38 componentes, 206 archivos, cero errores |
| Catálogo | 32 máquinas, 5 idiomas, válido |

Los skips son Headland y DMA AT. Counts orientativos al commit indicado;
deben aumentar al añadir tests, no mantenerse artificialmente. Compilación
Windows no es prueba de PowerPC/big-endian ni de otro host.

Ejemplo PowerShell UCRT64, adaptar instalación, checkout y build locales:

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
cmake -S . -B build/handoff-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBLUMACH_BUILD_LEGACY=OFF -DBLUMACH_BUILD_ENGINE=ON -DBLUMACH_BUILD_PORTABLE_QT=OFF -DBUILD_TESTING=ON
cmake --build build/handoff-debug --parallel 8
ctest --test-dir build/handoff-debug --output-on-failure -j 8
python -B -m unittest tools.tests.test_sst286 tools.tests.test_provenance_audit tools.tests.test_catalog_builder tools.tests.test_catalog_audit
python -B tools/provenance_audit.py
python -B tools/catalog_builder.py check
git diff --check
```

Parar al primer exit code no cero; no interpretar el éxito del último comando
como éxito de todos. Repetir en Release y, cuando esté disponible, MSVC con su
generador instalado y `--config Debug`/`--config Release`. No mezclar generadores
o compiladores en el mismo directorio. Mantener asserts en tests Release.

Los build originales son `build/integration-ucrt`, `build/integration-release`
y `build/integration-msvc`; sus caches NO se transfieren ni son portables.
El test nuevo es `pcs286-component.protected-entry`; descriptores es
`pcs286-component.cpu-descriptors`. Ejecutar focalizados y después suite completa.

Para SST: leer `tools/sst286.py --help` y el lock versionado, obtener el corpus
fijado externamente y configurar `BLUMACH_SST286_DATA_DIR`. No añadir vectores
al repositorio sin revisar su procedencia. El path cache de la estación anterior
no existe necesariamente en destino. `BM_SST286_PROBE` puede apuntar al probe
compilado para los tests Python. SST es opcional, no requisito para comenzar A.

## 9. Fuentes, derechos y biblioteca local

Primaria: Intel 80286/80287 Programmer's Reference Manual 1987 (210498-005),
capítulos de protección/excepciones y algoritmos del apéndice B:
https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf

Usar las referencias específicas del ledger y las erratas, incluida REP restart,
cuando apliquen. Extractos indexados ayudaron cuando fallaron mirrors; para
reglas críticas nuevas leer la página completa y dejar cualquier duda explícita.
No completar texto ilegible por intuición ni usar otro emulador como único oráculo.

Biblioteca canónica original: `Z:/library/olivetti/pcs286`.
Registro estable `pcs286`: README, manifest y worklog. La réplica del workspace
no es otra fuente de verdad. En la cuenta nueva Z puede no estar montado.
Si falta, se puede continuar la CPU sintética con este documento y fuentes
públicas; NO afirmar que se auditó la biblioteca, ejecutó firmware o validó placa.
Pedir al propietario acceso para la fase de máquina y registrar el cierre pendiente.

Herramienta local `tools/library_audit.py`: pertenece al workspace de investigación,
no asumir que viene con el Git de BluMach. Export original:
`Z:/BluMach-workspace-tools/tools/` (leer su README antes de instalar).
Skills exportadas: `Z:/Skills`, tampoco se cargan automáticamente en otra cuenta.
No copiar toda la biblioteca o sus documentos restringidos al checkout Git.

ROM/BIOS, medios, PDFs/imágenes con derechos no permitidos y credenciales no se
publican en la PR. Sólo metadatos autorizados, hashes y referencias. Originales
inmutables; medios se prueban en copias y sólo tras comprobación de seguridad.
La auditoría PCS286 previa tenía cero errores y 12 avisos de custodia (derechos
desconocidos y medios sin escanear); no ocultarlos para obtener un verde.

## 10. Procedimiento de cada entrega

1. Confirmar rama, HEAD y árbol limpio; preservar cambios de otras personas.
2. Seleccionar un bloque acotado con criterio de cierre, no una microfunción
   sin integración ni toda la CPU de una vez.
3. Contrastar semántica con fuente, escribir casos positivos/negativos.
4. Implementar en el componente dueño, sin ampliar ABI genérico por comodidad.
5. Probar estados independientes, límites, callbacks ausentes y fallos antes/
   después de efectos. Distinguir contrato del emulador de evidencia física.
6. Ejecutar focalizados, suite completa, Python, procedencia y diff check.
7. Actualizar plan/ledger y procedencia; preservar avisos originales/SPDX.
8. Commit inglés revisable, reportar qué funciona y qué continúa bloqueado.
9. Si biblioteca disponible: actualizar manifest/README/worklog, auditar y
   sincronizar selectivamente con guardia de hashes. No mirroring destructivo.
10. Mantener la PR en borrador mientras falte revisión del alcance acumulado.
    No fusionar automáticamente en master ni declarar terminada la máquina.

## 11. Primer mensaje sugerido al agente entrante

Continúa el trabajo de la rama de esta PR, no desde master. Lee completo
doc/architecture/pcs286-agent-handoff.md y los contratos/plan que referencia.
Primero reproduce el estado de tests sin modificar emulación. Después implementa
IRET protegido del 80286 al mismo CPL, con semántica Intel, pruebas de límites,
FLAGS, selectores y fallos por transferencia. Conserva las barreras públicas PE
hasta tener protección de accesos y entrega/retorno de excepciones integrados.
No inventes comportamiento para pasar POST, no conviertas errores host en faults
guest y no presentes helpers como programas protegidos funcionando. Conserva
autores, actualiza procedencia/ledger y deja claros los pendientes y la evidencia.
No dependas de paths, ROM, caches ni herramientas locales de la cuenta anterior.
