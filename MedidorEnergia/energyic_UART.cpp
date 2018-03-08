 /* ATM90E26 MEDIDOR DE ENERGIA librer�a

    The MIT License (MIT)

  Copyright (c) 2017 Kelly Martin

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include "energyic_UART.h"

unsigned short CommEnergyIC(unsigned char RW,unsigned char address, unsigned short val)
{
  unsigned short output;
  //Seteo el flag de lectura/escritura
  address|=RW<<7;

  unsigned char host_chksum = address;
  if(!RW) //Si es una operacion de escritura en registro
  {
    unsigned short chksum_short = (val>>8) + (val&0xFF) + address;
    host_chksum = chksum_short & 0xFF;
  }

  //Comienza el comando UART
  Serial1.write(0xFE); //primer byte enviado para que el ATM90E26 detecte el baud rate.
  Serial1.write(address); //segundo byte enviado, RW_ADDRESS, which has a R/W bit (bit7) and 7 address bits (bit6-0).


  if(!RW) //Si es una operacion de escritura en registro
  {
	  unsigned char MSBWrite = val>>8;
	  unsigned char LSBWrite = val&0xFF;

      Serial1.write(MSBWrite);
      Serial1.write(LSBWrite);
  }
  Serial1.write(host_chksum);
  delay(10);

  //Solo para operaciones de lectura
  if(RW) //Operacion de lectura
  {
    byte MSByte = Serial1.read();
    byte LSByte = Serial1.read();
    byte atm90_chksum = Serial1.read();

    if(atm90_chksum == ((LSByte + MSByte) & 0xFF))
    {
      output=(MSByte << 8) | LSByte; //junto MSB y LSB;
      return output;
    }
    Serial.println("Fallo la lectura");
    delay(20); // Delay de una transaccion fallida
    return 0xFFFF;
  }

  else //Operacion de escritura
  {
	   byte atm90_chksum = Serial1.read();

    if(atm90_chksum != host_chksum)
    {
    	Serial.println("Fallo la escritura \n");
    	delay(20); // Delay de una transaccion fallida
    }
  }
  return 0xFFFF;
}


double  GetLineVoltage(){
	unsigned short voltage=CommEnergyIC(1,Urms,0xFFFF);
	return (double)voltage/100;
}

unsigned short  GetMeterStatus(){
  return CommEnergyIC(1,EnStatus,0xFFFF);
}

double GetLineCurrent(){
	unsigned short current=CommEnergyIC(1,Irms,0xFFFF);
	return (double)current/1000;
}

double GetActivePower(){
	short int apower= (short int)CommEnergyIC(1,Pmean,0xFFFF); //Complemento, MSB es el bit con signo
	return (double)apower;
}

double GetReactivePower(){
	short int apower= (short int)CommEnergyIC(1,Qmean,0xFFFF); //Complemento, MSB es el bit con signo
	return (double)apower;
}

double GetApparentPower(){
	short int apower= (short int)CommEnergyIC(1,Smean,0xFFFF); //Complemento, MSB es el bit con signo
	return (double)apower;
}

double GetFrequency(){
	unsigned short freq=CommEnergyIC(1,Freq,0xFFFF);
	return (double)freq/100;
}

double GetPowerFactor(){
	short int pf= (short int)CommEnergyIC(1,PowerF,0xFFFF); //Complemento, MSB es el bit con signo
	//Si es negativo
	if(pf&0x8000){
		pf=(pf&0x7FFF)*-1;
	}
	return (double)pf/1000;
}

double GetImportEnergy(){
	//El registro se limpia luego de una lectura
	unsigned short ienergy=CommEnergyIC(1,APenergy,0xFFFF);
	return (double)ienergy/10/1000; //devuelve kWh si PL esta seteado a 1000imp/kWh
}

double GetExportEnergy(){
	//El registro se limpia luego de una lectura
	unsigned short eenergy=CommEnergyIC(1,ANenergy,0xFFFF);
	return (double)eenergy/10/1000; //devuelve kWh si PL esta seteado a 1000imp/kWh
}

unsigned short GetSysStatus(){
	return CommEnergyIC(1,SysStatus,0xFFFF);
}


void InitEnergyIC(){
	unsigned short systemstatus;

	CommEnergyIC(0,SoftReset,0x789A); //Realiza un soft reset
	CommEnergyIC(0,FuncEn,0x0030); //Voltage sag irq=1, report on warnout pin=1, energy dir change irq=0
	CommEnergyIC(0,SagTh,0x1F2F); //Voltage sag threshhold


	//Seteo los valores de calibracion para la medicion
	CommEnergyIC(0,CalStart,0x5678); //Comando de comiezo de calibracion. Registros 21 a 2B necesitan ser seteados
	CommEnergyIC(0,PLconstH,0x00B9); //PL Constante MSB
	CommEnergyIC(0,PLconstL,0xC1F3); //PL Constante LSB
	CommEnergyIC(0,Lgain,0x1D39); 	//Calibracion de la ganancia de linea
	CommEnergyIC(0,Lphi,0x0000); //Calibracion del angulo de linea
	CommEnergyIC(0,PStartTh,0x08BD); //Limite de la potencia de arrranque
	CommEnergyIC(0,PNolTh,0x0000); //Limite de la potencia activa sin carga
	CommEnergyIC(0,QStartTh,0x0AEC); //Limite de la potecnia reactia de arranquee
	CommEnergyIC(0,QNolTh,0x0000); //Limite de la potencia reactiva sin carga
	CommEnergyIC(0,MMode,0x9422); //Configuracion del modo de medicion. Valores por default. Ver pag 31 del datasheet
	CommEnergyIC(0,CSOne,0x4A34); //Escribo CSOne calculados

	Serial.println("Checksum 1:");
	//uartWriteString(UART_USB, CommEnergyIC(1,CSOne,0x0000)); //HEX Checksum 1. Necesita ser calculado a partir de los valores dados.

	//Seteo los valores de calibracion de la medicion.
	CommEnergyIC(0,AdjStart,0x5678); //Comando de inicio de la calibracion de medicion, registros 31-3A
	CommEnergyIC(0,Ugain,0x5F74);    //Ganancia del Voltage rms 0xD464
	CommEnergyIC(0,IgainL,0x6E49);   //Ganancia de corriente de linea L 0x6E49
	CommEnergyIC(0,IgainN,0x0000);
	CommEnergyIC(0,Uoffset,0x0000);  //Offset del voltage
	CommEnergyIC(0,IoffsetL,0x0000); //Offset de la corriente de linea
	CommEnergyIC(0,PoffsetL,0x0000); //Offset de la potencia activa de linea
	CommEnergyIC(0,QoffsetL,0x0000); //Offset de la potencia ractia de linea
	CommEnergyIC(0,PoffsetN,0x0000);
	CommEnergyIC(0,QoffsetN,0x0000);
	CommEnergyIC(0,CSTwo,0x0C8A); //Escribo CSTwo, calculados, (pag 38 datasheet) basados en Ugain y IgainL 0xD294

	Serial.println("Checksum 2:");
	//uartWriteString(UART_USB, CommEnergyIC(1,CSTwo,0x0000));  //HEX Checksum 2.  Necesita ser calculado a partir de los valores dados antes.

	CommEnergyIC(0,CalStart,0x8765); //Chequeo que esten correctos los registros 21-2B y comienza con la medicion si estan OK
	CommEnergyIC(0,AdjStart,0x8765); //Chequeo que esten correctos los registros 31-3A y comienza con la medicion si estan OK

	systemstatus = GetSysStatus();

	if (systemstatus&0xC000){
		//checksum 1 error
		Serial.println("Checksum 1 Error!");
	}
	if (systemstatus&0x3000){
		//checksum 2 error
		Serial.println("Checksum 2 Error!");
	}
}
