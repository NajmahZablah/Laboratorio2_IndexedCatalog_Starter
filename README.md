# Starter — Laboratorio 2: catálogo indexado y confiable

**Estudiante:** Najmah Zablah

Este proyecto contiene la infraestructura y las pruebas visibles del Laboratorio 2 de Estructura de Datos II.

Consulte la especificación completa en:

```text
../Lab2_Catalogo_Indexado_Confiable_EstructuraDeDatosII-Q32026.md
```

## Trabajo del estudiante

Modifique `src/catalog.cpp` y complete:

1. `read_record_at`
2. `build_primary_index`
3. `find_offset`
4. `build_composer_index`
5. `find_by_composer`
6. `verify_primary_index`
7. Opcional: `intersect_sorted`

Puede crear funciones auxiliares privadas dentro de ese archivo. Agregue sus pruebas en `tests/student_tests.cpp` y actualice este README. No modifique las interfaces públicas, `tests/tests.cpp`, `CMakeLists.txt` ni los demás archivos provistos.

## Compilar

```bash
cmake -S . -B build
cmake --build build
```

## Ejecutar pruebas

```bash
ctest --test-dir build --output-on-failure
```

O directamente:

```bash
./build/catalog_tests
```

Para ejecutar un ejercicio específico:

```bash
./build/catalog_tests --test-case="E04*"
```

El starter compila desde el inicio, pero las pruebas fallan hasta completar los TODO.

## Generar datos de ejemplo

```bash
./build/catalog_generate data/catalog.psv data/catalog.bin
```

## Usar la aplicación

```bash
./build/lab2_catalog build data/catalog.bin data/catalog.idx
./build/lab2_catalog find data/catalog.bin data/catalog.idx DG18807
./build/lab2_catalog composer data/catalog.bin data/catalog.idx BEETHOVEN
./build/lab2_catalog verify data/catalog.bin data/catalog.idx
```

## Simular corrupción

```bash
./build/catalog_corrupt flip data/catalog.bin data/catalog.corrupt 20
./build/lab2_catalog verify data/catalog.corrupt data/catalog.idx

./build/catalog_corrupt truncate data/catalog.bin data/catalog.truncated 3
./build/lab2_catalog verify data/catalog.truncated data/catalog.idx
```

## Archivos que ya están completos

- `src/binary_io.cpp`: I/O little-endian y utilidades de stream.
- `src/crc32.cpp`: CRC-32/ISO-HDLC.
- `src/catalog_codec.cpp`: codificación y decodificación del payload.
- `src/index_io.cpp`: persistencia del índice primario.
- `src/main.cpp`: interfaz de línea de comandos.
- `tools/`: generación y corrupción controlada de datos.

## Antes de entregar

Actualice este README con:

- Nombre del estudiante.
- Complejidad de las operaciones principales.
- Diferencia entre integridad física y consistencia lógica.
- Descripción de las pruebas adicionales realizadas en `tests/student_tests.cpp`.

No entregue `build/`, ejecutables ni archivos generados `.bin`, `.idx` o `.corrupt`.

## Complejidad de las operaciones principales

| Operación | Complejidad | Detalle |
|---|---|---|
| `build_primary_index` | O(n log n) | O(n) recorrido secuencial con `next_offset` + O(n log n) por `std::sort` sobre las entradas. Memoria: O(n). |
| `find_offset` / `find_record` | O(log n) + O(1) | Búsqueda binaria manual en memoria sobre el índice ordenado, seguida de un único `seek` + lectura en disco. |
| `build_composer_index` | O(n log n) | Se reúnen pares `(composer, label_id)` — O(n) — y se ordenan con `std::sort` — O(n log n) — antes de agruparlos en una sola pasada O(n). Memoria: O(n). |
| `find_by_composer` | O(log n) | Búsqueda binaria manual sobre el `ComposerIndex`, ya ordenado por compositor. |
| `verify_primary_index` | O(n log n) | Las verificaciones estructurales (`UnsortedIndex`, `DuplicateKey`, `DuplicateOffset`) ordenan copias de índices auxiliares — O(n log n) — y la auditoría contra el archivo de datos recorre cada entrada una vez — O(n). |
| `intersect_sorted` (bono) | O(n + m) | Estrategia de dos punteros (merge) sobre dos listas ya ordenadas, sin ciclos anidados. |

## Integridad física vs. consistencia lógica

**Integridad física** responde a la pregunta *¿los bytes leídos son los mismos que se escribieron?* Se detecta con el CRC-32 almacenado por registro: si un solo bit del payload cambia (corrupción de disco, escritura parcial, transferencia dañada), el CRC recalculado no coincide con el almacenado y el registro se rechaza (`ChecksumMismatch`). El truncamiento (`TruncatedHeader`, `TruncatedPayload`, `MissingChecksum`) también es un problema de integridad física: faltan bytes que deberían estar.

**Consistencia lógica** responde a *¿esta estructura tiene sentido según las reglas del sistema?*, incluso si los bytes están físicamente intactos. Ejemplos en este laboratorio: un `magic` distinto de `MUS2` o una `version` no soportada indican que se está leyendo el offset equivocado; una `payload_length` fuera de rango es una longitud absurda; una clave del índice que no coincide con la clave del registro (`KeyMismatch`) significa que el índice está desactualizado; claves duplicadas u offsets duplicados en el índice son inconsistencias del propio índice, no del archivo de datos.

Un registro puede pasar la verificación de integridad física (CRC válido) y aun así ser lógicamente inconsistente — por ejemplo, si el índice apunta al offset correcto de un registro real, pero ese registro pertenece a una clave distinta a la que el índice promete. Por eso `read_record_at` valida ambos niveles en orden estricto (primero estructura/límites, luego CRC, luego decodificación), y `verify_primary_index` audita ambos por separado en su reporte.

## Pruebas adicionales (tests/student_tests.cpp)

- **S01** — Un archivo vacío produce un índice primario vacío con estado `Ok` (no un error).
- **S02** — Un offset desalineado (que cae a mitad del header de un registro real) es rechazado por `read_record_at`, verificando la invariante de seguridad: nunca hay `Record` sin `status == Ok`.
- **S03** — Una versión de registro no soportada (`version = 2`) se detecta como `UnsupportedVersion` antes de intentar leer el payload.
- **S04** — `find_by_composer` retorna un `span` vacío cuando el compositor consultado no existe en el índice secundario.
- **S05** — `intersect_sorted` calcula correctamente la intersección de dos listas ordenadas que contienen duplicados internos, confirmando la estrategia de merge de dos punteros del bono.
