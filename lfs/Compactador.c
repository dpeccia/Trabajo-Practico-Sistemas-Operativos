#include "Compactador.h"

/* hiloCompactacion()
 * Parametros:
 * 	-> nombreTabla ::  char*
 * Descripcion: ejecuta la funcion compactar() cada cierta cantidad de tiempo (tiempoEntreCompactaciones) definido en la metadata de la tabla
 * Return:
 * 	-> :: void* */
void* hiloCompactacion(void* args) {
	char* nombreTabla;// Viene de lissandra
	char* pathTabla = string_from_format("%sTablas/%s", pathRaiz, nombreTabla);

	char* pathMetadataTabla = string_from_format("%s/Metadata.bin", pathTabla);
	t_config* configMetadataTabla = config_create(pathMetadataTabla);
	int tiempoEntreCompactaciones = config_get_int_value(configMetadataTabla, "COMPACTION_TIME");
	free(pathMetadataTabla);
	config_destroy(configMetadataTabla);

	while(1) {
		sleep(tiempoEntreCompactaciones/1000); //TODO: usleep
		char* infoComienzoCompactacion = string_from_format("Compactando tabla: %s", nombreTabla);
		log_info(logger_LFS, infoComienzoCompactacion);
		free(infoComienzoCompactacion);
		compactar(pathTabla);
		char* infoTerminoCompactacion = string_from_format("Compactacion de la tabla: %s terminada", nombreTabla);
		log_info(logger_LFS, infoTerminoCompactacion);
		free(infoTerminoCompactacion);
	}
	free(pathTabla);
}

/* compactar()
 * Parametros:
 * 	-> pathTabla ::  char*
 * Descripcion: convierte los tmp a tmpC, los lee, los compara con lo que ya hay en las particiones,
 * y mergea todos los archivos quedandose con los timestamps mas altos en caso de que se repita la key
 * Return:
 * 	-> :: void */
void compactar(char* pathTabla) {

	DIR *tabla;
	struct dirent *archivoDeLaTabla;
	t_list* registrosDeParticiones;
	t_list* particiones = list_create();
	char* puntoDeMontaje = config_get_string_value(config, "PUNTO_MONTAJE");

	// Leemos el tamanio del bloque
	char* pathMetadata = string_from_format("%sMetadata/Metadata.bin", puntoDeMontaje);
	t_config* configMetadata = config_create(pathMetadata);
	int tamanioBloque = config_get_int_value(configMetadata, "BLOCK_SIZE");
	free(pathMetadata);
	config_destroy(configMetadata);

	// Leemos el numero de particiones de la tabla
	char* pathMetadataTabla = string_from_format("%s/Metadata.bin", pathTabla);
	t_config* configMetadataTabla = config_create(pathMetadataTabla);
	int numeroDeParticiones = config_get_int_value(configMetadataTabla, "PARTITIONS");
	free(pathMetadataTabla);
	config_destroy(configMetadataTabla);

	t_registro* obtenerTimestampMasAltoSiExiste(t_registro* registroDeTmpC) {
		t_registro* registroAEscribir = malloc(sizeof(t_registro));
		int tieneMismaKey(t_registro* registroBuscado) {
			return registroDeTmpC->key == registroBuscado->key;
		}
    	t_registro* registroEncontrado = list_find(registrosDeParticiones, (void*)tieneMismaKey);
    	if(registroEncontrado != NULL) {
    		if(registroEncontrado->timestamp > registroDeTmpC->timestamp) {
    			registroAEscribir->timestamp = registroEncontrado->timestamp;
    			registroAEscribir->key = registroEncontrado->key;
    			registroAEscribir->value = strdup(registroEncontrado->value);
    		} else {
    			registroAEscribir->timestamp = registroDeTmpC->timestamp;
    			registroAEscribir->key = registroDeTmpC->key;
    			registroAEscribir->value = strdup(registroDeTmpC->value);
    		}
    	} else {
    		registroAEscribir->timestamp = registroDeTmpC->timestamp;
    		registroAEscribir->key = registroDeTmpC->key;
    		registroAEscribir->value = strdup(registroDeTmpC->value);
    	}
    	return registroAEscribir;
	}

	// Abrimos el directorio de la tabla para poder recorrerlo
	if((tabla = opendir(pathTabla)) == NULL) {
		perror("opendir");
	}

	// Renombramos los tmp a tmpc
	renombrarTmp_a_TmpC(pathTabla, archivoDeLaTabla, tabla);

	// Leemos todos los registros de los temporales a compactar y los guardamos en una lista
	t_list* registrosDeTmpC = leerDeTodosLosTmpC(pathTabla, archivoDeLaTabla, tabla, particiones, numeroDeParticiones, puntoDeMontaje);

	// Verificamos si hay datos que compactar
	if (registrosDeTmpC->elements_count != 0) {

		// Leemos todos los registros de las particiones y los guardamos en una lista
		registrosDeParticiones = leerDeTodasLasParticiones(pathTabla, particiones, puntoDeMontaje);

		// Mergeamos la lista de registros de tmpC con la lista de registros de las particiones y obtenemos una nueva lista
		// (filtrando por timestamp mas alto en caso de que hayan keys repetidas)
		t_list* registrosAEscribir = list_map(registrosDeTmpC, (void*) obtenerTimestampMasAltoSiExiste);

		// TODO: Bloquear tabla

		// Liberamos los bloques que contienen el archivo “.tmpc” y los que contienen el archivo “.bin”
		liberarBloquesDeTmpCyParticiones(pathTabla, archivoDeLaTabla, tabla, particiones, puntoDeMontaje);

		// Grabamos los datos en el nuevo archivo “.bin”
		guardarDatosNuevos(pathTabla, registrosAEscribir, particiones, tamanioBloque, puntoDeMontaje, numeroDeParticiones);

		// TODO: Desbloquear la tabla y dejar un registro de cuanto tiempo estuvo bloqueada la tabla para realizar esta operatoria

		list_destroy_and_destroy_elements(registrosDeParticiones, (void*) eliminarRegistro);
		list_destroy_and_destroy_elements(registrosAEscribir, (void*) eliminarRegistro);
	}

	// Cerramos el directorio
	if (closedir(tabla) == -1) {
		perror("closedir");
	}

	list_destroy_and_destroy_elements(registrosDeTmpC, (void*) eliminarRegistro);
	list_destroy_and_destroy_elements(particiones, (void*) eliminarParticion);
}

/* renombrarTmp_a_TmpC()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> archivoDeLaTabla :: struct dirent*
 * 	-> tabla :: DIR*
 * Descripcion: renombro los temporales del dumpeo a temporales a compactar
 * Return:
 * 	-> :: void */
void renombrarTmp_a_TmpC(char* pathTabla, struct dirent* archivoDeLaTabla, DIR* tabla) {
	while ((archivoDeLaTabla = readdir(tabla)) != NULL) {
		if (string_ends_with(archivoDeLaTabla->d_name, ".tmp")) {
			char* pathTmp = string_from_format("%s/%s", pathTabla, archivoDeLaTabla->d_name);
			char* pathTmpC = string_from_format("%s/%s%c", pathTabla, archivoDeLaTabla->d_name, 'c');
			int resultadoDeRenombrar = rename(pathTmp, pathTmpC);
			if (resultadoDeRenombrar != 0) {
				log_error(logger_LFS, "No se pudo renombrar el temporal");
			}
			free(pathTmp);
			free(pathTmpC);
		}
	}
	rewinddir(tabla);
}

/* leerDeTodosLosTmpC()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> archivoDeLaTabla :: struct dirent*
 * 	-> tabla :: DIR*
 * 	-> particiones :: t_list*
 * 	-> numeroDeParticiones :: int
 * 	-> puntoDeMontaje :: char*
 * Descripcion: leo todos los registros de todos los tmpC y devuelvo una lista con todos los registros
 * Return:
 * 	-> :: t_list* */
t_list* leerDeTodosLosTmpC(char* pathTabla, struct dirent* archivoDeLaTabla, DIR* tabla, t_list* particiones, int numeroDeParticiones, char* puntoDeMontaje) {

	t_registro* tRegistro;
	t_int* particion;
	int particionDeEstaKey;
	t_list* registrosDeTmpC = list_create();

	int existeParticion(t_int* particionAComparar) {
		return particionAComparar->valor == particionDeEstaKey;
	}

	int tieneMismaKey(t_registro* registroBuscado) {
		return tRegistro->key == registroBuscado->key;
	}

	while ((archivoDeLaTabla = readdir(tabla)) != NULL) {

		if (string_ends_with(archivoDeLaTabla->d_name, ".tmpc")) {

			char* pathTmpC = string_from_format("%s/%s", pathTabla, archivoDeLaTabla->d_name);
			t_config* configTmpC = config_create(pathTmpC);
			char** bloques = config_get_array_value(configTmpC, "BLOCKS");
			free(pathTmpC);
			config_destroy(configTmpC);

			int i = 0;
			int j = 0;

			char* registro = calloc(1, 27 + config_get_int_value(config, "TAMAÑO_VALUE"));
			// 65635 como maximo para el key van a ser 5 bytes y 18.446.744.073.709.551.616 para el timestamp son 20 bytes + 2 punto y coma
			// 5 bytes son 5 char

			while (bloques[i] != NULL) {
				char* pathBloque = string_from_format("%sBloques/%s.bin", puntoDeMontaje, bloques[i]);
				FILE* bloque = fopen(pathBloque, "r");
				free(pathBloque);
				if (bloque == NULL) {
					perror("Error");
				}
				do {
					char caracterLeido = fgetc(bloque);
					if (feof(bloque)) {
						break;
					}
					if (caracterLeido == '\n') {
						char** registroSeparado = string_n_split(registro, 3, ";");
						tRegistro = (t_registro*) malloc(sizeof(t_registro));
						convertirTimestamp(registroSeparado[0], &(tRegistro->timestamp));
						tRegistro->key = convertirKey(registroSeparado[1]);
						tRegistro->value = strdup(registroSeparado[2]);

						liberarArrayDeChar(registroSeparado);

						particionDeEstaKey = tRegistro->key % numeroDeParticiones;

						particion = list_find(particiones, (void*) existeParticion);
						if (particion == NULL) {
							t_int* particionAAgregar = malloc(sizeof(t_int*));
							particionAAgregar->valor = particionDeEstaKey;
							list_add(particiones, particionAAgregar);
						}

						t_registro* registroEncontrado = list_find(registrosDeTmpC, (void*) tieneMismaKey);
						if (registroEncontrado != NULL) {
							if (tRegistro->timestamp > registroEncontrado->timestamp) {
								list_remove_and_destroy_by_condition(registrosDeTmpC, (void*) tieneMismaKey, (void*) eliminarRegistro);
								list_add(registrosDeTmpC, tRegistro);
							}
						} else {
							list_add(registrosDeTmpC, tRegistro);
						}
						j = 0;
						strcpy(registro, "");
					} else {
						registro[j] = caracterLeido;
						j++;
					}
				} while (1);
				fclose(bloque);
				i++;
			}
			liberarArrayDeChar(bloques);
			free(registro);
		}
	}
	rewinddir(tabla);
	return registrosDeTmpC;
}

/* leerDeTodasLasParticiones()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> particiones :: unit16*
 * 	-> puntoDeMontaje :: char*
 * Descripcion: leo todos los registros de todas las particiones (que esten en el tmpC) y devuelvo una lista con todos los registros
 * Return:
 * 	-> :: t_list* */
t_list* leerDeTodasLasParticiones(char* pathTabla, t_list* particiones, char* puntoDeMontaje) {

	t_int* particion;
	t_registro* tRegistro;
	t_list* registrosDeParticiones = list_create();

	for (int i = 0; list_get(particiones, i) != NULL; i++) {
		particion = list_get(particiones, i);

		char* pathParticion = string_from_format("%s/%d.bin", pathTabla, particion->valor);
		t_config* particionConfig = config_create(pathParticion);
		char** bloques = config_get_array_value(particionConfig, "BLOCKS");
		free(pathParticion);
		config_destroy(particionConfig);

		char* registro = calloc(1, 27 + config_get_int_value(config, "TAMAÑO_VALUE"));
		// 65635 como maximo para el key van a ser 5 bytes y 18.446.744.073.709.551.616 para el timestamp son 20 bytes + 2 punto y coma
		// 5 bytes son 5 char

		int z = 0;
		int j = 0;
		while (bloques[z] != NULL) {
			char* pathBloque = string_from_format("%sBloques/%s.bin", puntoDeMontaje, bloques[z]);
			FILE* bloque = fopen(pathBloque, "r");
			if (bloque == NULL) {
				perror("Error");
			}
			do {
				char caracterLeido = fgetc(bloque);
				if (feof(bloque)) {
					break;
				}
				if (caracterLeido == '\n') {
					char** registroSeparado = string_n_split(registro, 3, ";");
					tRegistro = (t_registro*) malloc(sizeof(t_registro));
					convertirTimestamp(registroSeparado[0], &tRegistro->timestamp);
					tRegistro->key = convertirKey(registroSeparado[1]);
					tRegistro->value = strdup(registroSeparado[2]);
					liberarArrayDeChar(registroSeparado);
					list_add(registrosDeParticiones, tRegistro);
					j = 0;
					strcpy(registro, "");
				} else {
					registro[j] = caracterLeido;
					j++;
				}
			} while (1);
			free(pathBloque);
			fclose(bloque);
			z++;
		}
		free(registro);
		liberarArrayDeChar(bloques);
	}
	return registrosDeParticiones;
}

/* liberarBloquesDeTmpCyParticiones()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> archivoDeLaTabla :: unit16*
 * 	-> tabla :: DIR*
 * 	-> particiones :: t_list*
 * 	-> puntoDeMontaje :: char*
 * Descripcion: libero todos los bloques de los tmpC y de las particiones (que esten en el tmpC)
 * Return:
 * 	-> :: void */
void liberarBloquesDeTmpCyParticiones(char* pathTabla, struct dirent* archivoDeLaTabla, DIR* tabla, t_list* particiones, char* puntoDeMontaje) {

	while ((archivoDeLaTabla = readdir(tabla)) != NULL) {
		if (string_ends_with(archivoDeLaTabla->d_name, ".tmpc")) {
			char* pathTmpC = string_from_format("%s/%s", pathTabla, archivoDeLaTabla->d_name);
			t_config* configTmpC = config_create(pathTmpC);
			char** bloques = config_get_array_value(configTmpC, "BLOCKS");
			config_destroy(configTmpC);
			int i = 0;
			while (bloques[i] != NULL) {
				char* pathBloque = string_from_format("%sBloques/%s.bin", puntoDeMontaje, bloques[i]);
				FILE* bloque = fopen(pathBloque, "w");
				bitarray_clean_bit(bitarray, (int) strtol(bloques[i], NULL, 10));
				free(pathBloque);
				fclose(bloque);
				i++;
			}
			liberarArrayDeChar(bloques);
			remove(pathTmpC);
			free(pathTmpC);
		}

		if (string_ends_with(archivoDeLaTabla->d_name, ".bin") && !string_equals_ignore_case(archivoDeLaTabla->d_name, "Metadata.bin")) {
			char** numeroDeParticionString = string_split(archivoDeLaTabla->d_name, ".");
			int numeroDeParticion = strtol(numeroDeParticionString[0], NULL, 10);
			liberarArrayDeChar(numeroDeParticionString);

			int particionActual(t_int* particion_actual) {
				return particion_actual->valor == numeroDeParticion;
			}

			t_int* particionEncontrada = list_find(particiones, (void*) particionActual);
			if (particionEncontrada != NULL) {
				char* particionPath = string_from_format("%s/%s", pathTabla, archivoDeLaTabla->d_name);
				t_config* configParticion = config_create(particionPath);
				free(particionPath);
				char** bloques = config_get_array_value(configParticion, "BLOCKS");
				config_destroy(configParticion);
				int i = 0;
				while (bloques[i] != NULL) {
					char* pathBloque = string_from_format("%sBloques/%s.bin", puntoDeMontaje, bloques[i]);
					FILE* bloque = fopen(pathBloque, "w");
					bitarray_clean_bit(bitarray, (int) strtol(bloques[i], NULL, 10));
					free(pathBloque);
					fclose(bloque);
					i++;
				}
				liberarArrayDeChar(bloques);
			}
		}
	}
}

/* guardarDatosNuevos()
 * Parametros:
 * 	-> pathTabla ::  char*
 * 	-> registrosAEscribir :: unit16*
 * 	-> particiones :: t_list*
 * 	-> tamanioBloque :: int
 * 	-> puntoDeMontaje :: char*
 * 	-> numeroDeParticiones :: int
 * Descripcion: pido bloques nuevos para cada particion y guardo los datos nuevos
 * Return:
 * 	-> :: void */
void guardarDatosNuevos(char* pathTabla, t_list* registrosAEscribir, t_list* particiones, int tamanioBloque, char* puntoDeMontaje, int numeroDeParticiones) {

	errorNo error;
	t_int* particion;

	int keyCorrespondeAParticion(t_registro* registro) {
		return particion->valor == registro->key % numeroDeParticiones;
	}

	for (int i = 0; list_get(particiones, i) != NULL; i++) {
		particion = list_get(particiones, i);
		char* pathParticion = string_from_format("%s/%d.bin", pathTabla, particion->valor);
		t_config* configParticion = config_create(pathParticion);
		free(pathParticion);

		t_list* registrosPorParticion = list_filter(registrosAEscribir, (void*) keyCorrespondeAParticion);

		char* datosACompactar = calloc(1, 27 + config_get_int_value(config, "TAMAÑO_VALUE"));
		// 65635 como maximo para el key van a ser 5 bytes y 18.446.744.073.709.551.616 para el timestamp son 20 bytes + 2 punto y coma
		// 5 bytes son 5 char

		for (int j = 0; list_get(registrosPorParticion, j) != NULL; j++) {
			t_registro* registro = list_get(registrosPorParticion, j);
			string_append_with_format(&datosACompactar, "%llu;%u;%s\n", registro->timestamp, registro->key, registro->value);
		}

		list_destroy(registrosPorParticion);

		int cantidadDeBloquesAPedir = strlen(datosACompactar) / tamanioBloque;
		if (strlen(datosACompactar) % tamanioBloque != 0) {
			cantidadDeBloquesAPedir++;
		}
		char* tamanioDatos = string_from_format("%d", strlen(datosACompactar));
		config_set_value(configParticion, "SIZE", tamanioDatos);
		free(tamanioDatos);

		char* bloques = strdup("");
		for (int i = 0; i < cantidadDeBloquesAPedir; i++) {
			int bloqueDeParticion = obtenerBloqueDisponible(&error); //si hay un error se setea en errorNo
			if (bloqueDeParticion == -1) {
				log_info(logger_LFS, "no hay bloques disponibles");
			} else {
				if (i == cantidadDeBloquesAPedir - 1) {
					string_append_with_format(&bloques, "%d", bloqueDeParticion);
				} else {
					string_append_with_format(&bloques, "%d,", bloqueDeParticion);
				}
				char* pathBloque = string_from_format("%sBloques/%d.bin", puntoDeMontaje, bloqueDeParticion);
				FILE* bloque = fopen(pathBloque, "a+");
				free(pathBloque);
				if (cantidadDeBloquesAPedir != 1 && i < cantidadDeBloquesAPedir - 1) {
					char* registrosAEscribirString = string_substring_until(datosACompactar, tamanioBloque);
					char* stringAuxiliar = string_substring_from(datosACompactar, tamanioBloque);
					free(datosACompactar);
					datosACompactar = stringAuxiliar;
					fprintf(bloque, "%s", registrosAEscribirString);
					free(registrosAEscribirString);
				} else {
					fprintf(bloque, "%s", datosACompactar);
				}
				fclose(bloque);
			}
		}
		char* blocks = string_from_format("[%s]", bloques);
		config_set_value(configParticion, "BLOCKS", blocks);
		free(blocks);
		free(bloques);
		free(datosACompactar);
		config_save(configParticion);
		config_destroy(configParticion);
	}
}

/* eliminarParticion()
 * Parametros:
 * 	-> particionAEliminar ::  t_int*
 * Descripcion: libero la memoria de un t_int* (particion)
 * Return:
 * 	-> :: void */
void eliminarParticion(t_int* particionAEliminar) {
	free(particionAEliminar);
}

/* eliminarRegistro()
 * Parametros:
 * 	-> registroAEliminar ::  t_registro*
 * Descripcion: libero la memoria de un t_registro* (registro)
 * Return:
 * 	-> :: void */
void eliminarRegistro(t_registro* registroAEliminar) {
	free(registroAEliminar->value);
	free(registroAEliminar);
}
