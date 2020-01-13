/*
 * Name: Battery Monitor Board (BMB)
 * Author: Frank Mitchell
 * Date Started: 6/24/19
 * Desc: Firmware for Sub8 battery monitor board
 */

/*     ******** ADC PIN MAPPING ********
 *
 *   Cell   | 00 | 01 | 02 | 03 | 04 | 05
 *-----------------------------------------
 * Battery0 | D2 | D3 | E3 | E2 | E1 | E0
 * ----------------------------------------
 * Battery1 | D1 | D0 | E5 | E4 | B4 | B5
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/timer.h"
#include "driverlib/pwm.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"

#include "MIL_CLK.h"
#include "MIL_CAN.h"
#include "MIL_ADC.h"

//-------------------- Constants --------------------
const uint8_t CELL_MSG_LEN = 3;
const uint8_t BUFF_SIZE_INT = 7;
const uint8_t BUFF_SIZE = 0x7F;
const double refVoltage = 3.0;

const uint32_t BMB_CANID = 0x44; //task group 4, ECU 4
const uint32_t BMB_FILTID_bm = 0xFF;
const uint8_t BMB_CAN_MSG_LEN = 3;
const uint32_t BMB_CAN_BASE = CAN1_BASE;
//------------------ End Constants ------------------


//-------------------- Variables --------------------
uint8_t msgData[BMB_CAN_MSG_LEN] = {0}; //CAN Buffer
uint8_t cellMsg[CELL_MSG_LEN] = "x37"; //byte 0 is batt and cell num (top nibble battery, bottom nibble cell)
                                       //and  byte 1-2 is 16 bit voltage

uint32_t tempData[12]; //temp data from ADC
uint16_t bufferIndex = 0;

uint16_t Bat0Cell0[BUFF_SIZE]; //Battery 0 Cell 0 Buffer
uint16_t Bat0Cell1[BUFF_SIZE]; //Battery 0 Cell 1 Buffer
uint16_t Bat0Cell2[BUFF_SIZE]; //Battery 0 Cell 2 Buffer
uint16_t Bat0Cell3[BUFF_SIZE]; //Battery 0 Cell 3 Buffer
uint16_t Bat0Cell4[BUFF_SIZE]; //Battery 0 Cell 4 Buffer
uint16_t Bat0Cell5[BUFF_SIZE]; //Battery 0 Cell 5 Buffer
uint32_t Bat0Cell[6] = {0}; //battery0 cell voltages

uint16_t Bat1Cell0[BUFF_SIZE]; //Battery 1 Cell 0 Buffer
uint16_t Bat1Cell1[BUFF_SIZE]; //Battery 1 Cell 1 Buffer
uint16_t Bat1Cell2[BUFF_SIZE]; //Battery 1 Cell 2 Buffer
uint16_t Bat1Cell3[BUFF_SIZE]; //Battery 1 Cell 3 Buffer
uint16_t Bat1Cell4[BUFF_SIZE]; //Battery 1 Cell 4 Buffer
uint16_t Bat1Cell5[BUFF_SIZE]; //Battery 1 Cell 5 Buffer
uint32_t Bat1Cell[6] = {0}; //battery1 cell voltages

uint16_t test = 0x00;
uint32_t testData[3] = {0, 0, 0};
double testVoltage[3] = {0.0, 0.0, 0.0};

//------------------ End Variables ------------------


//-------------------- Functions --------------------
//Timer0 Initialization
void initTimer0(void (*pISR)(void), float period_ms){
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0));
    TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_B_PERIODIC);
    TimerPrescaleSet(TIMER0_BASE, TIMER_B, 0xFF);
    TimerLoadSet(TIMER0_BASE, TIMER_B, period_ms/1000 * SysCtlClockGet()/0xFF);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMB_TIMEOUT);
    TimerIntRegister(TIMER0_BASE, TIMER_B, pISR);
    IntEnable(INT_TIMER0B);
    TimerEnable(TIMER0_BASE, TIMER_B);
}

//Initializes GPIO pins and ports for use
void initGPIO(){
    //Initialize peripherals
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    //SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
    //SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);

    //GPIO
    GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, GPIO_PIN_7); //PORTB 7 (Debug switch)
    GPIOPinTypeGPIOOutput(GPIO_PORTB_BASE, GPIO_PIN_6); //PORTB 6 (Debug LED)

    //ADC Channels
    GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, GPIO_PIN_4 | GPIO_PIN_5);
    GPIOPinTypeGPIOInput(GPIO_PORTD_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
    GPIOPinTypeGPIOInput(GPIO_PORTE_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);
    //MIL_ADCPinConfig(0x0FFF); //CH0-11 (B4-5, D0-3, E0-5)
    MIL_ADCPinConfig(MIL_ADC_CH4_PD3_bm | MIL_ADC_CH5_PD2_bm | MIL_ADC_CH6_PD1_bm);

    //I2C
    //GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2); //PORTB 2
    //GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3); //PORTB 3

    //UART
    //GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1); //PORTB 0-1
}

//Fill the cell voltage buffers to prevent from killing on startup
void fillBuffers(void){
    for(int i = 0; i < BUFF_SIZE; i++){
        Bat0Cell0[i] = 0xFF;
        Bat0Cell1[i] = 0xFF;
        Bat0Cell2[i] = 0xFF;
        Bat0Cell3[i] = 0xFF;
        Bat0Cell4[i] = 0xFF;
        Bat0Cell5[i] = 0xFF;

        Bat1Cell0[i] = 0xFF;
        Bat1Cell1[i] = 0xFF;
        Bat1Cell2[i] = 0xFF;
        Bat1Cell3[i] = 0xFF;
        Bat1Cell4[i] = 0xFF;
        Bat1Cell5[i] = 0xFF;
    }
}

//Clears the cell average voltages
void clearVoltages(void){
    for(int i = 0; i < 6; i++){
        Bat0Cell[i] = 0;
        Bat1Cell[i] = 0;
    }
}

//Sums the cell buffers to find average cell voltage
void sumBuffers(void){
    clearVoltages(); //make sure cell averages are clear
    for(int i = 0; i < BUFF_SIZE; i++){ //sum every value in buffer
        Bat0Cell[0] += Bat0Cell0[i];
        Bat0Cell[1] += Bat0Cell1[i];
        Bat0Cell[2] += Bat0Cell2[i];
        Bat0Cell[3] += Bat0Cell3[i];
        Bat0Cell[4] += Bat0Cell4[i];
        Bat0Cell[5] += Bat0Cell5[i];

        Bat1Cell[0] += Bat1Cell0[i];
        Bat1Cell[1] += Bat1Cell1[i];
        Bat1Cell[2] += Bat1Cell2[i];
        Bat1Cell[3] += Bat1Cell3[i];
        Bat1Cell[4] += Bat1Cell4[i];
        Bat1Cell[5] += Bat1Cell5[i];
    }

    for(int i = 0; i < 6; i++){
        Bat0Cell[i] = (Bat0Cell[i]>>BUFF_SIZE_INT); //find average of cells
        Bat1Cell[i] = (Bat1Cell[i]>>BUFF_SIZE_INT);
    }
}

//Updates battery message
void updateMessage(uint8_t batteryNum, uint8_t cellNum){
    cellMsg[0] = (batteryNum<<4) + cellNum; //set battery & cell num

    //split and store top and bottom bytes
    if(batteryNum == 0){ //Battery 0
        cellMsg[1] = (Bat0Cell[cellNum]>>8);
        cellMsg[2] = Bat0Cell[cellNum];
    }
    else if(batteryNum == 1){ //Battery 1
        cellMsg[1] = (Bat1Cell[cellNum]>>8);
        cellMsg[2] = Bat1Cell[cellNum];
    }
}

//Timer0 ISR, used for timing between ADC samples
void TIMER0_ISR(void){
    TimerIntClear(TIMER0_BASE, TIMER_TIMB_TIMEOUT);

    MIL_ADCGetData(ADC0_BASE, MIL_ADC_SEQ0, 10, &testData); //get data from ADC
    for(int i = 0; i < 3; i++){
        testVoltage[i] = 3.3*(testData[i]/4095.0); //convert to voltage
    }
    /*MIL_ADCGetData(ADC0_BASE, MIL_ADC_SEQ0, 10, tempData); //get data from ADC

    Bat0Cell0[(bufferIndex & BUFF_SIZE)] = tempData[5]; //load from channel into respective cell
    Bat0Cell1[(bufferIndex & BUFF_SIZE)] = tempData[4];
    Bat0Cell2[(bufferIndex & BUFF_SIZE)] = tempData[0];
    Bat0Cell3[(bufferIndex & BUFF_SIZE)] = tempData[1];
    Bat0Cell4[(bufferIndex & BUFF_SIZE)] = tempData[2];
    Bat0Cell5[(bufferIndex & BUFF_SIZE)] = tempData[3];

    Bat0Cell0[(bufferIndex & BUFF_SIZE)] = tempData[6];
    Bat1Cell1[(bufferIndex & BUFF_SIZE)] = tempData[7];
    Bat1Cell2[(bufferIndex & BUFF_SIZE)] = tempData[8];
    Bat1Cell3[(bufferIndex & BUFF_SIZE)] = tempData[9];
    Bat1Cell4[(bufferIndex & BUFF_SIZE)] = tempData[10];
    Bat1Cell5[(bufferIndex & BUFF_SIZE)] = tempData[11];
    bufferIndex++; //inc index
    sumBuffers(); //find cell avg*/
    GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_6, test); //test timing of ISR
    if(test == 0x00){
        test = 0xFF;
    }
    else{
        test = 0x00;
    }
}
//------------------ End Functions ------------------


//---------------------- Main -----------------------
int main(void){
    MIL_ClkSetInt_16MHz(); //set CLK to 16MHz

    initGPIO(); //initialize pins
    //fillBuffers(); //fill cell voltage buffers

    MIL_CANPortClkEnable(MIL_CAN_PORT_A); //enable PORTA
    MIL_InitCAN(MIL_CAN_PORT_A, CAN1_BASE); //configure CAN1 on PORTA
    //MIL_CAN_MailBox_t CAN_MsgBox = {.canid = BMB_CANID, .filt_mask = BMB_FILTID_bm,.base = BMB_CAN_BASE,.msg_len = BMB_CAN_MSG_LEN,.obj_num = 1,.rx_flag_int = 0,.buffer = msgData};
    //MIL_InitMailBox(&CAN_MsgBox); //initialize mailbox

    //MIL_ADCSeqInit(ADC0_BASE, MIL_ADC_SEQ0, 0xFFF, MIL_ADC_TimTrig);
    MIL_ADCSeqInit(ADC0_BASE, MIL_ADC_SEQ0, MIL_ADC_CH4_PD3_bm | MIL_ADC_CH5_PD2_bm | MIL_ADC_CH6_PD1_bm, MIL_ADC_TimTrig);

    initTimer0(&TIMER0_ISR, 100); //initialize timer0 for 100ms period
    TimerControlTrigger(TIMER0_BASE, TIMER_B, true);

    IntMasterEnable(); //master interrupt enable

    while(1){
        for(int i = 0; i < 2; i++){
            for(int j = 0; j < 6; j++){
                /*GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_6, 0x00);
                //updateMessage(i, j); //update messages as necessary
                MIL_CANSimpleTX(BMB_CANID, cellMsg, CELL_MSG_LEN, BMB_CAN_BASE); //send battery message
                SysCtlDelay(16000000); //delay
                GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_6, 0xFF);*/
                SysCtlDelay(16000000); //delay
            }
        }
    }
}
//-------------------- End Main --------------------
