#include "Dump.h"

/* hiloDump()
 * Parametros: void
 * Descripcion: ejecuta la funcion dumpear() cada cierta cantidad de tiempo (tiempoDump) definido en el config
 * Return: void* */
void* hiloDump(void* args) {
	int tiempoDump = config_get_int_value(config, "TIEMPO_DUMP");
	while(1) {
		sleep(tiempoDump/1000);
		log_info(logger_LFS, "Dump iniciado");
		errorNo resultado = dumpear();
		switch(resultado) {
			case ERROR_CREANDO_ARCHIVO:
				log_info(logger_LFS, "Error creando archivo temporal");
				break;
			case SUCCESS:
				log_info(logger_LFS, "Dump exitoso");
				break;
			default: break;
		}
	}
}

/* dumpear() [VALGRINDEADO]
 * Parametros: void
 * Descripcion: baja los datos de la memtable a disco
 * Return: codigo de error definido en el enum errorNo
 * Parametros: void
 * Descripcion: baja los datos de la memtable a disco
 * Return: codigo de error definido en el enum errorNo */
errorNo dumpear() {
	t_tabla* tabla;
	errorNo error = SUCCESS;
	char* pathTmp;
	FILE* fileTmp;
	char* puntoDeMontaje = config_get_string_value(config, "PUNTO_MONTAJE");
	char* pathMetadata = string_from_format("%sMetadata/Metadata.bin", puntoDeMontaje);
	t_config* configMetadata = config_create(pathMetadata);
	free(pathMetadata);
	int tamanioBloque = config_get_int_value(configMetadata, "BLOCK_SIZE");
	config_destroy(configMetadata);
	// Refactor list_iterate
	for(int i = 0; list_get(memtable->tablas,i) != NULL; i++) { // Recorro las tablas de la memtable
		tabla = list_get(memtable->tablas,i);
		char* pathTabla = string_from_format("%sTablas/%s", pathRaiz, tabla->nombreTabla);
		DIR *dir = opendir(pathTabla);
		if(dir) { // Verificamos que exista la tabla (por si hubo un DROP en el medio)
			int numeroTemporal = 0;
			do { // Nos fijamos que numero de temporal crear
				pathTmp = string_from_format("%s/%d.tmp", pathTabla, numeroTemporal);
				fileTmp = fopen(pathTmp, "r");
				numeroTemporal++;
				if(fileTmp != NULL) {
					free(pathTmp);
					fclose(fileTmp);
				}
			} while(fileTmp != NULL);
			free(pathTabla);
			fileTmp = fopen(pathTmp, "a+");
			free(pathTmp);
			if (fileTmp == NULL) {
				error = ERROR_CREANDO_ARCHIVO;
			} else {
				// Guardo lo de la tabla en el archivo temporal
				char* datosADumpear = malloc(sizeof(uint16_t) + (size_t) config_get_int_value(config, "TAMAÑO_VALUE") + sizeof(unsigned long long));
				strcpy(datosADumpear, "");
				for(int j = 0; list_get(tabla->registros,j) != NULL; j++) {
					t_registro* registro = list_get(tabla->registros,j);
					string_append_with_format(&datosADumpear, "%u;%s;%llu\n", registro->key, registro->value, registro->timestamp);
				}
				int cantidadDeBloquesAPedir = strlen(datosADumpear) / tamanioBloque;
				if(strlen(datosADumpear) % tamanioBloque != 0) {
					cantidadDeBloquesAPedir++;
				}
				char* tamanioTmp = string_from_format("SIZE=%d", strlen(datosADumpear));
				char* bloques = strdup("");
				for(int i=0; i<cantidadDeBloquesAPedir;i++) {
					int bloqueDeParticion = obtenerBloqueDisponible(&error); //si hay un error se setea en errorNo
					if(bloqueDeParticion == -1){
						log_info(logger_LFS, "no hay bloques disponibles");
					} else {
						if(i==cantidadDeBloquesAPedir-1) {
							string_append_with_format(&bloques, "%d", bloqueDeParticion);
						} else {
							string_append_with_format(&bloques, "%d,", bloqueDeParticion);
						}
						char* pathBloque = string_from_format("%sBloques/%d.bin", puntoDeMontaje, bloqueDeParticion);
						FILE* bloque = fopen(pathBloque, "a+");
						if(cantidadDeBloquesAPedir != 1 && i < cantidadDeBloquesAPedir - 1) {
							char* registrosAEscribir = string_substring_until(datosADumpear, tamanioBloque);
							datosADumpear = string_substring_from(datosADumpear, tamanioBloque);
							fprintf(bloque, "%s", registrosAEscribir);
							free(registrosAEscribir);
						} else {
							fprintf(bloque, "%s", datosADumpear);
						}
						fclose(bloque);
						free(pathBloque);
					}
				}
				char* bloquesTmp = string_from_format("BLOCKS=[%s]", bloques);
				free(bloques);
				fprintf(fileTmp, "%s\n%s", tamanioTmp, bloquesTmp);
				free(bloquesTmp);
				free(tamanioTmp);
				free(datosADumpear);
				fclose(fileTmp);
				// Vacio la memtable
				list_clean_and_destroy_elements(memtable->tablas, (void*) vaciarTabla);
			}
		}
		closedir(dir);
	}
	return error;
}