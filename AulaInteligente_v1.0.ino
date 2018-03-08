#include <energyic_UART.h>

//Tareas:
//      Poner cartel de encendiendo... mientras se envian datos al servidor, para no confundir al usuario. HECHO
//      Hacer promedio de la temperatura

//Librerias utilizadas

///////////////////////////////////////// RFID ///////////////////////////////////
#include <EEPROM.h>     // We are going to read and write PICC's UIDs from/to EEPROM
#include <SPI.h>        // RC522 Module uses SPI protocol
#include <MFRC522.h>  // Library for Mifare RC522 Devices

boolean match = false;          // initialize card match to false
boolean programMode = false;  // initialize programming mode to false
boolean replaceMaster = false;
boolean aulaEnUso = false; //inicializo la variable aula en uso en falso

uint8_t successRead;    // Variable integer to keep if we have Successful Read from Reader

byte storedCard[4];   // Stores an ID read from EEPROM
byte readCard[4];   // Stores scanned ID read from RFID Module
byte masterCard[4];   // Stores master card's ID read from EEPROM
byte actualCard[4];  //Guarda el usuario actual que está utilizando el sistema

// Create MFRC522 instance.
#define SS_PIN 3
#define RST_PIN 2
MFRC522 mfrc522(SS_PIN, RST_PIN);

//////////////////////////////////// DISPLAY //////////////////////////////////////////////

#include <TFT_HX8357.h> // Libreria para el display
TFT_HX8357 tft = TFT_HX8357();       // Invocacion de la libreria


///////////////////////////////// TECLADO /////////////////////////////////////////////
#include <Keypad.h> // Libreria para el teclado matricial
const byte Filas = 4;  //Cuatro filas
const byte Cols = 4;   //Cuatro columnas
byte Pins_Filas[] = {11, 10, 9, 8}; //Pines Arduino a las filasbyte
byte Pins_Cols[] = {7, 6, 5, 4}; // Pines Arduino a las columnas.
//no utilizar los pines 1 y 0 para no interferir en Rx y Tx

char Teclas [ Filas ][ Cols ] =
{
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

Keypad Teclado1 = Keypad(makeKeymap(Teclas), Pins_Filas, Pins_Cols, Filas, Cols);

boolean luces = false;
boolean proyector = false;
boolean aire = false;

///////////////////////////// ETHERNET /////////////////////////////////////////////////
#include <SPI.h>
#include <Ethernet.h>

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
// if you don't want to use DNS (and reduce your sketch size)
// use the numeric IP instead of the name for the server:
//IPAddress server(74,125,232,128);  // numeric IP for Google (no DNS)
char server[] = "aula-inteligente.herokuapp.com";    // name address for Google (using DNS)

// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(192, 168, 0, 177);

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client;

///////////////////////////////////// DECLARACION DE VARIABLES Y CONSTANTES ///////////////////////////////////////////
String lastRfid = "";
String activoRfid = ""; //alamcena el usuario que etsa usando el sistema
String url;
String consumo = "0";
String aula_id = "1";

int voutTemp = 1;      //pin entrada analoga A1 para temperatura
int lecturaTemp;                    //lectura del pin vout (A1)
float tensionTemp, temperatura;     //variables para la conversion a tension y temperatura

//Declaración de los pines utilizados
const int sensor1 = 20;  // pin 20 para el sensor de movimiento 1
const int sensor2 = 21; // pin 21 para el sensor de movimiento 2

//Declaracion de intervalos de tiempo
const long intervaloLuces = 300000; //Intervalo para el apagado de las luces
const long intervaloTemp = 600000;

//Declaracion de variables
boolean usuarioRegistrado = false;
boolean enviaTemperatura = true;

long previoMillis = 0; //para guardar el ultimo momento en el que se detecto movimiento
long previoMillisTemp = 0; //para guardar el ultimo momento en el que se enviaron los datos de la temperatura


///////////////////////////////////////// RELE //////////////////////////////////////
#define relePin1 16
#define relePin2 17
#define relePin3 18
#define relePin4 19
#define RELAY_ON 0
#define RELAY_OFF 1

//////////////////////////////// SENSOR DE CORRIENTE ///////////////////////////////
float sensibilidad = 0.066; //sensibilidad en Voltios/Amperio para sensor de 30A
int voutCorriente = 2;      //pin entrada analoga A2 para sensor de corriente
float lecturaCorriente;
float corriente;
int nMuestras = 200;
int muestras = 0;
int tension = 220;
double potencia = 0;
int consumoParcial = 0;

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  pinMode(relePin1, OUTPUT);
  pinMode(relePin2, OUTPUT);
  pinMode(relePin3, OUTPUT);
  pinMode(relePin4, OUTPUT);
  digitalWrite (relePin1, RELAY_OFF);
  digitalWrite (relePin2, RELAY_OFF);
  digitalWrite (relePin3, RELAY_OFF);
  digitalWrite (relePin4, RELAY_OFF);

  //Inicializacion del display
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_NAVY); // Limpia el display

  tft.fillScreen(TFT_BLACK);
  header("     Aula Inteligente     ");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM); // Centre text on x,y position
  SPI.begin();           // MFRC522 Hardware uses SPI protocol
  mfrc522.PCD_Init();    // Initialize MFRC522 Hardware
  pinMode (sensor1 , INPUT); //Declaro el pin del sensor1 como entrada

  Serial.println(F("Aula Inteligente v0.1"));   // For debugging purposes
  ShowReaderDetails();  // Show details of PCD - MFRC522 Card Reader details
  // Check if master card defined, if not let user choose a master card
  // This also useful to just redefine the Master Card
  // You can keep other EEPROM records just write other than 143 to EEPROM address 1
  // EEPROM address 1 should hold magical number which is '143'


  if (EEPROM.read(1) != 143) {
    Serial.println(F("No se ha definido tarjeta maestro"));
    Serial.println(F("Escane una PICC para definirla como maestro"));
    do {
      successRead = getID();            // sets successRead to 1 when we get read from reader otherwise 0
    }
    while (!successRead);                  // Program will not go further while you not get a successful read
    for ( uint8_t j = 0; j < 4; j++ ) {        // Loop 4 times
      EEPROM.write( 2 + j, readCard[j] );  // Write scanned PICC's UID to EEPROM, start from address 3
    }
    EEPROM.write(1, 143);                  // Write to EEPROM we defined Master Card.
    Serial.println(F("Tarjeta maestra definida"));
  }
  Serial.println(F("-------------------"));
  Serial.println(F("UID de la Tarjeta Maestro"));
  for ( uint8_t i = 0; i < 4; i++ ) {          // Read Master Card's UID from EEPROM
    masterCard[i] = EEPROM.read(2 + i);    // Write it to masterCard
    Serial.print(masterCard[i], HEX);
  }
  Serial.println("");
  Serial.println(F("-------------------"));
  Serial.println(F("Todo listo"));
  Serial.println(F("Esperando PICCs para ser escaneadas"));

  // Inicializo el medidor de energia
  InitEnergyIC();
}

void loop() {
  esperando();
  do {
    successRead = getID();  // sets successRead to 1 when we get read from reader otherwise 0   
    ///////////////////////////////////////// CONTROL DE MOVIMIENTO  SI LAS LUCES QUEDARON ENCENDIDAS////////////////////////////////////////////////////////
    if(luces || aire || proyector){
          int valorSensor1 = digitalRead(sensor1); //Leo el valor del sensor 1
        if (valorSensor1 == HIGH)  //Si esta en alto = movimiento
        {
          //Serial.println("Se detecta movimiento");
          previoMillis = millis();  //Guardo el instante en que se detecto movimiento
        }
        if (millis() - previoMillis  > intervaloLuces) { //Si el instante actual menos el instante en que ultima vez se detecto movimiento es mayo al intervalo de tiempo establecido
          Serial.println("Se supero el tiempo establecido sin detectar movimiento, se procede a apagar dispositivos!");
          if (luces) {
            releta('1');
            luces = false;
            informoActividadDispositivo("estadoLuces", "Apagadas");
          }
          
          if (aire) {
            releta('3');
            aire = false;
            informoActividadDispositivo("estadoAire", "Apagado");
          }

          if(proyector){
            releta('2');
            proyector = false;
            informoActividadDispositivo("estadoProyector", "Apagado");
          }
                  
        }
    } 
    
  }
  while (!successRead);   //Nos avanzara de aca hasta conseguir una lectura de tarjeta exitosa
  
  if (programMode) {
    if ( isMaster(readCard) ) { //When in program mode check First If master card scanned again to exit program mode
      Serial.println(F("Tarjeta maestro escaneada"));
      Serial.println(F("Saliendo del Modo Programacion"));
      Serial.println(F("-----------------------------"));
      programMode = false;
      return;
    }
    else {
      if ( findID(readCard) ) { // If scanned card is known delete it
        Serial.println(F("Conozco esta PICC, removiendo..."));
        deleteID(readCard);
        Serial.println("-----------------------------");
        Serial.println(F("Escane una PICC para AGREGAR o REMOVER de la EEPROM"));
      }
      else {                    // If scanned card is not known add it
        Serial.println(F("No conozco esta PICC, agregando..."));
        writeID(readCard);
        Serial.println(F("-----------------------------"));
        Serial.println(F("Escane una PICC para AGREGAR o REMOVER de la EEPROM"));
      }
    }
  }
  else {
    if ( isMaster(readCard)) {    // If scanned card's ID matches Master Card's ID - enter program mode
      programMode = true;
      Serial.println(F("Hola Administrador - Modo Programacion"));
      uint8_t count = EEPROM.read(0);   // Read the first Byte of EEPROM that
      Serial.print(F("Tengo "));     // stores the number of ID's in EEPROM
      Serial.print(count);
      Serial.print(F(" grabada(s) en la EEPROM"));
      Serial.println("");
      Serial.println(F("Escane una PICC para AGREGAR o REMOVER de la EEPROM"));
      Serial.println(F("Escane la tarjeta Maestro nuevamente para Salir del modo Programacion"));
      Serial.println(F("-----------------------------"));
    }
    else {
      if ( findID(readCard) ) { // If not, see if the card is in the EEPROM
        Serial.println(F("Bienvenido, usted puede ingresar"));
        for ( uint8_t j = 0; j < 4; j++ ) {        // Loop 4 times
          actualCard[j] = readCard[j];  // Guardo el usuario actual del sistema
        }
        granted();
      }
      else {      // If not, show that the ID was not valid
        Serial.println(F("Usted no puede ingresar, no se encuentra registrado"));
        denied();
      }
    }
  }
}

/////////////////////////////////////////  ACCESO CONCEDIDO    //////////////////////////////////////////////
void granted () {
  //ACCIONES PARA USUARIO REGISTRADO
  //Enviar datos al servidor
  //Intentar leer tarjeta RFID
  //Comenzar a medir el consumo
  //Encendido de dispositivos
  //Control de movimiento y sensores

  aulaEnUso = true; //Pongo el aula en uso
  consumoParcial = 0; //Pongo el consumo parcial en 0
  consumo = String(consumoParcial);
  activoRfid = lastRfid; //Establezco la tarjeta RFID actual
  bienvenida();

  /////////////////////////////////////// ENVIO DATOS DE ENTRADA AL SERVIDOR //////////////////////////////
  //Ethernet.begin(mac, timeout, responseTimeout); timeouts are in ms <- Para que no se quede en un loop en el caso de que no haya conexion  internet
  if (Ethernet.begin(mac, 10000, 10000) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // try to congifure using IP address instead of DHCP:
    Ethernet.begin(mac, ip);
  }
  // give the Ethernet shield a second to initialize:
  delay(1000);
  Serial.println("connecting...");

  // if you get a connection, report back via serial:
  if (client.connect(server, 80)) {
    Serial.println("connected. Enviando datos de entrada al servidor...");
    // Make a HTTP request:
    url = "GET /nuevoIngreso?ingreso[HoraEntrada]=1&ingreso[HoraSalida]=9999&ingreso[ConsumoParcial]=" + consumo + "&ingreso[aula_id]=1&ingreso[codigo]=" + lastRfid + " HTTP/1.1";
    client.println(url);
    Serial.println(url);
    client.println("Host: aula-inteligente.herokuapp.com");
    client.println("Connection: close");
    client.println();
  } else {
    // if you didn't get a connection to the server:
    Serial.println("connection failed");
  }

  Serial.println("disconnecting.");
  client.stop();
  
  tft.fillScreen(TFT_BLACK); //Limpio el display
  opciones();

  ////////////////////////////////////////// MIENTRAS EL AULA ESTE EN USO /////////////////////////////////////////////////////// 
  while (aulaEnUso) { 
    
    medicionTemperatura();
    
    medicionElectrica();

    ////////////////////////////////////////// Encendido y apagado de dispositivos /////////////////////////////////////////////  
    //Encendido de dispositivos solicitados
    char pulsacion = Teclado1.getKey() ; //Leo la tecla pulsada
    
    if (pulsacion != 0) {
      if (pulsacion == '1') {
        if (!luces) {
          releta('1');
          luces = true;
          encendiendoDispositivo();
          informoActividadDispositivo("estadoLuces", "Encendidas");
          cambioLuces();
        }
        else {
          releta('1');
          luces = false;
          apagandoDispositivo();
          informoActividadDispositivo("estadoLuces", "Apagadas");
          cambioLuces();
        }
      }

      if (pulsacion == '2') {
        if (!proyector) {
          releta('2');
          proyector = true;
          encendiendoDispositivo();
          informoActividadDispositivo("estadoProyector", "Encendido");
          cambioProyector();
        }
        else {
          releta('2');
          proyector = false;
          apagandoDispositivo();        
          informoActividadDispositivo("estadoProyector", "Apagado");
          cambioProyector();
        }
      }

      if (pulsacion == '3') {
        if (!aire) {
          releta('3');
          aire = true;
          encendiendoDispositivo(); 
          informoActividadDispositivo("estadoAire", "Encendido");
          cambioAire();
        }
        else {
          releta('3');
          aire = false;
          apagandoDispositivo(); 
          informoActividadDispositivo("estadoAire", "Apagado");
          cambioAire();
        }
      }
    }

    /////////////////////////////////////// LECTURA DE TARJETA /////////////////////////////////////  
    successRead = getID(); //Intento leer alguna tarjeta
    if (successRead) { //Si se lee una tarjeta
      if (findID(readCard)) { //Si la tarjeta esta registrada
        consumo = String(consumoParcial);
        if (checkTwo(readCard, actualCard)) { // Si es el usuario actual
          Serial.print(F("Hasta luego. Gracias por usar el sistema!"));
          despedida();
          aulaEnUso = false; // Deja de usar el aula

          /////////////////////////////////////// ENVIO DATOS DE SALIDA DE USUARIO AL SERVIDOR //////////////////////////////
          // start the Ethernet connection:
          if (Ethernet.begin(mac, 10000, 10000) == 0) {
            Serial.println("Failed to configure Ethernet using DHCP");
            // try to congifure using IP address instead of DHCP:
            Ethernet.begin(mac, ip);
          }
          // give the Ethernet shield a second to initialize:
          delay(1000);
          Serial.println("connecting...");

          // if you get a connection, report back via serial:
          if (client.connect(server, 80)) {
            Serial.println("connected. Enviando datos de salida al servidor...");
            // Make a HTTP request:
            url = "GET /nuevoIngreso?ingreso[HoraEntrada]=9999&ingreso[HoraSalida]=1&ingreso[ConsumoParcial]=" +  consumo + "&ingreso[aula_id]=1&ingreso[codigo]=" + activoRfid + " HTTP/1.1";
            client.println(url);
            client.println("Host: aula-inteligente.herokuapp.com");
            client.println("Connection: close");
            client.println();
          } else {
            // if you didn't get a connection to the server:
            Serial.println("connection failed");
          }

          Serial.println("disconnecting.");
          client.stop();
        }
        
        else { //Si no es el usuario actual
          Serial.print(F("El usuario anterior no cerro su sesion!"));
          usuarioActivo();
          
          ///////////////////////////// CIERRO LA SESION DEL USUARIO ANTERIOR EN EL SERVIDOR, ENVIO CONSUMO Y LO  PONGO EN 0 PARA EL NUEVO USR //////////////////////////////////
          // start the Ethernet connection:
          if (Ethernet.begin(mac, 10000, 10000) == 0) {
            Serial.println("Failed to configure Ethernet using DHCP");
            // try to congifure using IP address instead of DHCP:
            Ethernet.begin(mac, ip);
          }
          // give the Ethernet shield a second to initialize:
          delay(1000);
          Serial.println("connecting...");

          // if you get a connection, report back via serial:
          if (client.connect(server, 80)) {
            Serial.println("connected. Enviando datos de salida al servidor...");
            // Make a HTTP request:
            url = "GET /nuevoIngreso?ingreso[HoraEntrada]=9999&ingreso[HoraSalida]=1&ingreso[ConsumoParcial]=" +  consumo + "&ingreso[aula_id]=1&ingreso[codigo]=" + activoRfid + " HTTP/1.1";
            client.println(url);
            Serial.println(url);
            client.println("Host: aula-inteligente.herokuapp.com");
            client.println("Connection: close");
            client.println();
          } else {
            // if you didn't get a connection to the server:
            Serial.println("connection failed");
          }

          Serial.println("disconnecting.");
          client.stop();                   
           
          consumoParcial = 0; //Reseteo el contador de consumo
          consumo = String(consumoParcial);
          //Guardo el nuevo usuario
          for ( uint8_t j = 0; j < 4; j++ ) {        // Loop 4 times
            actualCard[j] = readCard[j];  // Guardo el usuario actual del sistema
          }
          activoRfid = lastRfid;
          
          usuarioCambiado();

          ///////////////////////////// INICIO LA SESION DEL NUEVO USUARIO EN EL SERVIDOR //////////////////////////////////////
            // start the Ethernet connection:
            //Ethernet.begin(mac, timeout, responseTimeout); timeouts are in ms <- Para que no se quede en un loop en el caso de que no haya conexion  internet
            if (Ethernet.begin(mac, 10000, 10000) == 0) {
              Serial.println("Failed to configure Ethernet using DHCP");
              // try to congifure using IP address instead of DHCP:
              Ethernet.begin(mac, ip);
            }
            // give the Ethernet shield a second to initialize:
            delay(1000);
            Serial.println("connecting...");
          
            // if you get a connection, report back via serial:
            if (client.connect(server, 80)) {
              Serial.println("connected. Enviando datos de entrada al servidor...");
              // Make a HTTP request:
              url = "GET /nuevoIngreso?ingreso[HoraEntrada]=1&ingreso[HoraSalida]=9999&ingreso[ConsumoParcial]=" + consumo + "&ingreso[aula_id]=1&ingreso[codigo]=" + lastRfid + " HTTP/1.1";
              client.println(url);
              Serial.println(url);
              client.println("Host: aula-inteligente.herokuapp.com");
              client.println("Connection: close");
              client.println();
            } else {
              // if you didn't get a connection to the server:
              Serial.println("connection failed");
            }
          
          Serial.println("disconnecting.");
          client.stop();
                 
          tft.fillScreen(TFT_BLACK); //Limpio el display
          opciones();         
        }
      }
    }
   
    ///////////////////////////////////////// CONTROL DE MOVIMIENTO ////////////////////////////////////////////////////////
    if (controlMovimiento(intervaloLuces)){
      if (luces) {
        releta('1');
        luces = false;
        cambioLuces();
        informoActividadDispositivo("estadoLuces", "Apagadas");
      }
    }
    
  }
}



///////////////////////////////////////// Access Denied  ///////////////////////////////////
void denied() {
  denegado();
  delay(1000);
}


///////////////////////////////////////// Get PICC's UID ///////////////////////////////////
uint8_t getID() {
  // Getting ready for Reading PICCs
  if ( ! mfrc522.PICC_IsNewCardPresent()) { //If a new PICC placed to RFID reader continue
    return 0;
  }
  if ( ! mfrc522.PICC_ReadCardSerial()) {   //Since a PICC placed get Serial and continue
    return 0;
  }
  // There are Mifare PICCs which have 4 byte or 7 byte UID care if you use 7 byte PICC
  // I think we should assume every PICC as they have 4 byte UID
  // Until we support 7 byte PICCs
  lastRfid = "";
  Serial.println(F("UID de PICC's Escaneada:"));
  for ( uint8_t i = 0; i < 4; i++) {  //
    readCard[i] = mfrc522.uid.uidByte[i];
    Serial.print(readCard[i], HEX);
    lastRfid += String(mfrc522.uid.uidByte[i], HEX);  //Guardo la clave UID de la tarjeta en us string para luego enviarla al servidor
  }
  Serial.println("");
  Serial.println(lastRfid);
  mfrc522.PICC_HaltA(); // Stop reading
  return 1;
}

void ShowReaderDetails() {
  // Get the MFRC522 software version
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print(F("MFRC522 Version de Software: 0x"));
  Serial.print(v, HEX);
  if (v == 0x91)
    Serial.print(F(" = v1.0"));
  else if (v == 0x92)
    Serial.print(F(" = v2.0"));
  else
    Serial.print(F(" (desconocida),probablemente una copia china?"));
  Serial.println("");
  // When 0x00 or 0xFF is returned, communication probably failed
  if ((v == 0x00) || (v == 0xFF)) {
    Serial.println(F("PRECAUCION: error de comunicacion, esta el MFRC522 conectado correctamente?"));
    Serial.println(F("SISTEMA DETENIDO: revise la conexion."));
    while (true); // do not go further
  }
}

//////////////////////////////////////// Read an ID from EEPROM //////////////////////////////
void readID( uint8_t number ) {
  uint8_t start = (number * 4 ) + 2;    // Figure out starting position
  for ( uint8_t i = 0; i < 4; i++ ) {     // Loop 4 times to get the 4 Bytes
    storedCard[i] = EEPROM.read(start + i);   // Assign values read from EEPROM to array
  }
}

///////////////////////////////////////// Add ID to EEPROM   ///////////////////////////////////
void writeID( byte a[] ) {
  if ( !findID( a ) ) {     // Before we write to the EEPROM, check to see if we have seen this card before!
    uint8_t num = EEPROM.read(0);     // Get the numer of used spaces, position 0 stores the number of ID cards
    uint8_t start = ( num * 4 ) + 6;  // Figure out where the next slot starts POR QUE + 6????
    num++;                // Increment the counter by one
    EEPROM.write( 0, num );     // Write the new count to the counter
    for ( uint8_t j = 0; j < 4; j++ ) {   // Loop 4 times
      EEPROM.write( start + j, a[j] );  // Write the array values to EEPROM in the right position
    }
    Serial.println(F("Correctamente agregado el ID a la EEPROM"));
  }
  else {
    Serial.println(F("Error! Hay algo mal en el ID o un error en la EEPROM"));
  }
}

///////////////////////////////////////// Remove ID from EEPROM   ///////////////////////////////////
void deleteID( byte a[] ) {
  if ( !findID( a ) ) {     // Before we delete from the EEPROM, check to see if we have this card!
    Serial.println(F("Error! Hay algo mal en el ID o un error en la EEPROM"));
  }
  else {
    uint8_t num = EEPROM.read(0);   // Get the numer of used spaces, position 0 stores the number of ID cards
    uint8_t slot;       // Figure out the slot number of the card
    uint8_t start;      // = ( num * 4 ) + 6; // Figure out where the next slot starts
    uint8_t looping;    // The number of times the loop repeats
    uint8_t j;
    uint8_t count = EEPROM.read(0); // Read the first Byte of EEPROM that stores number of cards
    slot = findIDSLOT( a );   // Figure out the slot number of the card to delete
    start = (slot * 4) + 2;
    looping = ((num - slot) * 4);
    num--;      // Decrement the counter by one
    EEPROM.write( 0, num );   // Write the new count to the counter
    for ( j = 0; j < looping; j++ ) {         // Loop the card shift times
      EEPROM.write( start + j, EEPROM.read(start + 4 + j));   // Shift the array values to 4 places earlier in the EEPROM
    }
    for ( uint8_t k = 0; k < 4; k++ ) {         // Shifting loop
      EEPROM.write( start + j + k, 0);
    }
    Serial.println(F("Correctamente removido el ID a la EEPROM"));
  }
}

///////////////////////////////////////// Check Bytes   ///////////////////////////////////
boolean checkTwo ( byte a[], byte b[] ) {
  if ( a[0] != 0 )      // Make sure there is something in the array first
    match = true;       // Assume they match at first
  for ( uint8_t k = 0; k < 4; k++ ) {   // Loop 4 times
    if ( a[k] != b[k] )     // IF a != b then set match = false, one fails, all fail
      match = false;
  }
  if ( match ) {      // Check to see if if match is still true
    return true;      // Return true
  }
  else  {
    return false;       // Return false
  }
}

///////////////////////////////////////// Find Slot   ///////////////////////////////////
uint8_t findIDSLOT( byte find[] ) {
  uint8_t count = EEPROM.read(0);       // Read the first Byte of EEPROM that
  for ( uint8_t i = 1; i <= count; i++ ) {    // Loop once for each EEPROM entry
    readID(i);                // Read an ID from EEPROM, it is stored in storedCard[4]
    if ( checkTwo( find, storedCard ) ) {   // Check to see if the storedCard read from EEPROM
      // is the same as the find[] ID card passed
      return i;         // The slot number of the card
      break;          // Stop looking we found it
    }
  }
}

///////////////////////////////////////// Find ID From EEPROM   ///////////////////////////////////
boolean findID( byte find[] ) {
  uint8_t count = EEPROM.read(0);     // Read the first Byte of EEPROM that
  for ( uint8_t i = 1; i <= count; i++ ) {    // Loop once for each EEPROM entry
    readID(i);          // Read an ID from EEPROM, it is stored in storedCard[4]
    if ( checkTwo( find, storedCard ) ) {   // Check to see if the storedCard read from EEPROM
      return true;
      break;  // Stop looking we found it
    }
  }
  return false;
}


////////////////////// Check readCard IF is masterCard   ///////////////////////////////////
// Check to see if the ID passed is the master programing card
boolean isMaster( byte test[] ) {
  if ( checkTwo( test, masterCard ) )
    return true;
  else
    return false;
}

// Muestra el titulo en el display TFT
void header(char *string)
{
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.fillRect(0, 0, 480, 60, TFT_BLUE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(string, 239, 2, 4);
}

// Muestra las opciones de encendido
void opciones()
{
    header("     Aula Inteligente     ");
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 60, 480, 320, TFT_WHITE);
    tft.drawString("Encender/ Apagar:", 20, 65, 4);
    tft.drawString("Luces:___  Presione 1", 20, 120, 4.5);
    tft.drawString("Proyector: Presione 2", 20, 170, 4.5);
    tft.drawString("Aire:_____ Presione 3", 20, 220, 4.5);
}

void esperando()
{
  tft.fillScreen(TFT_BLACK); //Limpio el display
  header("     Aula Inteligente     ");
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.fillRect(0, 60, 480, 320, TFT_WHITE);
  tft.drawString("Aproxime su tarjeta", 20, 65, 4); 
  tft.drawString("RFID para ingresar", 20, 120, 4);  
  tft.drawString("al sistema.", 20, 170, 4);
}

void bienvenida()
{
   tft.fillScreen(TFT_BLACK); //Limpio el display
   header("     Aula Inteligente     ");
   tft.setTextColor(TFT_BLACK, TFT_WHITE);
   tft.fillRect(0, 60, 480, 320, TFT_WHITE);
   tft.drawString("     Bienvenido al aula     ", 20, 80, 4);
}

void despedida()
{
    tft.fillScreen(TFT_BLACK); //Limpio el display
    header("     Aula Inteligente     ");
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 60, 480, 320, TFT_WHITE);
    tft.drawString("Hasta luego!", 20, 65, 4); 
    tft.drawString("Gracias por usar el", 20, 120, 4);  
    tft.drawString("sistema.", 20, 170, 4);
}

void cambioLuces()
{
  tft.fillScreen(TFT_BLACK); //Limpio el display
  opciones();
  if(luces){
    tft.drawString("Luces encendidas", 20, 270, 4.5);
  }
  else{
    tft.drawString("Luces apagadas", 20, 270, 4.5);
  }
}

void cambioProyector()
{
  tft.fillScreen(TFT_BLACK); //Limpio el display
  opciones();
  if(proyector){
    tft.drawString("Proyector encendido", 20, 270, 4.5);
  }
  else{
    tft.drawString("Proyector apagado", 20, 270, 4.5);
  }
}

void cambioAire()
{
  tft.fillScreen(TFT_BLACK); //Limpio el display
  opciones();
  if(aire){
    tft.drawString("Aire encendido", 20, 270, 4.5);
  }
  else{
    tft.drawString("Aire apagado", 20, 270, 4.5);
  }
}


void denegado()
{
    tft.fillScreen(TFT_BLACK); //Limpio el display
    header("     Aula Inteligente     ");
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 60, 480, 320, TFT_WHITE);
    tft.drawString("No se encuentra", 20, 65, 4); 
    tft.drawString("registrado, comuniquese", 20, 120, 4);  
    tft.drawString("con el administrador.", 20, 170, 4);
}

void usuarioActivo()
{
    tft.fillScreen(TFT_BLACK); //Limpio el display
    header("     Aula Inteligente     ");
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 60, 480, 320, TFT_WHITE);
    tft.drawString("El usuario anterior", 20, 65, 4); 
    tft.drawString("no cerro su sesion", 20, 120, 4);  
}

void usuarioCambiado()
{
    Serial.print(F("Sesion del usuario anterior cerrada correctamente"));
    Serial.print(F("Puede utilizar el sistema"));
          
    tft.fillScreen(TFT_BLACK); //Limpio el display
    header("     Aula Inteligente     ");
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 60, 480, 320, TFT_WHITE);
    tft.drawString("Se cerro la sesion", 20, 65, 4); 
    tft.drawString("del usuario anterior", 20, 120, 4);  
    tft.drawString("puede usar el sistema.", 20, 170, 4);
}


void encendiendoDispositivo()
{
    tft.fillScreen(TFT_BLACK); //Limpio el display
    header("     Aula Inteligente     ");
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 60, 480, 320, TFT_WHITE);
    tft.drawString("Aguarde por favor", 20, 65, 4); 
    tft.drawString("encendiendo...", 20, 120, 4);  
}

void apagandoDispositivo()
{
    tft.fillScreen(TFT_BLACK); //Limpio el display
    header("     Aula Inteligente     ");
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 60, 480, 320, TFT_WHITE);
    tft.drawString("Aguarde por favor", 20, 65, 4); 
    tft.drawString("apagando...", 20, 120, 4);  
}


void releta(char rele) {
  switch (rele) {
    case '1'  :
      digitalWrite (relePin1, !(digitalRead(relePin1)));
      break;
    case '2'  :
      digitalWrite (relePin2, !(digitalRead(relePin2)));
      break;
    case '3'  :
      digitalWrite (relePin3, !(digitalRead(relePin3)));
      break;
    case '4'  :
      digitalWrite (relePin4, !(digitalRead(relePin4)));
      break;
  }
}

// Funcion para control del movimiento, recibe como parámetro el tiempo 
boolean controlMovimiento(long intervalo){
  int valorSensor1 = digitalRead(sensor1); //Leo el valor del sensor 1
  if (valorSensor1 == HIGH)  //Si esta en alto = movimiento
  {
    previoMillis = millis();  //Guardo el instante en que se detecto movimiento
  }
  if (millis() - previoMillis  > intervalo) { 
    return true; // Se supero el intervalo establecido
  }
  else{
    return false;
  }
}

//Funcion para enviar al servidor el apagado o encendido de un dispositivo
void informoActividadDispositivo(String estadoDispositivo, String actividad){
   // start the Ethernet connection:
  if (Ethernet.begin(mac, 10000, 10000) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // try to congifure using IP address instead of DHCP:
    Ethernet.begin(mac, ip);
  }
  // give the Ethernet shield a second to initialize:
  delay(1000);
  Serial.println("connecting...");
  
  // if you get a connection, report back via serial:
  if (client.connect(server, 80)) {
    Serial.println("connected. Enviando datos de apagado del "+ estadoDispositivo  +"al servidor...");
    // Make a HTTP request:
    url = "GET /eventoAula?aula_id=" + aula_id + "&aula[temperatura]=" + String(temperatura) + "&aula[" + estadoDispositivo + "]=" + actividad + " HTTP/1.1";
    client.println(url);
    Serial.println(url);
    client.println("Host: aula-inteligente.herokuapp.com");
    client.println("Connection: close");
    client.println();
  } else {
    // if you didn't get a connection to the server:
    Serial.println("connection failed");
  }
    
  Serial.println("disconnecting.");
  client.stop();
}

//Funcion para la medición y el envio de los datos de la temperatura
void medicionTemperatura(){
  lecturaTemp = analogRead(voutTemp);
  tensionTemp = 5 * lecturaTemp;
  tensionTemp = tensionTemp / 1024;
  temperatura = tensionTemp * 100;
  temperatura = temperatura;
  if (enviaTemperatura) {
    previoMillisTemp = millis();
    enviaTemperatura = false;
    //ENVIO DATOS DE LA TEMPERATURA
    // start the Ethernet connection:
    if (Ethernet.begin(mac, 10000, 10000) == 0) {
      // try to congifure using IP address instead of DHCP:
      Ethernet.begin(mac, ip);
    }
    // give the Ethernet shield a second to initialize:
    delay(1000);
    // if you get a connection, report back via serial:
    if (client.connect(server, 80)) {
      // Make a HTTP request:
      url = "GET /eventoAula?aula_id=" + aula_id + "&aula[temperatura]=" + String(temperatura) + " HTTP/1.1";
      client.println(url);
      client.println("Host: aula-inteligente.herokuapp.com");
      client.println("Connection: close");
      client.println();
    }
    client.stop();
  }
  if (millis() - previoMillisTemp > intervaloTemp ) {
    enviaTemperatura = true;
  } 
}


//Funcion para la medicion de la corriente electrica
void medicionElectrica(){
    potencia = GetLineVoltage();
    consumoParcial += potencia; 
    Serial.print("Potencia: ");
    Serial.println(potencia); 
}
  
