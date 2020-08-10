# Lissandra File System

#### Trabajo Práctico Grupal realizado por el grupo _Bugbusters_ para la materia _Sistemas Operativos_ en la UTN FRBA - 1er cuatrimestre 2019

Consiste en una simulación de ciertos aspectos de una base de datos distribuida, donde los distintos recursos del sistema pueden realizar una carga de trabajo ejecutando desde distintos puntos.
Los componentes incluidos dentro de la arquitectura del sistema están preparados para trabajar en conjunto para la planificación y ejecución de distintos scripts, conformados por múltiples Requests (operaciones como leer valores, escribirlos, crear tablas, etc), los cuales se asemejan a las principales operaciones de una base de datos.

Componentes del sistema:
* **_Kernel_**: punto de partida del sistema, tiene la función de recibir requests y distribuir la carga en las distintas memorias que administra
* **_Pool de memorias_**: se comunican internamente entre ellas utilizando _gossiping_. Cada memoria es asignada dinámicamente en tiempo de ejecución a un criterio de elección (el sistema puede estar funcionando y nuevas memorias pueden unirse). La función de éstas es almacenar temporalmente los datos de los request que se le soliciten, manejando para esto una memoria principal
* **_File system_**: es el encargado de guardar físicamente los archivos del sistema. Cada uno de estos archivos está asociado a una _Tabla_ que posee distintos registros en forma de filas

Los Request principales que le podemos solicitar al sistema son:
* **SELECT**: Permite la obtención del valor de una key dentro de una tabla
* **INSERT**: Permite la creación y/o actualización de una key dentro de una tabla
* **CREATE**: Permite la creación de una nueva tabla dentro del file system
* **DESCRIBE**: Permite obtener la Metadata de una tabla en particular o de todas las tablas que el file system tenga
* **DROP**: Permite la eliminación de una tabla del file system
